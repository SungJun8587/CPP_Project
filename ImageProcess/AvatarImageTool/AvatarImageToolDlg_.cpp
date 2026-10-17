
//***************************************************************************
// AvatarImageToolDlg.cpp : implementation of the CAvatarImageToolDlg class.
//
//***************************************************************************

#include "stdafx.h"
#include "AvatarImageTool.h"
#include "AvatarImageToolDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void	Swap( int& nSrc, int& nDest );
void	Partition( int A[], int nF, int nL, int& nPivotIndex );
void	Quicksort( int A[], int nF, int nL );

CEventLog				g_SysLog;
CAvatarServerReg		g_AvatarServerReg;
CCharacterInfoMemDBEx   g_CharacterInfoMem;
CItemInfoListMemDBEx	g_ItemInfoListMem;
CExpressionInfoMemDBEx	g_ExpressionInfoMem;

//***************************************************************************
// Construction/Destruction
//***************************************************************************

CAvatarImageToolDlg::CAvatarImageToolDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CAvatarImageToolDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAvatarImageToolDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	
	m_pItemImageCreateDlg = NULL;
	*m_bAboveBuffer	= NULL;
	*m_bExprBuffer = NULL;
	*m_bUnderBuffer = NULL;
	*m_bBuffer = NULL;
	m_nBufSize = 0;
	m_nAboveBufferSize = 0;
	m_nExprBufferSize = 0;
	m_nUnderBufferSize = 0;	
	m_nAvatar = 0;
	m_nSubType = 0;
	m_nGender = 0;
	m_nDrawStyle = 0;
	m_nExpression = 0;
	m_szEncString[0] = '\0';
}

CAvatarImageToolDlg::~CAvatarImageToolDlg()
{
	if( m_pItemImageCreateDlg )
	{
		delete m_pItemImageCreateDlg;
		m_pItemImageCreateDlg = NULL;
	}
}

void CAvatarImageToolDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAvatarImageToolDlg)
	DDX_Control(pDX, IDCANCEL, m_ctrlCancel);
	DDX_Control(pDX, IDC_IMAGE_FILE_CREATETOOL_CONTROL, m_ctrlImageFileCreateTool);
	DDX_Control(pDX, IDC_LIST_DELETE, m_ctrlListDelete);
	DDX_Control(pDX, IDC_RIGHT_DRAW, m_ctrlRightDraw);
	DDX_Control(pDX, IDC_LEFT_DRAW, m_ctrlLeftDraw);
	DDX_Control(pDX, IDC_CHAR_GENERAL_ITEM_LIST, m_ctrlCharacterGeneralItemList);
	DDX_Control(pDX, IDC_ITEMDRAW_LIST, m_ctrlItemDrawList);
	DDX_Control(pDX, IDC_ITEM_LIST, m_ctrlItemList);
	DDX_Control(pDX, IDC_AVATAR_LIST, m_ctrlAvatarList);
	DDX_Control(pDX, IDC_SUBTYPE, m_ctrlSubTypeList);
	DDX_Control(pDX, IDC_GENDER, m_ctrlGenderList);
	DDX_Control(pDX, IDC_EXPRESSION, m_ctrlExprList);
	DDX_Control(pDX, IDC_DRAWSTYLE, m_ctrlDrawStyleList);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAvatarImageToolDlg, CDialog)
	//{{AFX_MSG_MAP(CAvatarImageToolDlg)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_NOTIFY(NM_DBLCLK, IDC_AVATAR_LIST, OnDblclkAvatarList)
	ON_NOTIFY(NM_DBLCLK, IDC_ITEM_LIST, OnDblclkItemList)
	ON_CBN_SELCHANGE(IDC_SUBTYPE, OnSelchangeSubtype)
	ON_CBN_SELCHANGE(IDC_GENDER, OnSelchangeGender)
	ON_WM_NCLBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_BN_CLICKED(IDC_LIST_DELETE, OnListDelete)
	ON_NOTIFY(NM_CLICK, IDC_ITEMDRAW_LIST, OnClickItemdrawList)
	ON_CBN_SELCHANGE(IDC_EXPRESSION, OnSelchangeExpression)
	ON_BN_CLICKED(IDC_LEFT_DRAW, OnLeftDraw)
	ON_BN_CLICKED(IDC_RIGHT_DRAW, OnRightDraw)
	ON_CBN_SELCHANGE(IDC_DRAWSTYLE, OnSelchangeDrawstyle)
	ON_BN_CLICKED(IDC_IMAGE_FILE_CREATETOOL_CONTROL, OnImageFileCreatetoolControl)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

//***************************************************************************
//
BOOL CAvatarImageToolDlg::OnInitDialog()
{
	int			nSize = 0;
	TCHAR		tszSysLogDirectory[ SM_DIRECTORY_LEN ];
	TCHAR		tszTargetFile[ SM_FILEPATH_LEN ];

	CCharacterInfo		*pCharacterInfo = NULL;
	CItemInfoList		*pItemInfoList = NULL;
	CExpressionInfo		*pExpressionInfo = NULL;

	CDialog::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon( m_hIcon, TRUE );			// Set big icon
	SetIcon( m_hIcon, FALSE );		// Set small icon

	// TODO: Add extra initialization here
	g_AvatarServerReg.Initialize();

	_tcscpy( tszSysLogDirectory, g_AvatarServerReg.m_tszSysLogDirectory );

	if( _tcslen( tszSysLogDirectory ) < SM_DIRECTORY_LEN && tszSysLogDirectory[ _tcslen( tszSysLogDirectory ) - 1 ] != '\\' ) 
	{
		_tcscat( tszSysLogDirectory, _T("\\") );
		tszSysLogDirectory[ SM_DIRECTORY_LEN - 1 ] = '\0';
	}

	CreateDirectoryRecursive( tszSysLogDirectory );
	g_SysLog.InitForLogFile( tszSysLogDirectory, SM_4LEAF_AVATAR_SERVER_LOG_FILENAME_PREFIX, EVENTLOG_FMT_DAILY, _T(",") );

	_stprintf( tszTargetFile, _T("%s%s"), g_AvatarServerReg.m_tszDatFilelocation, g_AvatarServerReg.m_tszCharInfoFileName );

	ReadCharacterInfo( tszTargetFile, pCharacterInfo, nSize );
	g_CharacterInfoMem.Init( nSize, pCharacterInfo );
	if( pCharacterInfo ) 
	{	
		delete []pCharacterInfo;
		pCharacterInfo = NULL;
	}

	_stprintf( tszTargetFile, _T("%s%s"), g_AvatarServerReg.m_tszDatFilelocation, g_AvatarServerReg.m_tszItemInfoFileName );

	ReadItemImageInfoList( tszTargetFile, pItemInfoList, nSize );
	Quicksort( pItemInfoList, 0, nSize - 1 );
	g_ItemInfoListMem.Init( nSize, pItemInfoList );
	if( pItemInfoList ) 
	{	
		delete []pItemInfoList;
		pItemInfoList = NULL;
	}

	_stprintf( tszTargetFile, _T("%s%s"), g_AvatarServerReg.m_tszDatFilelocation, g_AvatarServerReg.m_tszExprInfoFileName );

	ReadExpressionInfo( tszTargetFile, pExpressionInfo, nSize );
	g_ExpressionInfoMem.Init( nSize, pExpressionInfo );
	if( pExpressionInfo ) 
	{	
		delete []pExpressionInfo;
		pExpressionInfo = NULL;
	}

	m_nAvatar = 1;
	
	LV_COLUMN lvcolumn;
	
	TCHAR *ptszText01[] = {_T("아바타 캐릭터 리스트")};
	
	lvcolumn.mask = LVCF_FMT|LVCF_SUBITEM|LVCF_TEXT|LVCF_WIDTH;
	lvcolumn.fmt = LVCFMT_RIGHT;
	lvcolumn.pszText = ptszText01[0];
	lvcolumn.iSubItem = 0;
	lvcolumn.cx = 150;
	m_ctrlAvatarList.InsertColumn( 0, &lvcolumn );

	TCHAR *ptszText02[] = {_T("아이템 명"), _T("번호")};
	
	for( int i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT|LVCF_SUBITEM|LVCF_TEXT|LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = ptszText02[i];
		lvcolumn.iSubItem = i;
		
		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlItemList.InsertColumn( i, &lvcolumn );
	}
	
	TCHAR *ptszText03[] = {_T("캐릭터 기본 아이템 명"), _T("번호")};
	
	for( i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT|LVCF_SUBITEM|LVCF_TEXT|LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = ptszText03[i];
		lvcolumn.iSubItem = i;
		
		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlCharacterGeneralItemList.InsertColumn( i, &lvcolumn );
	}

	TCHAR *ptszText04[] = {_T("아이템 명"), _T("번호")};
	
	for( i = 0; i < 2; i++ )
	{
		lvcolumn.mask = LVCF_FMT|LVCF_SUBITEM|LVCF_TEXT|LVCF_WIDTH;
		lvcolumn.fmt = LVCFMT_LEFT;

		lvcolumn.pszText = ptszText04[i];
		lvcolumn.iSubItem = i;
		
		if( i == 0 )		lvcolumn.cx = 175;
		else if( i == 1 )	lvcolumn.cx = 0;

		m_ctrlItemDrawList.InsertColumn( i, &lvcolumn );
	}

	if( m_pItemImageCreateDlg->GetSafeHwnd() == NULL )
    {
		m_pItemImageCreateDlg = new CItemImageCreateDlg();
		m_pItemImageCreateDlg->Create( this, IDD_IMAGEFILECREATE_DIALOG );
	}

	GenderListInit();
	CharacterListInit();
	SubTypeListInit();
	ItemListInit();
	ExprListInit();
	DrawStyleListInit();
	CharacterDrawGeneralItemList();
	OnLeftDraw();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnPaint() 
{
	if( IsIconic() )
	{
		CPaintDC dc(this); // device context for painting

		SendMessage( WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0 );

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics( SM_CXICON );
		int cyIcon = GetSystemMetrics( SM_CYICON );
		CRect rect;
		GetClientRect( &rect );
		int x = ( rect.Width() - cxIcon + 1 ) / 2;
		int y = ( rect.Height() - cyIcon + 1 ) / 2;

		// Draw the icon
		dc.DrawIcon( x, y, m_hIcon );
	}
	else
	{
		CClientDC dc(this);
		CDialog::OnPaint();

		CxImage xImage( _T("Image/wallpaper.gif"), CXIMAGE_FORMAT_GIF );
		xImage.Draw2( dc.GetSafeHdc(), 0, 0, 800, 600 );

		CWindowDC TitleDc(this);

		CRect rc;
		GetWindowRect( rc );

 		CxImage Title( _T("Image/Title.gif"), CXIMAGE_FORMAT_GIF );
		Title.Draw2( TitleDc.GetSafeHdc(), 3, 2, 800, 50 );

		rc.bottom = GetSystemMetrics( SM_CYCAPTION ) + GetSystemMetrics( SM_CYFRAME );
        
		// Area for buttons
        CRect closeRect;
        closeRect.left = rc.right - rc.left - 20;
        closeRect.top = GetSystemMetrics(SM_CYFRAME);
        closeRect.right = rc.right - rc.left - 5;
        closeRect.bottom = GetSystemMetrics( SM_CYSIZE );
		TitleDc.DrawFrameControl( closeRect, DFC_CAPTION, DFCS_CAPTIONCLOSE );
		m_rcClose = closeRect;

        closeRect.left = rc.right - rc.left - 37;
        closeRect.top = GetSystemMetrics(SM_CYFRAME);
        closeRect.right = rc.right - rc.left - 22;
        closeRect.bottom = GetSystemMetrics( SM_CYSIZE );
		TitleDc.DrawFrameControl( closeRect, DFC_CAPTION, DFCS_CAPTIONMAX );

        closeRect.left = rc.right - rc.left - 54;
        closeRect.top = GetSystemMetrics(SM_CYFRAME);
        closeRect.right = rc.right - rc.left - 39;
        closeRect.bottom = GetSystemMetrics( SM_CYSIZE );
		TitleDc.DrawFrameControl( closeRect, DFC_CAPTION, DFCS_CAPTIONMIN );
		m_rcMin = closeRect;

		ImageDraw( SM_4LEAF_AVATAR_SERVER_IMAGE_ABOVE_LAYER );
		ImageDraw( SM_4LEAF_AVATAR_SERVER_IMAGE_EXPR_LAYER );
		ImageDraw( SM_4LEAF_AVATAR_SERVER_IMAGE_UNDER_LAYER );
		ImageDraw( SM_4LEAF_AVATAR_SERVER_IMAGE_TOTAL_LAYER );
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::ImageDraw( int nCheck )
{
	int nBufferSize = 0;
	int nX = 20, nY = 30;
	int nWidth = 120, nHeight = 170;

	CClientDC dc(this);

	BYTE	*pBuffer = NULL;

	if( nCheck == SM_4LEAF_AVATAR_SERVER_IMAGE_ABOVE_LAYER )
	{
		nX = 20;
		pBuffer = m_bAboveBuffer;
		nBufferSize = m_nAboveBufferSize;
	}
	else if( nCheck == SM_4LEAF_AVATAR_SERVER_IMAGE_EXPR_LAYER )
	{
		nX = 20 + nWidth + 20;
		pBuffer = m_bExprBuffer;
		nBufferSize = m_nExprBufferSize;
	}
	else if( nCheck == SM_4LEAF_AVATAR_SERVER_IMAGE_UNDER_LAYER )
	{
		nX = 20 + nWidth * 2 + 40;
		pBuffer = m_bUnderBuffer;
		nBufferSize = m_nUnderBufferSize;	
	}
	else
	{
		nX = 20 + nWidth * 3 + 60;
		pBuffer = m_bBuffer;
		nBufferSize = m_nBufSize;
	}

	CxImage xImage( pBuffer, nBufferSize, CXIMAGE_FORMAT_PNG );
	xImage.Draw2( dc.GetSafeHdc(), nX, nY, xImage.GetWidth(), xImage.GetHeight() );
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnNcLButtonDown( UINT nHitTest, CPoint point ) 
{
	// TODO: Add your message handler code here and/or call default
	CPoint pt = point;
	ScreenToClient( &pt );

	pt.y += GetSystemMetrics( SM_CYCAPTION ) + GetSystemMetrics( SM_CYFRAME );
	pt.x += 5;

	DWORD hitTest = HitTest( pt );

	switch( hitTest )
	{
		case DHT_CLOSE:
			{
				CWindowDC dc(this);
				DrawFrameControl( dc.m_hDC, m_rcClose, DFC_CAPTION, DFCS_CAPTIONCLOSE | DFCS_PUSHED );
				m_LastHit = hitTest;
				m_ButtonDown = hitTest;
				SetCapture();
			}
			break;
		case DHT_MIN:
			{
				CWindowDC dc(this);
				DrawFrameControl( dc.m_hDC, m_rcMin, DFC_CAPTION, DFCS_CAPTIONMIN | DFCS_PUSHED );
				m_LastHit = hitTest;
				m_ButtonDown = hitTest;
				SetCapture();
			}
			break;
		default:
				Default(); 
				break;
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnLButtonUp(UINT nFlags, CPoint point) 
{
	// TODO: Add your message handler code here and/or call default
	if( this != GetCapture() ) return;
	
	CPoint pt = point;
	point.y += GetSystemMetrics( SM_CYCAPTION ) + GetSystemMetrics( SM_CYFRAME );
	point.x += 5;

	// Now test for button
	DWORD hitTest = HitTest(point);

	switch( m_ButtonDown ) // Release the pressed button
	{
		case DHT_CLOSE:
				{
					CWindowDC dc(this);
					DrawFrameControl( dc.m_hDC, m_rcClose, DFC_CAPTION, DFCS_CAPTIONCLOSE );
				}
				break;
		case DHT_MIN:
				{
					CWindowDC dc(this);
					DrawFrameControl( dc.m_hDC, m_rcClose, DFC_CAPTION, DFCS_CAPTIONMIN );
				}
				break;
		default:
				break;
	}

	switch( hitTest )		// Process command if released at on a button
	{
		case 1:
				this->DestroyWindow();
				break;
		case 2:
				this->ShowWindow( SW_MINIMIZE );	
				break;
		default:
				break;
	}

	m_ButtonDown = 0;
	ReleaseCapture();
}

//***************************************************************************
//
DWORD CAvatarImageToolDlg::HitTest( CPoint point )
{
	// Check for buttons
	CRect rect = m_rcClose;

	if( rect.PtInRect( point ) )
		 return (DWORD)DHT_CLOSE;
	else 
	{
		rect = m_rcMin;	
		if( rect.PtInRect( point ) )
			return (DWORD)DHT_MIN;
		
		return (DWORD)DHT_CAPTION;	
	}
}

//***************************************************************************
//
HCURSOR CAvatarImageToolDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

//***************************************************************************
//
void CAvatarImageToolDlg::GenderListInit()
{
	TCHAR *ptszSubType[] =
	{
		_T("남자"), _T("여자")
	};

	m_bManAvatar = true;
	m_bWomanAvatar = false;

	for( int i = 0; i < 2; i++ )
		m_ctrlGenderList.AddString( ptszSubType[i], i );

	m_ctrlGenderList.SelectItemData( m_nGender );
	OnSelchangeGender();
	InvalidateRect( NULL );
}

//***************************************************************************
//
void CAvatarImageToolDlg::CharacterListInit()
{
	CCharacterInfo	CharacterInfo;

	m_ctrlAvatarList.DeleteAllItems();
	if( m_bManAvatar )
	{
		for( int i = 0; i < g_CharacterInfoMem.GetMaxSize(); i++ )
		{	
			memset( &CharacterInfo.m_CharacterInfo, 0, sizeof( SM_AVATAR_IMAGE_CHARACTER_INFO ) );
			if( g_CharacterInfoMem.Search( i + 1, CharacterInfo ) > -1 )
			{
				if( CharacterInfo.m_CharacterInfo.m_tszGender[0] == _T('M') )
					m_ctrlAvatarList.InsertItem( i, CharacterInfo.m_CharacterInfo.m_tszChrName );
			}
		}
	}
	else
	{
		for( int i = 0; i < g_CharacterInfoMem.GetMaxSize(); i++ )
		{	
			memset( &CharacterInfo.m_CharacterInfo, 0, sizeof( SM_AVATAR_IMAGE_CHARACTER_INFO ) );
			if( g_CharacterInfoMem.Search( i + 1, CharacterInfo ) > -1 )
			{
				if( CharacterInfo.m_CharacterInfo.m_tszGender[0] == _T('F') )
					m_ctrlAvatarList.InsertItem( i, CharacterInfo.m_CharacterInfo.m_tszChrName );
			}
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::SubTypeListInit()
{
	TCHAR *ptszSubType[] =
	{
		_T("전체타입"), _T("몸"), _T("표정"), _T("가발"), _T("모자"), _T("상의"), _T("하의"), _T("신발"), _T("무기"), _T("장식")
	};

	for( int i = 0; i < 10; i++ )
		m_ctrlSubTypeList.AddString( ptszSubType[i], i );

	m_ctrlSubTypeList.SelectItemData( m_nSubType );
	OnSelchangeSubtype();
}

//***************************************************************************
//
void CAvatarImageToolDlg::ExprListInit()
{
	TCHAR *ptszExpression[] =
	{
		_T("표정"), 
		_T("하하1"), _T("하하2"), _T("하하3"), _T("하하4"), _T("하하5"), _T("하하6"),
		_T("우씨1"), _T("우씨2"), _T("우씨3"), _T("우씨4"), _T("우씨5"), _T("우씨6"),
		_T("으앙1"), _T("으앙2"), _T("으앙3"), _T("으앙4"), _T("으앙5"), _T("으앙6"),
		_T("좋아1"), _T("좋아2"), _T("좋아3"), _T("좋아4"), _T("좋아5"), _T("좋아6"),
		_T("헉1"), _T("헉2"), _T("헉3"), _T("헉4"), _T("헉5"), _T("헉6"),
		_T("기타1"), _T("기타2"), _T("기타3"), _T("기타4"), _T("기타5"), _T("기타6")
	};

	for( int i = 0; i < SM_4LEAF_AVATAR_SERVER_EXPRESSION_IMAGE_NUM + 1; i++ )
		m_ctrlExprList.AddString( ptszExpression[i], i );

	m_ctrlExprList.SelectItemData( 0 );
}

//***************************************************************************
//
void CAvatarImageToolDlg::DrawStyleListInit()
{
	TCHAR *ptszDirection[] =
	{
		_T("전신상"), _T("상반신"), _T("머리"), _T("줄인머리"), _T("아이콘머리")
	};

	for( int i = 0; i < 5; i++ )
		m_ctrlDrawStyleList.AddString( ptszDirection[i], i );

	m_ctrlDrawStyleList.SelectItemData( 0 );
}

//***************************************************************************
//
void CAvatarImageToolDlg::ItemListInit()
{
	int		nCuritem = 0;
	BOOL	bInsertCheck = false;
	TCHAR	tszIndex[ 8 ];
	
	LV_ITEM lvitem;
	
	CItemInfoList	ItemInfoList;

	m_ctrlItemList.DeleteAllItems();
	for( int i = 0 ; i < g_ItemInfoListMem.GetMaxSize(); i++ )
	{
		memset( &ItemInfoList, 0 , sizeof( ItemInfoList ) );
		
		g_ItemInfoListMem.Search( i, ItemInfoList );

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
						lvitem.iItem = (j == 0 ) ? i : nCuritem;
						lvitem.iSubItem = j;

						switch( j )
						{
							case 0 :
									lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
									nCuritem = m_ctrlItemList.InsertItem( &lvitem );
									break;

							case 1 :
									_stprintf( tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber );
									lvitem.pszText = tszIndex;
									m_ctrlItemList.SetItem( &lvitem );
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
							lvitem.iItem = (j == 0 ) ? i : nCuritem;
							lvitem.iSubItem = j;

							switch( j )
							{
								case 0 :
										lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
										nCuritem = m_ctrlItemList.InsertItem( &lvitem );
										break;

								case 1 :
										_stprintf( tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber );
										lvitem.pszText = tszIndex;
										m_ctrlItemList.SetItem( &lvitem );
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
	if( g_CharacterInfoMem.Search( m_nAvatar, CharacterInfo ) > - 1 )
	{
		int nItemNumber = CharacterInfo.m_CharacterInfo.m_nBodyItemNum;
		CharacterDrawGeneralItemListUpdate( nItemNumber );

		nItemNumber = CharacterInfo.m_CharacterInfo.m_nHairItemNum;
		CharacterDrawGeneralItemListUpdate( nItemNumber );
	
		nItemNumber = CharacterInfo.m_CharacterInfo.m_nJacketItemNum;
		CharacterDrawGeneralItemListUpdate( nItemNumber );

		nItemNumber = CharacterInfo.m_CharacterInfo.m_nPantsItemNum;
		CharacterDrawGeneralItemListUpdate( nItemNumber );
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeGender() 
{
	// TODO: Add your control notification handler code here
	m_nGender = m_ctrlGenderList.GetCurSelData();
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
	m_nSubType = m_ctrlSubTypeList.GetCurSelData();

	ItemListInit();
	InvalidateRect( NULL );
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnSelchangeExpression() 
{
	// TODO: Add your control notification handler code here
	int		nItemNumber = 0;
	int		nIndex = 0;
		
	nIndex = m_ctrlExprList.GetCurSelData();
	
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
	m_nDrawStyle = m_ctrlDrawStyleList.GetCurSelData();
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
		nIndex = m_ctrlAvatarList.HitTest( pNMListView->ptAction, NULL );
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
void CAvatarImageToolDlg::OnDblclkItemList( NMHDR* pNMHDR, LRESULT* pResult ) 
{
	// TODO: Add your control notification handler code here
	int		nIndex = -1;
	TCHAR	tszBuffer[8];

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if( pNMListView->iItem == -1 ) // subitem이 선택되었을 경우 
	{
		// 좌표를 변경하여 item 변호를 얻는다. 
		pNMListView->ptAction.x = 2;
		nIndex = m_ctrlItemList.HitTest( pNMListView->ptAction, NULL );
	}
	else nIndex = pNMListView->iItem;

	_stprintf( tszBuffer, _T("%s"), m_ctrlItemList.GetItemText( nIndex, 1 ) );
	ItemDrawListUpdate( _ttoi( tszBuffer ) );

	*pResult = 0;
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnClickItemdrawList( NMHDR* pNMHDR, LRESULT* pResult ) 
{
	// TODO: Add your control notification handler code here
	int		nIndex = -1;

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if( pNMListView->iItem == -1 ) // subitem이 선택되었을 경우 
	{
		// 좌표를 변경하여 item 변호를 얻는다. 
		pNMListView->ptAction.x = 2;
		nIndex = m_ctrlItemDrawList.HitTest( pNMListView->ptAction, NULL );
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
	m_ctrlItemDrawList.DeleteItem( m_nDelIndex );

	if( m_nDelIndex < 0 ) m_ctrlItemDrawList.DeleteAllItems();
	m_nDelIndex = -1;
}

//***************************************************************************
//
void CAvatarImageToolDlg::CharacterDrawGeneralItemListUpdate( int nItemNumber )
{
	BOOL	bCoverageCheck = true;
	int		nCuritem = 0;
	TCHAR	tszIndex[ 8 ];
	
	LV_ITEM lvitem;
	
	CItemInfoList	ItemInfoList;

	if( nItemNumber < 1 ) return;

	g_ItemInfoListMem.Search( nItemNumber, ItemInfoList ); 

	int nCount = m_ctrlCharacterGeneralItemList.GetItemCount( );

	for( int j = 0; j < 2; j++ )
	{
		lvitem.mask = LVIF_TEXT;
		lvitem.iItem = (j == 0 ) ? nCount : nCuritem;
		lvitem.iSubItem = j;

		switch( j )
		{
			case 0 :
					lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
					nCuritem = m_ctrlCharacterGeneralItemList.InsertItem( &lvitem );
					break;

			case 1 :
					_stprintf( tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber );
					lvitem.pszText = tszIndex;
					m_ctrlCharacterGeneralItemList.SetItem( &lvitem );
					break;
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::ItemDrawListUpdate( int nItemNumber )
{
	BOOL	bCoverageCheck = true;
	int		nCuritem = 0;
	TCHAR	tszIndex[ 8 ];
	TCHAR	tszBuffer[ 32 ];
	TCHAR	tszMessage[ SM_MAXPACKETSIZE ];
	
	LV_ITEM lvitem;
	
	CItemInfoList	ItemInfoList;
	CItemInfoList	TempItemInfoList;

	if( nItemNumber < 1 ) return;

	g_ItemInfoListMem.Search( nItemNumber, ItemInfoList ); 

	int nCount = m_ctrlItemDrawList.GetItemCount( );

	if( nCount >= SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS )
	{
		_stprintf( tszMessage, _T("장착 아이템은 32개만 가능합니다.") );

		AfxMessageBox( tszMessage );
		return;
	}

	for( int i = 0; i < nCount; i++ )
	{	
		m_ctrlItemDrawList.GetItemText( i, 1, tszBuffer, sizeof( tszBuffer ) );

		g_ItemInfoListMem.Search( _ttoi( tszBuffer ), TempItemInfoList );		//AvatarDraw.Lib Function
		if( ( ItemInfoList.m_ItemImageInfo.m_nCoverageMask & TempItemInfoList.m_ItemImageInfo.m_nCoverageMask ) != 0 ) 
		{
			bCoverageCheck = false;
			break;
		}
	}

	if( !bCoverageCheck ) m_ctrlItemDrawList.DeleteItem(i);

	for( int j = 0; j < 2; j++ )
	{
		lvitem.mask = LVIF_TEXT;
		lvitem.iItem = (j == 0 ) ? nCount : nCuritem;
		lvitem.iSubItem = j;

		switch( j )
		{
			case 0 :
					lvitem.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;
					nCuritem = m_ctrlItemDrawList.InsertItem( &lvitem );
					break;

			case 1 :
					_stprintf( tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber );
					lvitem.pszText = tszIndex;
					m_ctrlItemDrawList.SetItem( &lvitem );
					break;
		}
	}
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnImageFileCreatetoolControl() 
{
	// TODO: Add your control notification handler code here
    if( m_pItemImageCreateDlg->IsWindowVisible() )
		m_pItemImageCreateDlg->ShowWindow( SW_HIDE );
	else m_pItemImageCreateDlg->ShowWindow( SW_SHOW );
	
	InvalidateRect( NULL );
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnLeftDraw() 
{
	// TODO: Add your control notification handler code here
	DrawingImage( _T("L") );
	InvalidateRect( NULL );
}

//***************************************************************************
//
void CAvatarImageToolDlg::OnRightDraw() 
{
	// TODO: Add your control notification handler code here
	DrawingImage( _T("R") );
	InvalidateRect( NULL );
}

//***************************************************************************
//
void CAvatarImageToolDlg::DrawingImage( TCHAR *ptszDirection )
{
	int		nCount = 0;
	int		nItemNumber[ SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS ];

	TCHAR	tszBuffer[8];
	TCHAR	tszEncString[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN ];
	TCHAR	tszQueryString[ SM_QUERYSTRING_LEN ];

	CDrawImage				DrawImage;
	CImageInfo				ImageInfo;	
	SM_DRAWING_ITEM_INFO	DrawItemInfo;

	CMemBuffer<BYTE>	Base64EncBuffer;

	DrawImage.SetCanvasInfo( g_AvatarServerReg.m_tszSprFilelocation, SM_4LEAF_AVATAR_SERVER_IMAGE_SIZE_WIDTH, SM_4LEAF_AVATAR_SERVER_IMAGE_SIZE_HEIGHT, 24, 8, SM_4LEAF_AVATAR_SERVER_IMAGE_LAYER_TOTAL, 0 );

	memset( &nItemNumber, 0, SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS );
	for( int i = 0; i < m_ctrlItemDrawList.GetItemCount(); i++ )
	{
		if( i == SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS ) break;
		_stprintf( tszBuffer, _T("%s"), m_ctrlItemDrawList.GetItemText( i, 1 ) );
		
		int nTemp = _ttoi( tszBuffer );
		if( nTemp > 534 ) 
		{
			nItemNumber[nCount] = nTemp;
			nCount++;
		}
	}

	nItemNumber[nCount] = 3377;
	nCount++;

	memset( &DrawItemInfo, 0, sizeof( SM_DRAWING_ITEM_INFO ) ); 

	tszEncString[0] = '\0';
	if( Base64Enc((BYTE *)nItemNumber, i * sizeof(int), Base64EncBuffer) )
	{
		_tcsncpy( tszEncString, (TCHAR *)Base64EncBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 );
		tszEncString[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 ] = '\0';
	}

	_tcsncpy( m_szEncString, tszEncString, SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 );
	m_szEncString[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 ] = '\0';

	_stprintf( tszQueryString, _T("style=%d&side=%s&char=%d&dress=%s"), m_nDrawStyle, ptszDirection, m_nAvatar, tszEncString ); 
	GetDrawingInfo( tszQueryString, DrawItemInfo );

	DrawItemInfo.m_nLayerExpr = SM_4LEAF_AVATAR_SERVER_IMAGE_TOTAL_LAYER;
	if( !DrawImage.SetItemInfoMemDB( DrawItemInfo, ImageInfo )  ) return;
	m_nBufSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy( m_bBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nBufSize );

	DrawItemInfo.m_nLayerExpr = SM_4LEAF_AVATAR_SERVER_IMAGE_ABOVE_LAYER;
	if( !DrawImage.SetItemInfoMemDB( DrawItemInfo, ImageInfo )  ) return;
	m_nAboveBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy( m_bAboveBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nAboveBufferSize );

	DrawItemInfo.m_nLayerExpr = SM_4LEAF_AVATAR_SERVER_IMAGE_EXPR_LAYER;
	if( !DrawImage.SetItemInfoMemDB( DrawItemInfo, ImageInfo )  ) return;
	m_nExprBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy( m_bExprBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nExprBufferSize );

	DrawItemInfo.m_nLayerExpr = SM_4LEAF_AVATAR_SERVER_IMAGE_UNDER_LAYER;
	if( !DrawImage.SetItemInfoMemDB( DrawItemInfo, ImageInfo )  ) return;
	m_nUnderBufferSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
	memcpy( m_bUnderBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nUnderBufferSize );
}

//*************************************************************************
// http://avatar-image.4leaf.com/render-body?style=0&char=6&dress=NIo26OdDrZJEpmqXwGnYgVg
//
BOOL CAvatarImageToolDlg::GetDrawingInfo( TCHAR *ptszImageInfo, SM_DRAWING_ITEM_INFO &DrawItemInfo )
{
	TCHAR   *ptszQueryString = NULL; 

	TCHAR	tszDrawType[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_STR_LEN ];
	TCHAR	tszDirection[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_STR_LEN ];
	TCHAR   tszCharacter[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_STR_LEN ];
	TCHAR   tszData[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN ];

	CMemBuffer<TCHAR>	StrMidBuffer;
	CMemBuffer<BYTE>	Base64DecBuffer;
	CMemBuffer<BYTE>	Base64EncBuffer;

	if( ptszQueryString = _tcsstr( ptszImageInfo, SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_TOKEN ) )
	{
		ptszQueryString = ptszQueryString + _tcslen( SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_TOKEN );

		if( !StrMidToken( ptszQueryString, 1, SM_QUERY_CHAR_TOKEN, StrMidBuffer ) ) return false;

		_tcsncpy( tszDrawType, StrMidBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_STR_LEN - 1 );
		tszDrawType[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_STR_LEN - 1 ] = '\0';
	}
	else return false;

	if( ptszQueryString = _tcsstr( ptszImageInfo, SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_TOKEN ) )
	{
		ptszQueryString = ptszQueryString + _tcslen( SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_TOKEN );

		if( !StrMidToken( ptszQueryString, 1, SM_QUERY_CHAR_TOKEN, StrMidBuffer ) ) return false;

		_tcsncpy( tszDirection, StrMidBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_STR_LEN - 1 );
		tszDirection[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_STR_LEN - 1 ] = '\0';
	}
	else return false;

	if( ptszQueryString = _tcsstr( ptszImageInfo, SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_TOKEN ) )
	{
		ptszQueryString = ptszQueryString + _tcslen( SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_TOKEN );

		if( !StrMidToken( ptszQueryString, 1, SM_QUERY_CHAR_TOKEN, StrMidBuffer ) ) return false;

		_tcsncpy( tszCharacter, StrMidBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_STR_LEN - 1 );
		tszCharacter[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_STR_LEN - 1 ] = '\0';
	}
	else return false;

	if( !IsAllNumeric( tszDrawType ) || !IsAllNumeric( tszCharacter ) ) 
		return false;

	DrawItemInfo.m_nDrawType = _ttoi( tszDrawType );
	_tcsncpy( DrawItemInfo.m_tszDirection, tszDirection, sizeof( DrawItemInfo.m_tszDirection ) - 1 );
	DrawItemInfo.m_tszDirection[ sizeof( DrawItemInfo.m_tszDirection ) - 1 ] = '\0';

	DrawItemInfo.m_nCharacter = _ttoi( tszCharacter );
	DrawItemInfo.m_nExpression = m_nExpression + 1;

	if( ptszQueryString = _tcsstr( ptszImageInfo, SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRESS_TOKEN ) )
	{
		ptszQueryString = ptszQueryString + _tcslen( SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRESS_TOKEN );

		if( !_tcschr( ptszQueryString, SM_QUERY_CHAR_TOKEN ) )
		{	
			if( !StrMidToken( ptszQueryString, 1, _T('\0'), StrMidBuffer ) ) return false;
		}
		else
		{
			if( !StrMidToken( ptszQueryString, 1, SM_QUERY_CHAR_TOKEN, StrMidBuffer ) ) return false;
		}

		_tcsncpy( tszData, StrMidBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 );
		tszData[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 ] = '\0';
	
		int nCount = -1;
		int nBufLen = -1;
		int *pnDecData = NULL;

		if( !Base64Dec( (BYTE *)tszData, _tcslen( tszData ), Base64DecBuffer ) ) return false;

		nBufLen = Base64DecBuffer.GetBufSize();

		nCount = nBufLen / sizeof(int) ;

		pnDecData = (int*)Base64DecBuffer.GetBuffer();

		Quicksort( pnDecData, 0, nCount - 1 );

		for ( int i = 0; i < nCount ; i++ )
			DrawItemInfo.m_nItemNumber[i] = pnDecData[i];

		DrawItemInfo.m_nItemCount = nCount;

		if( Base64Enc((BYTE *)pnDecData, sizeof( int ) * nCount, Base64EncBuffer) )
		{
			_tcsncpy( tszData, (TCHAR *)Base64EncBuffer.GetBuffer(), SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 );
			tszData[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN - 1 ] = '\0';
		}

		_stprintf( DrawItemInfo.m_tszRequestImageURL, _T("%s%s&%s%s&%s%s&%s%s"), 
				SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRAW_TYPE_TOKEN, tszDrawType,
				SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_TOKEN, tszDirection,
				SM_4LEAF_AVATAR_SERVER_QUERYSTRING_CHARACTER_TOKEN, tszCharacter,
				SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DRESS_TOKEN, tszData );
	}
	else return false;

	return true;
}

//***************************************************************************
//
void Swap( int& nSrc, int& nDest )
{
	int nTemp = nSrc;
	nSrc = nDest;
	nDest = nTemp;
}  

//***************************************************************************
//
void Partition( int A[], int nF, int nL, int& nPivotIndex )
{
	int nPivot = A[ nF ];     
	int nLastS1 = nF;            
	int nFirstUnknown = nF + 1;  

	for( ; nFirstUnknown <= nL; ++nFirstUnknown )
	{
		if ( A[ nFirstUnknown ] < nPivot )
		{  
			++nLastS1;
			Swap( A[ nFirstUnknown ], A[ nLastS1 ] );
		}
	}

	Swap( A[ nF ], A[ nLastS1 ] );
	nPivotIndex = nLastS1;
}  

//***************************************************************************
//
void Quicksort( int A[], int nF, int nL )
{
	int nPivotIndex;

	if ( nF < nL )
	{
		Partition( A, nF, nL, nPivotIndex );

		Quicksort( A, nF, nPivotIndex - 1 );
		Quicksort( A, nPivotIndex + 1, nL );
	}
}
