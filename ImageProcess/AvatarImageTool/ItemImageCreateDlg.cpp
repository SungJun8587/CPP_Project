
//***************************************************************************
// ItemImageCreateDlg.cpp : implementation of the CItemImageCreateDlg class.
//
//***************************************************************************

#include "pch.h"
#include "AvatarImage.h"
#include "ItemImageCreateDlg.h"

CItemImageCreateDlg::CItemImageCreateDlg()
    : m_hWnd(NULL), m_hParentWnd(NULL), m_hCtrlItemList(NULL), m_hCtrlItemDrawList(NULL),
    m_bManAvatar(TRUE), m_bWomanAvatar(FALSE), m_bLeft(TRUE), m_bRight(FALSE),
    m_nLeft(0), m_nRight(120), m_nTop(0), m_nBottom(170), m_nWidth(120), m_nHeight(170),
    m_nSubType(0), m_nBufSize(0), m_nDelIndex(-1)
{
    memset(m_bBuffer, 0, sizeof(m_bBuffer));
}

CItemImageCreateDlg::~CItemImageCreateDlg()
{
    if( m_hWnd && IsWindow(m_hWnd) )
    {
        DestroyWindow(m_hWnd);
    }
}

BOOL CItemImageCreateDlg::Create(HWND hParentWnd, INT_PTR nID)
{
    m_hParentWnd = hParentWnd;
    m_hWnd = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(nID), hParentWnd, DlgProc, (LPARAM)this);
    return (m_hWnd != NULL);
}

void CItemImageCreateDlg::ShowWindow(int nCmdShow)
{
    if( m_hWnd )
        ::ShowWindow(m_hWnd, nCmdShow);
}

BOOL CItemImageCreateDlg::IsWindowVisible() const
{
    return m_hWnd ? ::IsWindowVisible(m_hWnd) : FALSE;
}

INT_PTR CALLBACK CItemImageCreateDlg::DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CItemImageCreateDlg* pThis = (CItemImageCreateDlg*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    if( uMsg == WM_INITDIALOG )
    {
        pThis = (CItemImageCreateDlg*)lParam;
        pThis->m_hWnd = hDlg;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->OnInitDialog();
        return TRUE;
    }

    if( pThis )
    {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }

    return FALSE;
}

INT_PTR CItemImageCreateDlg::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch( uMsg )
    {
    case WM_PAINT:
        OnPaint();
        return TRUE;

    case WM_MOVING:
        Sleep(100);
        if( m_hParentWnd ) ::InvalidateRect(m_hParentWnd, NULL, FALSE);
        return TRUE;

    case WM_CLOSE:
        OnClose();
        return TRUE;

    case WM_COMMAND:
    {
        WORD wID = LOWORD(wParam);
        WORD wCode = HIWORD(wParam);

        if( wCode == CBN_SELCHANGE && wID == IDC_SUBTYPE )
        {
            OnSelchangeSubtype();
            return TRUE;
        }

        switch( wID )
        {
        case IDC_MAN: OnMan(); return TRUE;
        case IDC_WOMAN: OnWoman(); return TRUE;
        case IDC_LEFTBUTTON: OnLeftButton(); return TRUE;
        case IDC_RIGHTBUTTON: OnRightButton(); return TRUE;
        case IDC_IMAGEFILECREATE: OnImageFileCreate(); return TRUE;
        case IDC_MOVE: OnListMove(); return TRUE;
        case IDC_LISTDELETE: OnListDelete(); return TRUE;
        }
    }
    break;

    case WM_NOTIFY:
    {
        LPNMHDR pNMHDR = (LPNMHDR)lParam;
        if( pNMHDR->idFrom == IDC_ITEMLIST && pNMHDR->code == NM_DBLCLK )
        {
            OnDblclkItemlist(pNMHDR);
            return TRUE;
        }
        else if( pNMHDR->idFrom == IDC_ITEMVIEWLIST && pNMHDR->code == NM_CLICK )
        {
            OnClickItemviewlist(pNMHDR);
            return TRUE;
        }
    }
    break;
    }
    return FALSE;
}

void CItemImageCreateDlg::OnInitDialog()
{
    if( m_hParentWnd ) ::InvalidateRect(m_hParentWnd, NULL, FALSE);

    m_hCtrlItemList = GetDlgItem(m_hWnd, IDC_ITEMLIST);
    m_hCtrlItemDrawList = GetDlgItem(m_hWnd, IDC_ITEMVIEWLIST);

    SendMessage(m_hCtrlItemList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
    SendMessage(m_hCtrlItemDrawList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
    lvc.fmt = LVCFMT_LEFT;

    TCHAR* ptszText01[] = { _T("아이템 명"), _T("번호") };
    for( int i = 0; i < 2; i++ )
    {
        lvc.pszText = ptszText01[i];
        lvc.iSubItem = i;
        lvc.cx = (i == 0) ? 155 : 0;
        ListView_InsertColumn(m_hCtrlItemList, i, &lvc);
    }

    TCHAR* ptszText02[] = { _T("아이템 명"), _T("번호") };
    for( int i = 0; i < 2; i++ )
    {
        lvc.pszText = ptszText02[i];
        lvc.iSubItem = i;
        lvc.cx = (i == 0) ? 175 : 0;
        ListView_InsertColumn(m_hCtrlItemDrawList, i, &lvc);
    }

    m_ctrlSubTypeList.Init(m_hWnd, IDC_SUBTYPE);

    UpdateData(FALSE);
    SubTypeListInit();
}

void CItemImageCreateDlg::OnClose()
{
    if( m_hParentWnd ) ::InvalidateRect(m_hParentWnd, NULL, FALSE);
    ShowWindow(SW_HIDE);
}

void CItemImageCreateDlg::UpdateData(BOOL bSaveAndValidate)
{
    if( bSaveAndValidate )
    {
        m_nLeft = GetDlgItemInt(m_hWnd, IDC_LEFT, NULL, TRUE);
        m_nRight = GetDlgItemInt(m_hWnd, IDC_RIGHT, NULL, TRUE);
        m_nTop = GetDlgItemInt(m_hWnd, IDC_TOP, NULL, TRUE);
        m_nBottom = GetDlgItemInt(m_hWnd, IDC_BOTTOM, NULL, TRUE);
        m_nWidth = GetDlgItemInt(m_hWnd, IDC_WIDTH, NULL, TRUE);
        m_nHeight = GetDlgItemInt(m_hWnd, IDC_HEIGHT, NULL, TRUE);

        m_bManAvatar = (IsDlgButtonChecked(m_hWnd, IDC_MAN) == BST_CHECKED);
        m_bWomanAvatar = (IsDlgButtonChecked(m_hWnd, IDC_WOMAN) == BST_CHECKED);
        m_bLeft = (IsDlgButtonChecked(m_hWnd, IDC_LEFTBUTTON) == BST_CHECKED);
        m_bRight = (IsDlgButtonChecked(m_hWnd, IDC_RIGHTBUTTON) == BST_CHECKED);
    }
    else
    {
        SetDlgItemInt(m_hWnd, IDC_LEFT, m_nLeft, TRUE);
        SetDlgItemInt(m_hWnd, IDC_RIGHT, m_nRight, TRUE);
        SetDlgItemInt(m_hWnd, IDC_TOP, m_nTop, TRUE);
        SetDlgItemInt(m_hWnd, IDC_BOTTOM, m_nBottom, TRUE);
        SetDlgItemInt(m_hWnd, IDC_WIDTH, m_nWidth, TRUE);
        SetDlgItemInt(m_hWnd, IDC_HEIGHT, m_nHeight, TRUE);

        CheckDlgButton(m_hWnd, IDC_MAN, m_bManAvatar ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_hWnd, IDC_WOMAN, m_bWomanAvatar ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_hWnd, IDC_LEFTBUTTON, m_bLeft ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_hWnd, IDC_RIGHTBUTTON, m_bRight ? BST_CHECKED : BST_UNCHECKED);
    }
}

void CItemImageCreateDlg::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hWnd, &ps);

    if( m_nBufSize > 0 )
    {
        CxImage xImage(m_bBuffer, m_nBufSize, CXIMAGE_FORMAT_PNG);
        xImage.Draw2(hdc, 10, 30, xImage.GetWidth(), xImage.GetHeight());
    }

    EndPaint(m_hWnd, &ps);
}

void CItemImageCreateDlg::OnMan()
{
    m_bManAvatar = TRUE;
    m_bWomanAvatar = FALSE;
    ItemListInit();
    UpdateData(FALSE);
}

void CItemImageCreateDlg::OnWoman()
{
    m_bManAvatar = FALSE;
    m_bWomanAvatar = TRUE;
    ItemListInit();
    UpdateData(FALSE);
}

void CItemImageCreateDlg::OnLeftButton()
{
    m_bLeft = TRUE;
    m_bRight = FALSE;
    UpdateData(FALSE);
}

void CItemImageCreateDlg::OnRightButton()
{
    m_bLeft = FALSE;
    m_bRight = TRUE;
    UpdateData(FALSE);
}

void CItemImageCreateDlg::SubTypeListInit()
{
    TCHAR* ptszSubType[] = {
        _T("전체타입"), _T("몸"), _T("표정"), _T("가발"), _T("모자"), _T("상의"), _T("하의"), _T("신발"), _T("무기"), _T("장식")
    };

    m_ctrlSubTypeList.ResetContent();
    for( int i = 0; i < 10; i++ )
        m_ctrlSubTypeList.AddString(ptszSubType[i], i);

    m_ctrlSubTypeList.SelectItemData(m_nSubType);
    OnSelchangeSubtype();
}

void CItemImageCreateDlg::OnSelchangeSubtype()
{
    m_nSubType = (int)m_ctrlSubTypeList.GetCurSelData();
    ItemListInit();
    ::InvalidateRect(m_hWnd, NULL, TRUE);
}

void CItemImageCreateDlg::ItemListInit()
{
    int nCuritem = 0;
    BOOL bInsertCheck = FALSE;
    TCHAR tszIndex[8];

    LVITEM lvi = { 0 };
    CItemInfoList ItemInfoList;

    ListView_DeleteAllItems(m_hCtrlItemList);

    for( int i = 0; i < g_ItemInfoListMem.GetMaxSize(); i++ )
    {
        memset(&ItemInfoList, 0, sizeof(ItemInfoList));
        g_ItemInfoListMem.Search(i, ItemInfoList);

        if( m_nSubType == 0 || ItemInfoList.m_ItemImageInfo.m_nSubType == m_nSubType )
        {
            if( ItemInfoList.m_ItemImageInfo.m_nItemNumber > 0 )
            {
                if( m_bManAvatar )
                    bInsertCheck = (ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('M'));
                else if( m_bWomanAvatar )
                    bInsertCheck = (ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('F'));

                if( bInsertCheck )
                {
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = ListView_GetItemCount(m_hCtrlItemList);
                    lvi.iSubItem = 0;
                    lvi.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;

                    int nInserted = ListView_InsertItem(m_hCtrlItemList, &lvi);

                    _stprintf(tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
                    ListView_SetItemText(m_hCtrlItemList, nInserted, 1, tszIndex);
                }
            }
        }
    }
}

void CItemImageCreateDlg::OnDblclkItemlist(LPNMHDR pNMHDR)
{
    LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)pNMHDR;
    int nIndex = pNMLV->iItem;

    if( nIndex == -1 )
    {
        LVHITTESTINFO lvhti;
        lvhti.pt = pNMLV->ptAction;
        nIndex = ListView_HitTest(m_hCtrlItemList, &lvhti);
    }

    if( nIndex != -1 )
    {
        TCHAR tszBuffer[16] = { 0 };
        ListView_GetItemText(m_hCtrlItemList, nIndex, 1, tszBuffer, sizeof(tszBuffer) / sizeof(TCHAR));

        int nItemNumber[SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS] = { 0 };
        nItemNumber[0] = _ttoi(tszBuffer);

        CImageInfo ImageInfo;
        CDrawImage DrawImage;

        DrawImage.SetItemInfoMemDB(nItemNumber, 1, ImageInfo);
        m_nBufSize = ImageInfo.m_CacheMemInfo.m_nBufSize;
        memcpy(m_bBuffer, ImageInfo.m_CacheMemInfo.m_bBuffer, m_nBufSize);

        ::InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

void CItemImageCreateDlg::OnClickItemviewlist(LPNMHDR pNMHDR)
{
    LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)pNMHDR;
    int nIndex = pNMLV->iItem;

    if( nIndex == -1 )
    {
        LVHITTESTINFO lvhti;
        lvhti.pt = pNMLV->ptAction;
        nIndex = ListView_HitTest(m_hCtrlItemDrawList, &lvhti);
    }

    m_nDelIndex = nIndex;
}

void CItemImageCreateDlg::OnListDelete()
{
    if( m_nDelIndex >= 0 )
    {
        ListView_DeleteItem(m_hCtrlItemDrawList, m_nDelIndex);
    }
    else
    {
        ListView_DeleteAllItems(m_hCtrlItemDrawList);
    }
    m_nDelIndex = -1;
    ::InvalidateRect(m_hWnd, NULL, TRUE);
}

void CItemImageCreateDlg::OnListMove()
{
    TCHAR tszIndex[8];
    TCHAR tszBuffer[256];
    LVITEM lvi = { 0 };

    int nSelectedCount = ListView_GetSelectedCount(m_hCtrlItemList);

    if( nSelectedCount > 0 )
    {
        int nItem = -1;
        while( (nItem = ListView_GetNextItem(m_hCtrlItemList, nItem, LVNI_SELECTED)) != -1 )
        {
            ListView_GetItemText(m_hCtrlItemList, nItem, 0, tszBuffer, sizeof(tszBuffer) / sizeof(TCHAR));
            ListView_GetItemText(m_hCtrlItemList, nItem, 1, tszIndex, sizeof(tszIndex) / sizeof(TCHAR));

            int nCount = ListView_GetItemCount(m_hCtrlItemDrawList);

            lvi.mask = LVIF_TEXT;
            lvi.iItem = nCount;
            lvi.iSubItem = 0;
            lvi.pszText = tszBuffer;

            int nInserted = ListView_InsertItem(m_hCtrlItemDrawList, &lvi);
            ListView_SetItemText(m_hCtrlItemDrawList, nInserted, 1, tszIndex);
        }
    }
    else
    {
        ListView_DeleteAllItems(m_hCtrlItemDrawList);
        CItemInfoList ItemInfoList;

        for( int i = 0; i < g_ItemInfoListMem.GetMaxSize(); i++ )
        {
            memset(&ItemInfoList, 0, sizeof(ItemInfoList));
            g_ItemInfoListMem.Search(i, ItemInfoList);

            if( ItemInfoList.m_ItemImageInfo.m_nSubType == m_nSubType )
            {
                BOOL bInsertCheck = FALSE;
                if( m_bManAvatar )
                    bInsertCheck = (ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('M'));
                else if( m_bWomanAvatar )
                    bInsertCheck = (ItemInfoList.m_ItemImageInfo.m_tszGender[0] == _T('F'));

                if( bInsertCheck )
                {
                    int nCount = ListView_GetItemCount(m_hCtrlItemDrawList);

                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = nCount;
                    lvi.iSubItem = 0;
                    lvi.pszText = ItemInfoList.m_ItemImageInfo.m_tszName;

                    int nInserted = ListView_InsertItem(m_hCtrlItemDrawList, &lvi);

                    _stprintf(tszIndex, _T("%d"), ItemInfoList.m_ItemImageInfo.m_nItemNumber);
                    ListView_SetItemText(m_hCtrlItemDrawList, nInserted, 1, tszIndex);
                }
            }
        }
    }
}

void CItemImageCreateDlg::OnImageFileCreate()
{
    int nExprImageNum = 0;
    int nCharacter = 0;
    int nWidth = 0, nHeight = 0;
    int nItemCount = 0;
    int nItemNumber[SM_4LEAF_AVATAR_SERVER_MAX_EQUIPMENTS] = { 0 };

    TCHAR tszBuffer[8];
    TCHAR tszDirection[SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DIRECTION_STR_LEN];

    RECT rRect;
    CDrawImage DrawImage;

    UpdateData(TRUE);

    if( m_bLeft ) _stprintf(tszDirection, _T("L"));
    else _stprintf(tszDirection, _T("R"));

    rRect.top = m_nTop;
    rRect.bottom = m_nBottom;
    rRect.left = m_nLeft;
    rRect.right = m_nRight;
    nWidth = m_nWidth;
    nHeight = m_nHeight;

    if( m_bManAvatar ) nCharacter = 0;
    else if( m_bWomanAvatar ) nCharacter = 8;

    int nCount = ListView_GetItemCount(m_hCtrlItemDrawList);

    for( int i = 0; i < nCount; i++ )
    {
        ListView_GetItemText(m_hCtrlItemDrawList, i, 1, tszBuffer, sizeof(tszBuffer) / sizeof(TCHAR));
        nItemNumber[0] = _ttoi(tszBuffer);
        nItemCount = 1;

        nExprImageNum = (i + 1) % 33;

        if( nExprImageNum == 1 ) nCharacter++;
        if( nExprImageNum == 0 ) nExprImageNum = 33;
        if( nCharacter == 8 ) nCharacter = 17;
        if( nCharacter == 16 ) nCharacter = 18;

        DrawImage.SetItemInfoMemDB(SM_4LEAF_AVATAR_SERVER_IMAGE_TOTAL_LAYER, tszDirection, nCharacter, nExprImageNum, nItemNumber, nItemCount, rRect, nWidth, nHeight);
        Sleep(10);
    }
}

