
// AvatarImageToolDlg.h: 헤더 파일
//

#pragma once
#include <afxcmn.h>

#define DHT_NONE		0x00
#define DHT_CLOSE		0x01
#define DHT_MIN			0x02
#define DHT_CAPTION		0x03

// CAvatarImageToolDlg 대화 상자
class CAvatarImageToolDlg : public CDialogEx
{
// 생성입니다.
public:
	CAvatarImageToolDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_AVATARIMAGETOOL_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON		m_hIcon;

	CButton		m_ctrlCancel;
	CButton		m_ctrlImageFileCreateTool;
	CButton		m_ctrlListDelete;
	CButton		m_ctrlRightDraw;
	CButton		m_ctrlLeftDraw;
	CListCtrl	m_ctrlCharacterGeneralItemList;
	CListCtrl	m_ctrlAvatarList;
	CListCtrl	m_ctrlItemList;
	CListCtrl	m_ctrlItemDrawList;

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnDblclkAvatarList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkItemList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSelchangeSubtype();
	afx_msg void OnSelchangeGender();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	DWORD HitTest(CPoint point) const;
	void  DrawTitleButtons(CDC& dc);
	afx_msg void OnListDelete();
	afx_msg void OnClickItemdrawList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSelchangeExpression();
	afx_msg void OnLeftDraw();
	afx_msg void OnRightDraw();
	afx_msg void OnSelchangeDrawstyle();
	afx_msg void OnImageFileCreatetoolControl();

	DECLARE_MESSAGE_MAP()


private:
	void	GenderListInit();
	void    CharacterListInit();
	void	DrawStyleListInit();
	void	ItemListInit();
	void	SubTypeListInit();
	void	ExprListInit();
	void	CharacterDrawGeneralItemList();
	void	CharacterDrawGeneralItemListUpdate(int nItemNumber);
	void	ItemDrawListUpdate(int nItemNumber);

	DWORD	HitTest(CPoint point);

private:
	int			m_nDelIndex;
	int			m_nSubType;
	int			m_nGender;
	int			m_nExpression;
	int			m_nDrawStyle;

	int			m_nAvatar;
	int			m_nBufSize;

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
};
