
//***************************************************************************
// ItemDataCombo.h : interface for the CItemDataCombo class.
//
//***************************************************************************

#ifndef __ITEMDATACOMBO_H__
#define __ITEMDATACOMBO_H__

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000


//***************************************************************************
// CItemDataCombo window
class CItemDataCombo
{
public:
    CItemDataCombo();
    ~CItemDataCombo();

    void Init(HWND hDlg, int nID);
    HWND GetHwnd() const { return m_hWnd; }

    int     AddString(const TCHAR* ptszString, DWORD_PTR dwData);
    int     SelectItemData(DWORD_PTR dwData);
    DWORD_PTR GetCurSelData();
    void    ResetContent();

private:
    HWND    m_hWnd;
};

#endif // ndef __ITEMDATACOMBO_H__
