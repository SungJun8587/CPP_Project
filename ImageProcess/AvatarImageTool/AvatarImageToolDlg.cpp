
// AvatarImageToolDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "AvatarImageTool.h"
#include "AvatarImageToolDlg.h"
#include "afxdialogex.h"
#include "AvatarInfoMemDBEx.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CCharacterInfoMemDBEx   g_CharacterInfoMem;
CItemInfoListMemDBEx	g_ItemInfoListMem;
CExpressionInfoMemDBEx	g_ExpressionInfoMem;

// CAvatarImageToolDlg 대화 상자

CAvatarImageToolDlg::CAvatarImageToolDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_AVATARIMAGETOOL_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
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

	ON_NOTIFY(NM_DBLCLK, IDC_ITEM_LIST, OnDblclkItemList)
	ON_CBN_SELCHANGE(IDC_SUBTYPE, OnSelchangeSubtype)
	ON_CBN_SELCHANGE(IDC_GENDER, OnSelchangeGender)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_BN_CLICKED(IDC_LIST_DELETE, OnListDelete)
	ON_NOTIFY(NM_CLICK, IDC_ITEMDRAW_LIST, OnClickItemdrawList)
	ON_CBN_SELCHANGE(IDC_EXPRESSION, OnSelchangeExpression)
	ON_BN_CLICKED(IDC_LEFT_DRAW, OnLeftDraw)
	ON_BN_CLICKED(IDC_RIGHT_DRAW, OnRightDraw)
	ON_CBN_SELCHANGE(IDC_DRAWSTYLE, OnSelchangeDrawstyle)
	//ON_BN_CLICKED(IDC_IMAGE_FILE_CREATETOOL_CONTROL, OnImageFileCreatetoolControl)
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
	ReadItemImageInfoList(tszTargetFile, pItemInfoList, nSize);
	if( pItemInfoList && nSize > 0 )
	{
		// std::sort 등으로 정렬 안전성 확보 필요
		std::sort(pItemInfoList, pItemInfoList + nSize, [](const CItemInfoList& a, const CItemInfoList& b) {
			return a.m_ItemImageInfo.m_nItemImageSeq < b.m_ItemImageInfo.m_nItemImageSeq; // 실제 멤버 변수명으로 변경
			});
	}
	g_ItemInfoListMem.Init(nSize, pItemInfoList);
	if( pItemInfoList ) { delete[] pItemInfoList; pItemInfoList = nullptr; }

	_sntprintf_s(tszTargetFile, _countof(tszTargetFile), _TRUNCATE, _T("%s%s"), AVATARITEM_AVATARINFO_FILEPATH, _T("expressioninfo.txt"));
	ReadExpressionInfo(tszTargetFile, pExpressionInfo, nSize);
	g_ExpressionInfoMem.Init(nSize, pExpressionInfo);
	if( pExpressionInfo ) { delete[] pExpressionInfo; pExpressionInfo = nullptr; }

	m_nAvatar = 1;

	LV_COLUMN lvcolumn;

	const TCHAR* ptszText01[] = { _T("아바타 캐릭터 리스트") };

	lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
	lvcolumn.fmt = LVCFMT_RIGHT;
	lvcolumn.pszText = const_cast<LPTSTR>(ptszText01[0]);
	lvcolumn.iSubItem = 0;
	lvcolumn.cx = 150;
	m_ctrlAvatarList.InsertColumn(0, &lvcolumn);

	const TCHAR* ptszText02[] = { _T("아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText02[0]);
		lvcolumn.iSubItem = i;

		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlItemList.InsertColumn(i, &lvcolumn);
	}

	const TCHAR* ptszText03[] = { _T("캐릭터 기본 아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText03[0]);
		lvcolumn.iSubItem = i;

		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlCharacterGeneralItemList.InsertColumn(i, &lvcolumn);
	}

	const TCHAR* ptszText04[] = { _T("아이템 명"), _T("번호") };

	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = const_cast<LPTSTR>(ptszText04[0]);
		lvcolumn.iSubItem = i;

		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlItemDrawList.InsertColumn(i, &lvcolumn);
	}

	GenderListInit();
	CharacterListInit();
	SubTypeListInit();
	ItemListInit();
	ExprListInit();
	DrawStyleListInit();
	CharacterDrawGeneralItemList();
	OnLeftDraw();

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
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
			Win32Draw::DrawToDC(dc.GetSafeHdc(), xImage.Buffer(), 0, 0, 800, 600);
		}
		catch( const ImageException& ) {}

		try
		{
			ImageProcessor Title;
			Title.Load("Image/Title.png");
			Win32Draw::DrawToDC(dc.GetSafeHdc(), Title.Buffer(), 0, 0, 800, 50);
		}
		catch( const ImageException& ) {}

		DrawTitleButtons(dc);
	}
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
	//Invalidate(FALSE);

	if( hit != pressed ) return;		// 누른 버튼 위에서 뗐을 때만 동작

	if( pressed == DHT_CLOSE )		OnCancel();
	else if( pressed == DHT_MIN )	ShowWindow(SW_MINIMIZE);
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

	m_bManAvatar = true;
	m_bWomanAvatar = false;

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
		_T("표정"),
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
	TCHAR	tszBuffer[32];
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
		m_ctrlItemDrawList.GetItemText(i, 1, tszBuffer, sizeof(tszBuffer));

		g_ItemInfoListMem.Search(_ttoi(tszBuffer), TempItemInfoList);
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
	//DrawingImage(_T("L"));
	InvalidateRect(NULL);
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnRightDraw()
{
	// TODO: Add your control notification handler code here
	//DrawingImage(_T("R"));
	InvalidateRect(NULL);
}