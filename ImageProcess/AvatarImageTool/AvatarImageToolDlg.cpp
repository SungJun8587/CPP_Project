
// AvatarImageToolDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "AvatarImageTool.h"
#include "AvatarImageToolDlg.h"
#include "afxdialogex.h"
#include "AvatarInfoMemDBEx.h"

CCharacterInfoMemDBEx   g_CharacterInfoMem;
CItemInfoListMemDBEx	g_ItemInfoListMem;
CExpressionInfoMemDBEx	g_ExpressionInfoMem;
CAvatarBgInfoMemDBEx	g_AvatarBgInfoListMem;

//*************************************************************************
//
static COLORREF GetListColor(UINT_PTR nID)
{
	switch( nID )
	{
	case IDC_AVATAR_LIST:				return RGB(70, 110, 180);	// 파랑
	case IDC_ITEM_LIST:					return RGB(60, 140, 90);	// 초록
	case IDC_ITEMDRAW_LIST:				return RGB(190, 100, 40);	// 주황
	case IDC_CHAR_GENERAL_ITEM_LIST:	return RGB(130, 80, 160);	// 보라
	}
	return RGB(70, 110, 180);
}

//*************************************************************************
// GIF 프레임 지연시간(ms) 변환. delayCs는 1/100초 단위, 0~1이면 브라우저 관례대로 100ms
static UINT GifDelayMs(const GifCodec::Frame& fr)
{
	return (fr.delayCs <= 1) ? 100 : static_cast<UINT>(fr.delayCs) * 10;
}

//*************************************************************************
// 애니메이션 GIF를 모든 프레임(합성 완료)으로 디코드. nDestW/nDestH > 0 이면 프레임마다 리사이즈
static bool LoadGifFrames(const TCHAR* ptszPath, std::vector<GifCodec::Frame>& out, int nDestW = 0, int nDestH = 0)
{
	out.clear();

	FILE* fp = nullptr;
	if( _tfopen_s(&fp, ptszPath, _T("rb")) != 0 || fp == nullptr ) return false;

	std::vector<uint8_t> bytes;
	uint8_t tmp[4096];
	size_t n = 0;
	while( (n = fread(tmp, 1, sizeof(tmp), fp)) > 0 )
		bytes.insert(bytes.end(), tmp, tmp + n);
	fclose(fp);

	if( bytes.empty() ) return false;

	try
	{
		GifCodec codec;
		out = codec.DecodeAllFrames(bytes.data(), bytes.size());

		if( nDestW > 0 && nDestH > 0 )
		{
			for( auto& fr : out )
			{
				if( static_cast<int>(fr.image.Width()) == nDestW && static_cast<int>(fr.image.Height()) == nDestH ) continue;

				ImageProcessor proc(std::move(fr.image));
				proc.Resize(static_cast<uint32_t>(nDestW), static_cast<uint32_t>(nDestH), ResizeMethod::Bilinear);
				fr.image = std::move(proc.Buffer());
			}
		}
	}
	catch( const ImageException& )
	{
		out.clear();
		return false;
	}

	return !out.empty();
}

//*************************************************************************
// 쿼리 구간에서 token(예: "char=")으로 시작하는 값을 추출
static BOOL GetQueryValue(const TCHAR* ptszQuery, const TCHAR* ptszToken, TCHAR* ptszOut, size_t nOutLen)
{
	const size_t nTokenLen = _tcslen(ptszToken);
	const TCHAR* p = ptszQuery;

	while( *p )
	{
		const TCHAR* pEnd = _tcschr(p, _T('&'));
		const size_t nSegLen = pEnd ? static_cast<size_t>(pEnd - p) : _tcslen(p);

		if( nSegLen >= nTokenLen && _tcsncmp(p, ptszToken, nTokenLen) == 0 )
		{
			const size_t nValLen = nSegLen - nTokenLen;
			if( nValLen == 0 || nValLen >= nOutLen ) return FALSE;	// 비었거나 버퍼 초과

			_tcsncpy_s(ptszOut, nOutLen, p + nTokenLen, nValLen);
			return TRUE;
		}

		if( !pEnd ) break;
		p = pEnd + 1;
	}
	return FALSE;
}

BEGIN_MESSAGE_MAP(CColorHeaderCtrl, CHeaderCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
	ON_NOTIFY_REFLECT(HDN_BEGINTRACKA, OnBeginTrack)
	ON_NOTIFY_REFLECT(HDN_BEGINTRACKW, OnBeginTrack)
	ON_NOTIFY_REFLECT(HDN_DIVIDERDBLCLICKA, OnBeginTrack)
	ON_NOTIFY_REFLECT(HDN_DIVIDERDBLCLICKW, OnBeginTrack)
END_MESSAGE_MAP()

void CColorHeaderCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMCUSTOMDRAW pCD = reinterpret_cast<LPNMCUSTOMDRAW>(pNMHDR);
	CDC* pDC = CDC::FromHandle(pCD->hdc);

	switch( pCD->dwDrawStage )
	{
	case CDDS_PREPAINT:
	{
		m_nLastVisible = -1;
		for( int k = GetItemCount() - 1; k >= 0; k-- )
		{
			CRect rcItem;
			GetItemRect(k, &rcItem);
			if( rcItem.Width() > 0 ) { m_nLastVisible = k; break; }
		}

		// 굵은 글꼴은 한 번만 생성
		if( m_fontBold.GetSafeHandle() == nullptr )
		{
			LOGFONT lf = {};
			CFont* pFont = GetFont();
			if( pFont && pFont->GetLogFont(&lf) )
			{
				lf.lfWeight = FW_BOLD;

				// lfHeight는 음수(문자 높이)가 일반적이라 절댓값을 줄임. 예: -13 -> -11
				lf.lfHeight = (lf.lfHeight < 0) ? lf.lfHeight + 2 : lf.lfHeight - 2;

				m_fontBold.CreateFontIndirect(&lf);
			}
		}

		CRect rcAll;
		GetClientRect(&rcAll);
		pDC->FillSolidRect(&rcAll, m_clrBk);
		pDC->FillSolidRect(rcAll.left, rcAll.bottom - 1, rcAll.Width(), 1, RGB(120, 126, 138));	// 아래 구분선
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;
	}

	case CDDS_ITEMPREPAINT:
	{
		CRect rc(pCD->rc);
		if( rc.Width() > 0 )			// 폭 0인 번호 컬럼은 건너뜀
		{
			TCHAR tszText[64] = { 0 };
			HDITEM hdi = {};
			hdi.mask = HDI_TEXT | HDI_FORMAT;
			hdi.pszText = tszText;
			hdi.cchTextMax = _countof(tszText);
			GetItem(static_cast<int>(pCD->dwItemSpec), &hdi);

			CFont* pOld = pDC->SelectObject(m_fontBold.GetSafeHandle() ? &m_fontBold : GetFont());
			pDC->SetBkMode(TRANSPARENT);
			pDC->SetTextColor(m_clrText);

			CRect rcText(rc);
			rcText.DeflateRect(4, 0, 4, 1);
			pDC->DrawText(tszText, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
			pDC->SelectObject(pOld);

			// 컬럼 사이 세로 구분선 (마지막 컬럼은 제외)
			if( static_cast<int>(pCD->dwItemSpec) != m_nLastVisible )
				pDC->FillSolidRect(rc.right - 1, rc.top + 3, 1, rc.Height() - 8, RGB(160, 165, 175));
		}
		*pResult = CDRF_SKIPDEFAULT;
		break;
	}

	default:
		*pResult = CDRF_DODEFAULT;
		break;
	}
}

void CColorHeaderCtrl::OnBeginTrack(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	*pResult = TRUE;		// 크기 조절 취소
}

// CAvatarImageToolDlg 대화 상자

CAvatarImageToolDlg::CAvatarImageToolDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_AVATARIMAGETOOL_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	memset(m_bAboveBuffer, 0, sizeof(m_bAboveBuffer));
	memset(m_bExprBuffer, 0, sizeof(m_bExprBuffer));
	memset(m_bUnderBuffer, 0, sizeof(m_bUnderBuffer));
	memset(m_bTotalBuffer, 0, sizeof(m_bTotalBuffer));

	m_nAboveBufferSize = 0;
	m_nExprBufferSize = 0;
	m_nUnderBufferSize = 0;
	m_nTotalBufferSize = 0;
	m_nAvatar = 0;
	m_nSubType = 0;
	m_nGender = 0;
	m_nDrawStyle = 0;
	m_nExpression = 0;
	m_nAvatarBg = 0;
	m_tszEncString[0] = _T('\0');

	m_bManAvatar = true;
	m_bWomanAvatar = false;
	m_bAvatarReady = false;

	m_pItemImageCreateDlg = nullptr;
}

void CAvatarImageToolDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);

	//{{AFX_DATA_MAP(CAvatarImageToolDlg)
	DDX_Control(pDX, IDCANCEL, m_ctrlCancel);
	DDX_Control(pDX, IDC_LIST_DELETE, m_ctrlListDelete);
	DDX_Control(pDX, IDC_RIGHT_DRAW, m_ctrlRightDraw);
	DDX_Control(pDX, IDC_LEFT_DRAW, m_ctrlLeftDraw);
	DDX_Control(pDX, IDC_CHAR_GENERAL_ITEM_LIST, m_ctrlCharacterGeneralItemList);
	DDX_Control(pDX, IDC_AVATAR_LIST, m_ctrlAvatarList);
	DDX_Control(pDX, IDC_ITEMDRAW_LIST, m_ctrlItemDrawList);
	DDX_Control(pDX, IDC_ITEM_LIST, m_ctrlItemList);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAvatarImageToolDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()

	ON_WM_TIMER()
	ON_WM_DESTROY()

	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()

	ON_NOTIFY(NM_DBLCLK, IDC_AVATAR_LIST, OnDblclkAvatarList)
	ON_NOTIFY(NM_DBLCLK, IDC_ITEM_LIST, OnDblclkItemList)
	ON_NOTIFY(NM_CLICK, IDC_ITEMDRAW_LIST, OnClickItemdrawList)

	ON_NOTIFY(NM_CUSTOMDRAW, IDC_AVATAR_LIST, OnCustomdrawListSelect)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_ITEM_LIST, OnCustomdrawListSelect)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_ITEMDRAW_LIST, OnCustomdrawListSelect)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CHAR_GENERAL_ITEM_LIST, OnCustomdrawListSelect)

	ON_CBN_SELCHANGE(IDC_SUBTYPE, OnSelchangeSubtype)
	ON_CBN_SELCHANGE(IDC_GENDER, OnSelchangeGender)
	ON_CBN_SELCHANGE(IDC_EXPRESSION, OnSelchangeExpression)

	ON_BN_CLICKED(IDC_LIST_DELETE, OnListDelete)

	ON_CBN_SELCHANGE(IDC_DRAWSTYLE, OnSelchangeDrawstyle)
	ON_CBN_SELCHANGE(IDC_AVATAR_BG, OnSelchangeAvatarBg)

	ON_BN_CLICKED(IDC_LEFT_DRAW, OnLeftDraw)
	ON_BN_CLICKED(IDC_RIGHT_DRAW, OnRightDraw)
	ON_BN_CLICKED(IDC_IMAGEFILECREATE, OnImageFileCreate)
END_MESSAGE_MAP()

// CAvatarImageToolDlg 메시지 처리기

BOOL CAvatarImageToolDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 타이틀바 제거
	ModifyStyle(WS_CAPTION | WS_SYSMENU, 0, SWP_FRAMECHANGED);

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	m_ctrlGenderList.Init(m_hWnd, IDC_GENDER);
	m_ctrlSubTypeList.Init(m_hWnd, IDC_SUBTYPE);
	m_ctrlExprList.Init(m_hWnd, IDC_EXPRESSION);
	m_ctrlAvatarBgList.Init(m_hWnd, IDC_AVATAR_BG);
	m_ctrlDrawStyleList.Init(m_hWnd, IDC_DRAWSTYLE);

	// TODO: 여기에 추가 초기화 작업을 추가합니다.
	int nSize = 0;
	TCHAR tszTargetFile[FULLPATH_STRLEN] = { 0 };

	CCharacterInfo* pCharacterInfo = nullptr;
	CItemInfoList* pItemInfoList = nullptr;
	CExpressionInfo* pExpressionInfo = nullptr;

	_sntprintf_s(tszTargetFile, _countof(tszTargetFile), _TRUNCATE, _T("%s%s"), AVATARITEM_AVATARINFO_FILEPATH, _T("characterinfo.txt"));
	ReadCharacterInfo(tszTargetFile, pCharacterInfo, nSize);
	g_CharacterInfoMem.Init(nSize, pCharacterInfo);
	if( pCharacterInfo ) { delete[] pCharacterInfo; pCharacterInfo = nullptr; }

	_sntprintf_s(tszTargetFile, _countof(tszTargetFile), _TRUNCATE, _T("%s%s"), AVATARITEM_AVATARINFO_FILEPATH, _T("iteminfo.txt"));

	std::vector<CItemInfoList> rows;
	if( !ReadItemImageInfoList(tszTargetFile, rows) )
	{
		LOG_ERROR(_T(" # iteminfo.txt Load Fail #, %s "), tszTargetFile);
		return FALSE;
	}

	// 같은 아이템 번호 행(레이어)의 상대 순서를 시퀀스 기준으로 맞춘다.
	// stable_sort 이므로 시퀀스가 같은 행은 파일 순서가 유지된다.
	std::stable_sort(rows.begin(), rows.end(), [](const CItemInfoList& a, const CItemInfoList& b) {
		return a.m_ItemImageInfo.m_nItemImageSeq < b.m_ItemImageInfo.m_nItemImageSeq;
		});

	g_AvatarBgInfoListMem.Init(std::vector<CItemInfoList>(rows));
	g_ItemInfoListMem.Init(std::move(rows));

	_sntprintf_s(tszTargetFile, _countof(tszTargetFile), _TRUNCATE, _T("%s%s"), AVATARITEM_AVATARINFO_FILEPATH, _T("expressioninfo.txt"));
	ReadExpressionInfo(tszTargetFile, pExpressionInfo, nSize);
	g_ExpressionInfoMem.Init(nSize, pExpressionInfo);
	if( pExpressionInfo ) { delete[] pExpressionInfo; pExpressionInfo = nullptr; }

	m_nAvatar = 1;

	LV_COLUMN lvcolumn;

	const TCHAR* ptszText01[] = { _T("캐릭터 리스트") };

	lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
	lvcolumn.fmt = LVCFMT_RIGHT;
	lvcolumn.pszText = const_cast<LPTSTR>(ptszText01[0]);
	lvcolumn.iSubItem = 0;
	lvcolumn.cx = 136;
	m_ctrlAvatarList.InsertColumn(0, &lvcolumn);

	const TCHAR* ptszText02[] = { _T("아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText02[i]);
		lvcolumn.iSubItem = i;

		if( i == 0 ) lvcolumn.cx = 163;
		else if( i == 1 ) lvcolumn.cx = 55;

		m_ctrlItemList.InsertColumn(i, &lvcolumn);
	}

	const TCHAR* ptszText03[] = { _T("캐릭터 기본 아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText03[i]);
		lvcolumn.iSubItem = i;

		if( i == 0 ) lvcolumn.cx = 163;
		else if( i == 1 ) lvcolumn.cx = 55;

		m_ctrlCharacterGeneralItemList.InsertColumn(i, &lvcolumn);
	}

	const TCHAR* ptszText04[] = { _T("아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText04[i]);
		lvcolumn.iSubItem = i;

		if( i == 0 ) lvcolumn.cx = 163;
		else if( i == 1 ) lvcolumn.cx = 55;

		m_ctrlItemDrawList.InsertColumn(i, &lvcolumn);
	}

	struct { CListCtrl* pList; CColorHeaderCtrl* pHdr; UINT nID; } hdrs[] =
	{
		{ &m_ctrlAvatarList,				&m_hdrAvatar,		IDC_AVATAR_LIST },
		{ &m_ctrlItemList,					&m_hdrItem,			IDC_ITEM_LIST },
		{ &m_ctrlItemDrawList,				&m_hdrItemDraw,		IDC_ITEMDRAW_LIST },
		{ &m_ctrlCharacterGeneralItemList,	&m_hdrCharGeneral,	IDC_CHAR_GENERAL_ITEM_LIST },
	};

	for( auto& h : hdrs )
	{
		CHeaderCtrl* pRaw = h.pList->GetHeaderCtrl();
		if( pRaw == nullptr ) continue;

		h.pHdr->SubclassWindow(pRaw->GetSafeHwnd());
		h.pHdr->SetColors(RGB(208, 214, 226), RGB(30, 40, 60));
		h.pHdr->Invalidate();
	}

	m_pItemImageCreateDlg = new(std::nothrow) CItemImageCreateDlg;
	if( m_pItemImageCreateDlg && !m_pItemImageCreateDlg->Create(m_hWnd, IDD_IMAGEFILECREATE_DIALOG) )
	{
		delete m_pItemImageCreateDlg;
		m_pItemImageCreateDlg = nullptr;
	}

	GenderListInit();
	CharacterListInit();
	SubTypeListInit();
	ItemListInit();
	ExprListInit();
	BgListInit();
	DrawStyleListInit();
	CharacterDrawGeneralItemList();
	OnLeftDraw();

	m_rcBody.SetRect(440, 56, 440 + AVATARITEM_IMAGE_SIZE_WIDTH, 56 + AVATARITEM_IMAGE_SIZE_HEIGHT);

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CAvatarImageToolDlg::OnDestroy()
{
	KillTimer(IDT_GIF_ANIM);

	delete m_pItemImageCreateDlg;
	m_pItemImageCreateDlg = nullptr;

	CDialogEx::OnDestroy();
}

void CAvatarImageToolDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CDialogEx::OnSysCommand(nID, lParam);
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CAvatarImageToolDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CPaintDC dc(this);		// 반드시 CPaintDC (갱신 영역 검증)

		try
		{
			ImageProcessor xImage;
			xImage.Load("Image/wallpaper.png");
			Win32Draw::DrawToDC(dc.GetSafeHdc(), xImage.Buffer(), 0, 0, 820, 600);
		}
		catch( const ImageException& ) {}

		try
		{
			ImageProcessor Title;
			Title.Load("Image/Title.png");
			Win32Draw::DrawToDC(dc.GetSafeHdc(), Title.Buffer(), 0, 0, 820, 50);
		}
		catch( const ImageException& ) {}

		DrawTitleButtons(dc);

		ImageDraw(AVATARITEM_IMAGE_ABOVE_LAYER);
		ImageDraw(AVATARITEM_IMAGE_EXPR_LAYER);
		ImageDraw(AVATARITEM_IMAGE_UNDER_LAYER);
		DrawBody(dc);
	}
}

//***************************************************************************
// GIF 프레임 진행 (프레임마다 지연시간이 달라 타이머를 매번 다시 건다)
void CAvatarImageToolDlg::OnTimer(UINT_PTR nIDEvent)
{
	if( nIDEvent == IDT_GIF_ANIM && m_gifBg.IsAnimated() )
	{
		m_gifBg.Advance();
		if( !IsIconic() ) { CClientDC dc(this); DrawBody(dc); }
		SetTimer(IDT_GIF_ANIM, m_gifBg.CurrentDelayMs(), nullptr);
		return;
	}
	CDialogEx::OnTimer(nIDEvent);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnCustomdrawListSelect(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);
	*pResult = CDRF_DODEFAULT;

	switch( pCD->nmcd.dwDrawStage )
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;

	case CDDS_ITEMPREPAINT:
		*pResult = CDRF_NOTIFYSUBITEMDRAW;
		break;

	case (CDDS_ITEMPREPAINT | CDDS_SUBITEM):
	{
		CListCtrl* pList = static_cast<CListCtrl*>(GetDlgItem(static_cast<int>(pNMHDR->idFrom)));
		if( pList == nullptr ) break;

		if( pList->GetItemState(static_cast<int>(pCD->nmcd.dwItemSpec), LVIS_SELECTED) & LVIS_SELECTED )
		{
			pCD->clrText = RGB(255, 255, 255);
			pCD->clrTextBk = GetListColor(pNMHDR->idFrom);
			pCD->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);	// 시스템 선택 색/포커스 점선 제거
		}
		break;
	}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::ImageDraw(int nCheck)
{
	int nBufferSize = 0;
	int nX = 50, nY = 56;
	int nWidth = 120, nHeight = 170;

	BYTE* pBuffer = NULL;

	if( nCheck == AVATARITEM_IMAGE_ABOVE_LAYER )
	{
		nX = 20;
		pBuffer = m_bAboveBuffer;
		nBufferSize = m_nAboveBufferSize;
	}
	else if( nCheck == AVATARITEM_IMAGE_EXPR_LAYER )
	{
		nX = 20 + nWidth + 20;
		pBuffer = m_bExprBuffer;
		nBufferSize = m_nExprBufferSize;
	}
	else if( nCheck == AVATARITEM_IMAGE_UNDER_LAYER )
	{
		nX = 20 + nWidth * 2 + 40;
		pBuffer = m_bUnderBuffer;
		nBufferSize = m_nUnderBufferSize;
	}

	if( pBuffer == nullptr || nBufferSize <= 0 ) return;	// 기존 CxImage는 빈 버퍼를 조용히 무시

	CClientDC dc(this);

	try
	{
		ImageProcessor xImage;
		xImage.LoadFromMemory(pBuffer, static_cast<size_t>(nBufferSize));
		Win32Draw::DrawToDCAlpha(dc.GetSafeHdc(), xImage.Buffer(), nX, nY);	// 원본 크기 (= GetWidth/GetHeight)
	}
	catch( const ImageException& )
	{
		// TODO: 디코딩 실패 처리
	}
}

//***************************************************************************
// 전신 아바타(m_bBuffer) 디코드 → m_imgAvatar. 실패하면 아바타 없이 배경만 그린다.
void CAvatarImageToolDlg::SetBodyAvatar(const BYTE* pBuf, int nSize)
{
	m_bAvatarReady = false;
	if( pBuf == nullptr || nSize <= 0 ) return;

	try
	{
		m_imgAvatar.LoadFromMemory(pBuf, static_cast<size_t>(nSize));
		m_bAvatarReady = true;
	}
	catch( const ImageException& ) {}
}

//***************************************************************************
// 전신 영역 그리기: 메모리 DC에 배경(회색 → GIF 현재 프레임 → 아바타)을 합성한 뒤 한 번에 BitBlt (깜빡임 방지)
void CAvatarImageToolDlg::DrawBody(CDC& dc)
{
	const int w = m_rcBody.Width();
	const int h = m_rcBody.Height();
	if( w <= 0 || h <= 0 ) return;

	const bool bHasBg = !m_gifBg.Empty();
	if( !bHasBg && !m_bAvatarReady ) return;		// 그릴 것이 없으면 월페이퍼를 그대로 둔다

	CDC mem;
	CBitmap bmp;
	if( !mem.CreateCompatibleDC(&dc) ) return;
	if( !bmp.CreateCompatibleBitmap(&dc, w, h) ) return;
	CBitmap* pOldBmp = mem.SelectObject(&bmp);

	if( bHasBg )
	{
		mem.FillSolidRect(0, 0, w, h, RGB(172, 172, 172));		// GIF 투명 픽셀 아래 기본색
		Win32Draw::DrawToDCAlpha(mem.GetSafeHdc(), m_gifBg.Current(), 0, 0, w, h);
	}
	else
	{
		// 배경 없음: OnPaint에서 이미 그려진 월페이퍼를 바탕으로 사용 (알파 합성이 월페이퍼 위로 비친다)
		mem.BitBlt(0, 0, w, h, &dc, m_rcBody.left, m_rcBody.top, SRCCOPY);
	}

	if( m_bAvatarReady )
	{
		const int aw = static_cast<int>(m_imgAvatar.Buffer().Width());
		const int ah = static_cast<int>(m_imgAvatar.Buffer().Height());
		Win32Draw::DrawToDCAlpha(mem.GetSafeHdc(), m_imgAvatar.Buffer(), (w - aw) / 2, h - ah);
	}

	dc.BitBlt(m_rcBody.left, m_rcBody.top, w, h, &mem, 0, 0, SRCCOPY);
	mem.SelectObject(pOldBmp);
}

//***************************************************************************
//
void CAvatarImageToolDlg::DrawTitleButtons(CDC& dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	const int nBtn = 16, nGap = 4, nTop = 6;
	int x = rcClient.right - 5 - nBtn;

	m_rcClose.SetRect(x, nTop, x + nBtn, nTop + nBtn);
	x -= nBtn + nGap;
	m_rcMin.SetRect(x, nTop, x + nBtn, nTop + nBtn);

	UINT uClose = DFCS_CAPTIONCLOSE | (m_ButtonDown == DHT_CLOSE && m_LastHit == DHT_CLOSE ? DFCS_PUSHED : 0);
	UINT uMin = DFCS_CAPTIONMIN | (m_ButtonDown == DHT_MIN && m_LastHit == DHT_MIN ? DFCS_PUSHED : 0);

	dc.DrawFrameControl(m_rcClose, DFC_CAPTION, uClose);
	dc.DrawFrameControl(m_rcMin, DFC_CAPTION, uMin);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if( m_ButtonDown != DHT_NONE )
	{
		// 누른 채 버튼 밖으로 나가면 눌림 해제, 다시 들어오면 눌림
		DWORD hit = HitTest(point);
		if( hit != m_LastHit )
		{
			m_LastHit = hit;
			InvalidateRect(m_ButtonDown == DHT_CLOSE ? m_rcClose : m_rcMin, FALSE);
		}
	}
	CDialogEx::OnMouseMove(nFlags, point);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if( m_ButtonDown == DHT_NONE )
	{
		CDialogEx::OnLButtonUp(nFlags, point);
		return;
	}

	DWORD pressed = m_ButtonDown;
	DWORD hit = HitTest(point);

	m_ButtonDown = DHT_NONE;
	m_LastHit = DHT_NONE;
	ReleaseCapture();
	Invalidate(FALSE);

	if( hit != pressed ) return;		// 누른 버튼 위에서 뗐을 때만 동작

	if( pressed == DHT_CLOSE ) OnCancel();
	else if( pressed == DHT_MIN ) ShowWindow(SW_MINIMIZE);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	DWORD hit = HitTest(point);

	switch( hit )
	{
	case DHT_CLOSE:
	case DHT_MIN:
		m_ButtonDown = hit;
		m_LastHit = hit;
		SetCapture();
		InvalidateRect(hit == DHT_CLOSE ? m_rcClose : m_rcMin, FALSE);
		return;

	case DHT_CAPTION:
		ReleaseCapture();
		SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);	// 창 이동
		return;
	}

	CDialogEx::OnLButtonDown(nFlags, point);
}

//***************************************************************************
//
DWORD CAvatarImageToolDlg::HitTest(CPoint point)
{
	// Check for buttons
	CRect rect = m_rcClose;

	if( rect.PtInRect(point) )
		return (DWORD)DHT_CLOSE;
	else
	{
		rect = m_rcMin;
		if( rect.PtInRect(point) )
			return (DWORD)DHT_MIN;

		return (DWORD)DHT_CAPTION;
	}
}

//***************************************************************************
// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CAvatarImageToolDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//***************************************************************************
//
void CAvatarImageToolDlg::GenderListInit()
{
	const TCHAR* ptszSubType[] =
	{
		_T("남자"), _T("여자")
	};

	for( int i = 0; i < 2; i++ )
		m_ctrlGenderList.AddString(ptszSubType[i], i);

	m_ctrlGenderList.SelectItemData(m_nGender);
	OnSelchangeGender();
	InvalidateRect(NULL);
}

//***************************************************************************
//
void CAvatarImageToolDlg::CharacterListInit()
{
	CCharacterInfo	CharacterInfo;

	m_ctrlAvatarList.DeleteAllItems();
	if( m_bManAvatar )
	{
		for( int i = 0; i < g_CharacterInfoMem.GetCount(); i++ )
		{
			memset(&CharacterInfo.m_CharacterInfo, 0, sizeof(AVATAR_IMAGE_CHARACTER_INFO));
			if( g_CharacterInfoMem.Search(i + 1, CharacterInfo) > -1 )
			{
				if( CharacterInfo.m_CharacterInfo.m_tszGender[0] == _T('M') )
					m_ctrlAvatarList.InsertItem(i, CharacterInfo.m_CharacterInfo.m_tszChrName);
			}
		}
	}
	else
	{
		for( int i = 0; i < g_CharacterInfoMem.GetCount(); i++ )
		{
			memset(&CharacterInfo.m_CharacterInfo, 0, sizeof(AVATAR_IMAGE_CHARACTER_INFO));
			if( g_CharacterInfoMem.Search(i + 1, CharacterInfo) > -1 )
			{
				if( CharacterInfo.m_CharacterInfo.m_tszGender[0] == _T('F') )
					m_ctrlAvatarList.InsertItem(i, CharacterInfo.m_CharacterInfo.m_tszChrName);
			}
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::DrawStyleListInit()
{
	const TCHAR* ptszDirection[] =
	{
		_T("전신상"), _T("상반신"), _T("머리"), _T("줄인머리"), _T("아이콘머리")
	};

	for( int i = 0; i < 5; i++ )
		m_ctrlDrawStyleList.AddString(ptszDirection[i], i);

	m_ctrlDrawStyleList.SelectItemData(0);
}

//***************************************************************************
//
void CAvatarImageToolDlg::ItemListInit()
{
	int		nCuritem = 0;
	BOOL	bInsertCheck = false;
	TCHAR	tszIndex[8];

	LV_ITEM lvitem;

	CItemInfoList	ItemInfoList;

	m_ctrlItemList.DeleteAllItems();
	for( int i = 0; i < g_ItemInfoListMem.GetCount(); i++ )
	{
		memset(&ItemInfoList, 0, sizeof(ItemInfoList));

		g_ItemInfoListMem.Search(i, ItemInfoList);

		if( m_nSubType == 0 )
		{
			if( ItemInfoList.m_ItemImageInfo.m_nItemNumber > 534 )
			{
				if( m_bManAvatar )
				{
					if( ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('M') )
						bInsertCheck = true;
					else bInsertCheck = false;
				}
				else if( m_bWomanAvatar )
				{
					if( ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('F') )
						bInsertCheck = true;
					else bInsertCheck = false;
				}

				if( bInsertCheck )
				{
					for( int j = 0; j < 2; j++ )
					{
						lvitem.mask = LVIF_TEXT;
						lvitem.iItem = (j == 0) ? i : nCuritem;
						lvitem.iSubItem = j;

						switch( j )
						{
						case 0:
							lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
							nCuritem = m_ctrlItemList.InsertItem(&lvitem);
							break;

						case 1:
							_sntprintf_s(tszIndex, _countof(tszIndex), _TRUNCATE, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
							lvitem.pszText = tszIndex;
							m_ctrlItemList.SetItem(&lvitem);
							break;
						}
					}
				}
			}
		}
		else
		{
			if( ItemInfoList.m_ItemImageInfo.m_nSubType == m_nSubType )
			{
				if( ItemInfoList.m_ItemImageInfo.m_nItemNumber > 534 )
				{
					if( m_bManAvatar )
					{
						if( ItemInfoList.m_ItemImageInfo.m_tszGender[0] == 'M' )
							bInsertCheck = true;
						else bInsertCheck = false;
					}
					else if( m_bWomanAvatar )
					{
						if( ItemInfoList.m_ItemImageInfo.m_tszGender[0] == 'F' )
							bInsertCheck = true;
						else bInsertCheck = false;
					}

					if( bInsertCheck )
					{
						for( int j = 0; j < 2; j++ )
						{
							lvitem.mask = LVIF_TEXT;
							lvitem.iItem = (j == 0) ? i : nCuritem;
							lvitem.iSubItem = j;

							switch( j )
							{
							case 0:
								lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
								nCuritem = m_ctrlItemList.InsertItem(&lvitem);
								break;

							case 1:
								_sntprintf_s(tszIndex, _countof(tszIndex), _TRUNCATE, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
								lvitem.pszText = tszIndex;
								m_ctrlItemList.SetItem(&lvitem);
								break;
							}
						}
					}
				}
			}
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::CharacterDrawGeneralItemList()
{
	CCharacterInfo	CharacterInfo;

	m_ctrlCharacterGeneralItemList.DeleteAllItems();
	if( g_CharacterInfoMem.Search(m_nAvatar, CharacterInfo) > -1 )
	{
		int nItemNumber = CharacterInfo.m_CharacterInfo.m_nBodyItemNum;
		CharacterDrawGeneralItemListUpdate(nItemNumber);

		nItemNumber = CharacterInfo.m_CharacterInfo.m_nHairItemNum;
		CharacterDrawGeneralItemListUpdate(nItemNumber);

		nItemNumber = CharacterInfo.m_CharacterInfo.m_nJacketItemNum;
		CharacterDrawGeneralItemListUpdate(nItemNumber);

		nItemNumber = CharacterInfo.m_CharacterInfo.m_nPantsItemNum;
		CharacterDrawGeneralItemListUpdate(nItemNumber);
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::SubTypeListInit()
{
	const TCHAR* ptszSubType[] =
	{
		_T("전체타입"), _T("몸"), _T("표정"), _T("가발"), _T("모자"), _T("상의"), _T("하의"), _T("신발"), _T("무기"), _T("장식")
	};

	for( int i = 0; i < 10; i++ )
		m_ctrlSubTypeList.AddString(ptszSubType[i], i);

	m_ctrlSubTypeList.SelectItemData(m_nSubType);
	OnSelchangeSubtype();
}

//***************************************************************************
//
void CAvatarImageToolDlg::ExprListInit()
{
	const TCHAR* ptszExpression[] =
	{
		_T("----- 표정 -----"),
		_T("하하1"), _T("하하2"), _T("하하3"), _T("하하4"), _T("하하5"), _T("하하6"),
		_T("우씨1"), _T("우씨2"), _T("우씨3"), _T("우씨4"), _T("우씨5"), _T("우씨6"),
		_T("으앙1"), _T("으앙2"), _T("으앙3"), _T("으앙4"), _T("으앙5"), _T("으앙6"),
		_T("좋아1"), _T("좋아2"), _T("좋아3"), _T("좋아4"), _T("좋아5"), _T("좋아6"),
		_T("헉1"), _T("헉2"), _T("헉3"), _T("헉4"), _T("헉5"), _T("헉6"),
		_T("기타1"), _T("기타2"), _T("기타3"), _T("기타4"), _T("기타5"), _T("기타6")
	};

	for( int i = 0; i < AVATARITEM_EXPRESSION_IMAGE_NUM + 1; i++ )
		m_ctrlExprList.AddString(ptszExpression[i], i);

	m_ctrlExprList.SelectItemData(0);
}

//***************************************************************************
//
void CAvatarImageToolDlg::BgListInit()
{
	m_ctrlAvatarBgList.AddString(_T("-------- 배경 --------"), 0);

	std::vector<CItemInfoList> bgList;
	g_AvatarBgInfoListMem.GetAll(bgList);

	std::set<int> seen;
	for( const CItemInfoList& item : bgList )
	{
		const int nNo = item.m_ItemImageInfo.m_nItemNumber;
		if( seen.insert(nNo).second )
			m_ctrlAvatarBgList.AddString(item.m_ItemImageInfo.m_tszName, nNo);
	}
	m_ctrlAvatarBgList.SelectItemData(0);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeGender()
{
	// TODO: Add your control notification handler code here
	m_nGender = static_cast<int>(m_ctrlGenderList.GetCurSelData());
	m_nExpression = 0;

	if( m_nGender == 0 )
	{
		m_bManAvatar = true;
		m_bWomanAvatar = false;
		m_nAvatar = 1;
	}
	else if( m_nGender == 1 )
	{
		m_bManAvatar = false;
		m_bWomanAvatar = true;
		m_nAvatar = 9;
	}

	CharacterListInit();
	ItemListInit();
	CharacterDrawGeneralItemList();
	OnLeftDraw();
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeSubtype()
{
	// TODO: Add your control notification handler code here
	m_nSubType = static_cast<int>(m_ctrlSubTypeList.GetCurSelData());

	ItemListInit();
	InvalidateRect(NULL);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeExpression()
{
	// TODO: Add your control notification handler code here
	int		nItemNumber = 0;
	int		nIndex = 0;

	nIndex = static_cast<int>(m_ctrlExprList.GetCurSelData());

	if( nIndex > 0 )
	{
		m_nExpression = nIndex - 1;
		OnLeftDraw();
	}
	else m_nExpression = 0;
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeAvatarBg()
{
	// TODO: Add your control notification handler code here
	m_nAvatarBg = static_cast<int>(m_ctrlAvatarBgList.GetCurSelData());	// 0 = 없음, 그 외 아이템 번호
	if( m_nAvatarBg > 0 ) StartGifBackground(m_nAvatarBg);
	else StopGifBackground();
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeDrawstyle()
{
	// TODO: Add your control notification handler code here
	m_nDrawStyle = static_cast<int>(m_ctrlDrawStyleList.GetCurSelData());
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnDblclkAvatarList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// TODO: Add your control notification handler code here
	int		nIndex = -1;

	CCharacterInfo			CharacterInfo;

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if( pNMListView->iItem == -1 ) // subitem이 선택되었을 경우 
	{
		// 좌표를 변경하여 item 변호를 얻는다. 
		pNMListView->ptAction.x = 2;
		nIndex = m_ctrlAvatarList.HitTest(pNMListView->ptAction, NULL);
	}
	else nIndex = pNMListView->iItem;

	if( m_bManAvatar )
	{
		if( nIndex == 8 ) m_nAvatar = 17;
		else m_nAvatar = nIndex + 1;
	}
	else if( m_bWomanAvatar ) m_nAvatar = m_ctrlAvatarList.GetItemCount() + nIndex + 1;

	CharacterDrawGeneralItemList();
	OnLeftDraw();

	*pResult = 0;
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnDblclkItemList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// TODO: Add your control notification handler code here
	int		nIndex = -1;
	TCHAR	tszBuffer[8];

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if( pNMListView->iItem == -1 ) // subitem이 선택되었을 경우 
	{
		// 좌표를 변경하여 item 변호를 얻는다. 
		pNMListView->ptAction.x = 2;
		nIndex = m_ctrlItemList.HitTest(pNMListView->ptAction, NULL);
	}
	else nIndex = pNMListView->iItem;

	_sntprintf_s(tszBuffer, _countof(tszBuffer), _TRUNCATE, _T("%s"), (LPCTSTR)m_ctrlItemList.GetItemText(nIndex, 1));
	ItemDrawListUpdate(_ttoi(tszBuffer));

	*pResult = 0;
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnClickItemdrawList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// TODO: Add your control notification handler code here
	int		nIndex = -1;

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if( pNMListView->iItem == -1 ) // subitem이 선택되었을 경우 
	{
		// 좌표를 변경하여 item 변호를 얻는다. 
		pNMListView->ptAction.x = 2;
		nIndex = m_ctrlItemDrawList.HitTest(pNMListView->ptAction, NULL);
	}
	else nIndex = pNMListView->iItem;

	m_nDelIndex = nIndex;

	*pResult = 0;
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnListDelete()
{
	// TODO: Add your control notification handler code here
	m_ctrlItemDrawList.DeleteItem(m_nDelIndex);

	if( m_nDelIndex < 0 ) m_ctrlItemDrawList.DeleteAllItems();
	m_nDelIndex = -1;
}

//***************************************************************************
//
void CAvatarImageToolDlg::CharacterDrawGeneralItemListUpdate(int nItemNumber)
{
	BOOL	bCoverageCheck = true;
	int		nCuritem = 0;
	TCHAR	tszIndex[8];

	LV_ITEM lvitem;

	CItemInfoList	ItemInfoList;

	if( nItemNumber < 1 ) return;

	g_ItemInfoListMem.Search(nItemNumber, ItemInfoList);

	int nCount = m_ctrlCharacterGeneralItemList.GetItemCount();

	for( int j = 0; j < 2; j++ )
	{
		lvitem.mask = LVIF_TEXT;
		lvitem.iItem = (j == 0) ? nCount : nCuritem;
		lvitem.iSubItem = j;

		switch( j )
		{
		case 0:
			lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
			nCuritem = m_ctrlCharacterGeneralItemList.InsertItem(&lvitem);
			break;

		case 1:
			_sntprintf_s(tszIndex, _countof(tszIndex), _TRUNCATE, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
			lvitem.pszText = tszIndex;
			m_ctrlCharacterGeneralItemList.SetItem(&lvitem);
			break;
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::ItemDrawListUpdate(int nItemNumber)
{
	BOOL	bCoverageCheck = true;
	int		nCuritem = 0;
	TCHAR	tszIndex[8];
	TCHAR	tszMessage[MAX_BUFFER_SIZE];

	LV_ITEM lvitem;

	CItemInfoList	ItemInfoList;
	CItemInfoList	TempItemInfoList;

	if( nItemNumber < 1 ) return;

	g_ItemInfoListMem.Search(nItemNumber, ItemInfoList);

	int nCount = m_ctrlItemDrawList.GetItemCount();

	if( nCount >= AVATARITEM_MAX_EQUIPMENTS )
	{
		_sntprintf_s(tszMessage, _countof(tszMessage), _TRUNCATE, _T("%s"), _T("장착 아이템은 32개만 가능합니다."));
		AfxMessageBox(tszMessage);
		return;
	}

	int i = 0;

	for( i = 0; i < nCount; i++ )
	{
		int nTempNumber = _ttoi(m_ctrlItemDrawList.GetItemText(i, 1));
		g_ItemInfoListMem.Search(nTempNumber, TempItemInfoList);
		if( (ItemInfoList.m_ItemImageInfo.m_nCoverageMask & TempItemInfoList.m_ItemImageInfo.m_nCoverageMask) != 0 )
		{
			bCoverageCheck = false;
			break;
		}
	}

	if( !bCoverageCheck ) m_ctrlItemDrawList.DeleteItem(i);

	for( int j = 0; j < 2; j++ )
	{
		lvitem.mask = LVIF_TEXT;
		lvitem.iItem = (j == 0) ? nCount : nCuritem;
		lvitem.iSubItem = j;

		switch( j )
		{
		case 0:
			lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
			nCuritem = m_ctrlItemDrawList.InsertItem(&lvitem);
			break;

		case 1:
			_sntprintf_s(tszIndex, _countof(tszIndex), _TRUNCATE, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
			lvitem.pszText = tszIndex;
			m_ctrlItemDrawList.SetItem(&lvitem);
			break;
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnLeftDraw()
{
	// TODO: Add your control notification handler code here
	DrawingImage(_T("L"));
	InvalidateRect(NULL);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnRightDraw()
{
	// TODO: Add your control notification handler code here
	DrawingImage(_T("R"));
	InvalidateRect(NULL);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnImageFileCreate()
{
	TCHAR tszDefName[64];
	_sntprintf_s(tszDefName, _countof(tszDefName), _TRUNCATE, _T("avatar_%d.png"), m_nAvatar);

	CFileDialog dlg(FALSE, _T("png"), tszDefName, OFN_OVERWRITEPROMPT, _T("PNG (*.png)|*.png||"), this);
	if( dlg.DoModal() == IDOK )
	{
		if( !SaveAvatarPng(dlg.GetPathName()) )
			AfxMessageBox(_T("PNG 저장에 실패했습니다."));
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnImageFileCreatetoolControl()
{
	// TODO: Add your control notification handler code here
	if( m_pItemImageCreateDlg == nullptr ) return;

	if( m_pItemImageCreateDlg->IsWindowVisible() )
		m_pItemImageCreateDlg->ShowWindow(SW_HIDE);
	else m_pItemImageCreateDlg->ShowWindow(SW_SHOW);

	InvalidateRect(NULL);
}

//***************************************************************************
//
BOOL CAvatarImageToolDlg::DrawingImage(const TCHAR* ptszDirection)
{
	int		nCount = 0;
	int		nItemNumber[AVATARITEM_MAX_EQUIPMENTS] = { 0 };

	TCHAR	tszEncString[AVATARITEM_QUERYSTRING_DATA_STR_ENC_LEN];
	TCHAR	tszQueryString[HTTP_QUERYSTRING_STRLEN];

	CDrawImage					DrawImage;
	CImageInfo					ImageInfo;
	AVATAR_DRAWING_ITEM_INFO	DrawItemInfo;

	DrawImage.SetCanvasInfo(AVATARITEM_AVATARINFO_FILEPATH, AVATARITEM_IMAGE_SIZE_WIDTH, AVATARITEM_IMAGE_SIZE_HEIGHT, 24, 8, AVATARITEM_IMAGE_LAYER_TOTAL, 0);

	for( int i = 0; i < m_ctrlItemDrawList.GetItemCount() && nCount < AVATARITEM_MAX_EQUIPMENTS; i++ )
	{
		int nTemp = _ttoi(m_ctrlItemDrawList.GetItemText(i, 1));
		if( nTemp > 534 )
			nItemNumber[nCount++] = nTemp;
	}

	nItemNumber[nCount] = 3377;
	nCount++;

	_tstring strSrc;
	for( int i = 0; i < nCount; i++ )
	{
		if( i > 0 ) strSrc += _T(',');
		strSrc += std::to_wstring(nItemNumber[i]);		// MBCS 빌드면 std::to_string
	}

	_tstring strEnc = Base64Enc(strSrc);

	if( strEnc.length() >= _countof(tszEncString) ) return FALSE;		// 4자리 번호가 섞이면 초과 가능
	_tcsncpy_s(tszEncString, _countof(tszEncString), strEnc.c_str(), _TRUNCATE);
	_tcsncpy_s(m_tszEncString, _countof(m_tszEncString), tszEncString, _TRUNCATE);

	_sntprintf_s(tszQueryString, _countof(tszQueryString), _TRUNCATE, _T("?style=%d&side=%s&char=%d&expr=%d&dress=%s"), m_nDrawStyle, ptszDirection, m_nAvatar, m_nExpression + 1, tszEncString);
	memset(&DrawItemInfo, 0, sizeof(AVATAR_DRAWING_ITEM_INFO));
	GetDrawingInfo(tszQueryString, DrawItemInfo);

	DrawItemInfo.m_nLayerExpr = AVATARITEM_IMAGE_TOTAL_LAYER;
	if( !DrawImage.SetItemInfoMemDB(DrawItemInfo, ImageInfo) ) return FALSE;
	m_nTotalBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy(m_bTotalBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nTotalBufferSize);
	SetBodyAvatar(m_bTotalBuffer, m_nTotalBufferSize);

	DrawItemInfo.m_nLayerExpr = AVATARITEM_IMAGE_ABOVE_LAYER;
	if( !DrawImage.SetItemInfoMemDB(DrawItemInfo, ImageInfo) ) return FALSE;
	m_nAboveBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy(m_bAboveBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nAboveBufferSize);

	DrawItemInfo.m_nLayerExpr = AVATARITEM_IMAGE_EXPR_LAYER;
	if( !DrawImage.SetItemInfoMemDB(DrawItemInfo, ImageInfo) ) return FALSE;
	m_nExprBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy(m_bExprBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nExprBufferSize);

	DrawItemInfo.m_nLayerExpr = AVATARITEM_IMAGE_UNDER_LAYER;
	if( !DrawImage.SetItemInfoMemDB(DrawItemInfo, ImageInfo) ) return FALSE;
	m_nUnderBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy(m_bUnderBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nUnderBufferSize);

	return TRUE;
}

//*************************************************************************
// http://avatar-image.test.com/render-body?style=0&side=L&char=6&expr=1&dress=NIo26OdDrZJEpmqXwGnYgVg
//
BOOL CAvatarImageToolDlg::GetDrawingInfo(const TCHAR* ptszImageInfo, AVATAR_DRAWING_ITEM_INFO& DrawItemInfo)
{
	if( ptszImageInfo == nullptr ) return FALSE;

	TCHAR	tszDrawType[AVATARITEM_QUERYSTRING_DRAW_TYPE_STR_LEN] = { 0 };
	TCHAR	tszDirection[AVATARITEM_QUERYSTRING_DIRECTION_STR_LEN] = { 0 };
	TCHAR	tszCharacter[AVATARITEM_QUERYSTRING_CHARACTER_STR_LEN] = { 0 };
	TCHAR	tszExpr[AVATARITEM_QUERYSTRING_EXPRESSION_STR_LEN] = { 0 };
	TCHAR	tszData[AVATARITEM_QUERYSTRING_DATA_STR_ENC_LEN] = { 0 };

	// 1) 쿼리 문자열 분리 ('?' 뒤, '#' 앞)
	const TCHAR* ptszQuery = _tcschr(ptszImageInfo, _T('?'));
	if( ptszQuery == nullptr ) return FALSE;

	_tstring strQuery(ptszQuery + 1);
	size_t nFrag = strQuery.find(_T('#'));
	if( nFrag != _tstring::npos ) strQuery.erase(nFrag);

	// 2) 항목 추출 (5개 모두 필수)
	if( !GetQueryValue(strQuery.c_str(), AVATARITEM_QUERYSTRING_DRAW_TYPE_TOKEN, tszDrawType, _countof(tszDrawType)) ) return FALSE;
	if( !GetQueryValue(strQuery.c_str(), AVATARITEM_QUERYSTRING_DIRECTION_TOKEN, tszDirection, _countof(tszDirection)) ) return FALSE;
	if( !GetQueryValue(strQuery.c_str(), AVATARITEM_QUERYSTRING_CHARACTER_TOKEN, tszCharacter, _countof(tszCharacter)) ) return FALSE;
	if( !GetQueryValue(strQuery.c_str(), AVATARITEM_QUERYSTRING_EXPRESSION_TOKEN, tszExpr, _countof(tszExpr)) ) return FALSE;
	if( !GetQueryValue(strQuery.c_str(), AVATARITEM_QUERYSTRING_DRESS_TOKEN, tszData, _countof(tszData)) ) return FALSE;

	// 3) 값 검증
	if( tszDirection[0] != _T('L') && tszDirection[0] != _T('R') ) return FALSE;

	TCHAR* pEnd = nullptr;
	const long nDrawType = _tcstol(tszDrawType, &pEnd, 10);  if( *pEnd != _T('\0') ) return FALSE;
	const long nCharacter = _tcstol(tszCharacter, &pEnd, 10);  if( *pEnd != _T('\0') ) return FALSE;
	const long nExpr = _tcstol(tszExpr, &pEnd, 10);  if( *pEnd != _T('\0') ) return FALSE;

	if( nDrawType < 0 || nDrawType > 4 ) return FALSE;
	if( nCharacter < 1 ) return FALSE;
	if( nExpr < 0 || nExpr > AVATARITEM_EXPRESSION_IMAGE_NUM ) return FALSE;

	// 4) dress Base64 복호화 (URL-safe 표기 보정: -,_ -> +,/ 및 패딩)
	_tstring strEnc = DecodeURIComponent(tszData);		// %2B -> '+' (원문 '+'는 공백이 되므로 인코딩 쪽에서 반드시 %2B로 보낼 것)
	while( strEnc.length() % 4 != 0 ) strEnc += _T('=');

	Base64DecodeResult res = Base64DecodeResult::Success;
	_tstring strDec = Base64Dec(strEnc, &res);
	if( res != Base64DecodeResult::Success || strDec.empty() ) return FALSE;
	if( strDec.length() > AVATARITEM_QUERYSTRING_DATA_STR_DEC_LEN ) return FALSE;

	// "535,540,600" 파싱
	int nItemNumber[AVATARITEM_MAX_EQUIPMENTS] = { 0 };
	int nCount = 0;

	size_t pos = 0;
	while( pos <= strDec.length() && nCount < AVATARITEM_MAX_EQUIPMENTS )
	{
		size_t end = strDec.find(_T(','), pos);
		if( end == _tstring::npos ) end = strDec.length();

		_tstring tok = strDec.substr(pos, end - pos);
		if( tok.empty() ) return FALSE;

		TCHAR* pEndTok = nullptr;
		long n = _tcstol(tok.c_str(), &pEndTok, 10);
		if( *pEndTok != _T('\0') || n <= 534 ) return FALSE;

		nItemNumber[nCount++] = static_cast<int>(n);
		pos = end + 1;
	}

	if( _tcslen(ptszImageInfo) >= REQUEST_URL_STRLEN ) return FALSE;	// URL이 버퍼보다 길면 거부

	memset(&DrawItemInfo, 0, sizeof(DrawItemInfo));

	DrawItemInfo.m_nDrawType = static_cast<int>(nDrawType);
	DrawItemInfo.m_nCharacter = static_cast<int>(nCharacter);
	DrawItemInfo.m_nExpression = static_cast<int>(nExpr);
	DrawItemInfo.m_tszDirection[0] = tszDirection[0];
	DrawItemInfo.m_tszDirection[1] = _T('\0');

	memcpy(DrawItemInfo.m_nItemNumber, nItemNumber, sizeof(nItemNumber));
	DrawItemInfo.m_nItemCount = nCount;

	_tcsncpy_s(DrawItemInfo.m_tszRequestImageURL, _countof(DrawItemInfo.m_tszRequestImageURL), ptszImageInfo, _TRUNCATE);

	return TRUE;
}

void CAvatarImageToolDlg::StopGifBackground()
{
	KillTimer(IDT_GIF_ANIM);
	m_gifBg.Clear();
	InvalidateRect(m_rcBody, FALSE);
}

void CAvatarImageToolDlg::StartGifBackground(int nItemNumber)
{
	StopGifBackground();

	CItemInfoList info;
	if( g_AvatarBgInfoListMem.Search(nItemNumber, info) < 0 ) return;

	TCHAR tszPath[FULLPATH_STRLEN];
	_sntprintf_s(tszPath, _countof(tszPath), _TRUNCATE, _T("%s%s"), AVATARITEM_AVATARINFO_BG_FILEPATH, info.m_ItemImageInfo.m_tszSprFileName);

	if( !m_gifBg.LoadFile(tszPath, m_rcBody.Width(), m_rcBody.Height()) ) return;

	if( m_gifBg.IsAnimated() )
		SetTimer(IDT_GIF_ANIM, m_gifBg.CurrentDelayMs(), nullptr);
	InvalidateRect(m_rcBody, FALSE);
}

//***************************************************************************
// 전신 아바타(m_bTotalBuffer)만 PNG로 저장. 배경 GIF/월페이퍼는 포함되지 않는다.
BOOL CAvatarImageToolDlg::SaveAvatarPng(const TCHAR* ptszPath)
{
	if( ptszPath == nullptr ) return FALSE;
	if( m_nTotalBufferSize <= 0 || m_nTotalBufferSize > static_cast<int>(sizeof(m_bTotalBuffer)) ) return FALSE;

	const uint8_t* pData = m_bTotalBuffer;
	size_t nSize = static_cast<size_t>(m_nTotalBufferSize);

	std::vector<uint8_t> reencoded;
	static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

	if( nSize < sizeof(kPngSig) || memcmp(pData, kPngSig, sizeof(kPngSig)) != 0 )
	{
		// PNG가 아니면 디코드 후 PNG로 다시 인코딩
		try
		{
			ImageProcessor img;
			img.LoadFromMemory(pData, nSize);

			PngCodec codec;
			reencoded = codec.Encode(img.Buffer());
		}
		catch( const std::exception& )
		{
			return FALSE;
		}
		if( reencoded.empty() ) return FALSE;

		pData = reencoded.data();
		nSize = reencoded.size();
	}

	std::ofstream fs(std::filesystem::path(ptszPath), std::ios::binary);
	if( !fs ) return FALSE;

	fs.write(reinterpret_cast<const char*>(pData), static_cast<std::streamsize>(nSize));
	return fs.good() ? TRUE : FALSE;
}