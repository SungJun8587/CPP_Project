
//***************************************************************************
// AvatarImageToolDlg.h : interface for the CAvatarImageToolDlg class.
//
//***************************************************************************

#ifndef __AVATARIMAGETOOLDLG_H__
#define __AVATARIMAGETOOLDLG_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define DHT_CLOSE		0x01
#define DHT_MIN			0x02 
#define DHT_CAPTION		0x03

#include "ItemImageCreateDlg.h"

//***************************************************************************
//
class CAvatarImageToolDlg : public CDialog
{
// Construction
public:
	CAvatarImageToolDlg(CWnd* pParent = NULL);	// standard constructor
	~CAvatarImageToolDlg();

// Dialog Data
	//{{AFX_DATA(CAvatarImageToolDlg)
	enum { IDD = IDD_AVATARIMAGETOOL_DIALOG };
	CButton		m_ctrlCancel;
	CButton		m_ctrlImageFileCreateTool;
	CButton		m_ctrlListDelete;
	CButton		m_ctrlRightDraw;
	CButton		m_ctrlLeftDraw;
	CListCtrl	m_ctrlCharacterGeneralItemList;
	CListCtrl	m_ctrlItemDrawList;
	CListCtrl	m_ctrlItemList;
	CListCtrl	m_ctrlAvatarList;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAvatarImageToolDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CAvatarImageToolDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnDblclkAvatarList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkItemList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSelchangeSubtype();
	afx_msg void OnSelchangeGender();
	afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnListDelete();
	afx_msg void OnClickItemdrawList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSelchangeExpression();
	afx_msg void OnLeftDraw();
	afx_msg void OnRightDraw();
	afx_msg void OnSelchangeDrawstyle();
	afx_msg void OnImageFileCreatetoolControl();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

private:
	void    CharacterListInit();
	void	ItemListInit();
	void	GenderListInit();
	void	SubTypeListInit();
	void	ExprListInit();
	void	DrawStyleListInit();
	void	CharacterDrawGeneralItemList();
	void	CharacterDrawGeneralItemListUpdate( int nItemNumber );
	void	ItemDrawListUpdate( int nItemNumber );

	void	DrawingImage( TCHAR *ptszDirection );
	BOOL	GetDrawingInfo( TCHAR *pszImageInfo, SM_DRAWING_ITEM_INFO &DrawItemInfo );
	void    ImageDraw( int nCheck );
	DWORD	HitTest( CPoint point );

private:
	int			m_nDelIndex;
	int			m_nSubType;
	int			m_nGender;
	int			m_nExpression;
	int			m_nDrawStyle;

	int			m_nAvatar;
	int			m_nBufSize;
	int			m_nAboveBufferSize;
	int			m_nExprBufferSize;
	int			m_nUnderBufferSize;

	BYTE		m_bBuffer[ SM_MAXBUFFERSIZE ];
	BYTE		m_bAboveBuffer[ SM_MAXBUFFERSIZE ];
	BYTE		m_bExprBuffer[ SM_MAXBUFFERSIZE ];
	BYTE		m_bUnderBuffer[ SM_MAXBUFFERSIZE ];

	TCHAR		m_szEncString[ SM_4LEAF_AVATAR_SERVER_QUERYSTRING_DATA_STR_ENC_LEN ];

	CRect		m_ImageRect;
	CRect		m_rcClose;
	CRect		m_rcMin;

	DWORD		m_LastHit;
	DWORD		m_ButtonDown;

	BOOL		m_bManAvatar;
	BOOL		m_bWomanAvatar;

	CItemDataCombo		m_ctrlSubTypeList;
	CItemDataCombo		m_ctrlGenderList;
	CItemDataCombo		m_ctrlExprList;
	CItemDataCombo		m_ctrlDrawStyleList;

	CItemImageCreateDlg	*m_pItemImageCreateDlg;
};

#endif // ndef __AVATARIMAGETOOLDLG_H__
