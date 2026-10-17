
//***************************************************************************
// ItemDataCombo.cpp: implementation of the CDrawImage class.
//
//***************************************************************************

#include "pch.h"
#include "ItemDataCombo.h"

//***************************************************************************
// Construction/Destruction
//***************************************************************************

CItemDataCombo::CItemDataCombo() : m_hWnd(NULL) {}
CItemDataCombo::~CItemDataCombo() {}

void CItemDataCombo::Init(HWND hDlg, int nID)
{
    m_hWnd = GetDlgItem(hDlg, nID);
}

int CItemDataCombo::AddString(const TCHAR* ptszString, DWORD_PTR dwData)
{
    if( !m_hWnd ) return -1;
    int nIdx = (int)SendMessage(m_hWnd, CB_ADDSTRING, 0, (LPARAM)ptszString);
    if( nIdx != CB_ERR )
    {
        SendMessage(m_hWnd, CB_SETITEMDATA, nIdx, (LPARAM)dwData);
    }
    return nIdx;
}

DWORD_PTR CItemDataCombo::GetCurSelData()
{
    if( !m_hWnd ) return 0;
    int nIdx = (int)SendMessage(m_hWnd, CB_GETCURSEL, 0, 0);
    if( nIdx != CB_ERR )
    {
        return (DWORD_PTR)SendMessage(m_hWnd, CB_GETITEMDATA, nIdx, 0);
    }
    return 0;
}

int CItemDataCombo::SelectItemData(DWORD_PTR dwData)
{
    if( !m_hWnd ) return -1;
    int nCount = (int)SendMessage(m_hWnd, CB_GETCOUNT, 0, 0);
    for( int nIdx = 0; nIdx < nCount; nIdx++ )
    {
        if( (DWORD_PTR)SendMessage(m_hWnd, CB_GETITEMDATA, nIdx, 0) == dwData )
        {
            SendMessage(m_hWnd, CB_SETCURSEL, nIdx, 0);
            return nIdx;
        }
    }
    return -1;
}

void CItemDataCombo::ResetContent()
{
    if( m_hWnd )
        SendMessage(m_hWnd, CB_RESETCONTENT, 0, 0);
}