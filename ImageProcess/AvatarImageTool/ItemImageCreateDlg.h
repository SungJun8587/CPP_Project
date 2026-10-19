
//***************************************************************************
// ItemImageCreateDlg.h : interface for the CItemImageCreateDlg class.
//
//***************************************************************************

#ifndef __ITEMIMAGECREATEDLG_H__
#define __ITEMIMAGECREATEDLG_H__

#include <windows.h>
#include <commctrl.h>
#include <tchar.h>

#include "Resource.h"
#include "AvatarImageDefine.h"
#include "ItemDataCombo.h"

//***************************************************************************
//
class CItemImageCreateDlg
{
public:
    CItemImageCreateDlg();
    ~CItemImageCreateDlg();

    BOOL Create(HWND hParentWnd, INT_PTR nID);
    void ShowWindow(int nCmdShow);
    BOOL IsWindowVisible() const;
    HWND GetHwnd() const { return m_hWnd; }

private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void    OnInitDialog();
    void    OnPaint();
    void    OnClose();
    void    OnSelchangeSubtype();
    void    OnMan();
    void    OnWoman();
    void    OnLeftButton();
    void    OnRightButton();
    void    OnImageFileCreate();
    void    OnListMove();
    void    OnListDelete();
    void    OnDblclkItemlist(LPNMHDR pNMHDR);
    void    OnClickItemviewlist(LPNMHDR pNMHDR);

    void    SubTypeListInit();
    void    ItemListInit();
    void    UpdateData(BOOL bSaveAndValidate);

private:
    HWND    m_hWnd;
    HWND    m_hParentWnd;

    HWND    m_hCtrlItemList;
    HWND    m_hCtrlItemDrawList;
    CItemDataCombo m_ctrlSubTypeList;

    BOOL    m_bManAvatar;
    BOOL    m_bWomanAvatar;
    BOOL    m_bLeft;
    BOOL    m_bRight;

    int     m_nLeft;
    int     m_nRight;
    int     m_nTop;
    int     m_nBottom;
    int     m_nWidth;
    int     m_nHeight;

    int     m_nSubType;
    int     m_nDelIndex;
    int     m_nBufSize;
    BYTE    m_bBuffer[AVATARITEM_AVATAR_IMAGE_BUFFER_SIZE];
};

#endif // ndef __ITEMIMAGECREATEDLG_H__
