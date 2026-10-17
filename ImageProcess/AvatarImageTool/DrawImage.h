
//***************************************************************************
// DrawImage.h : interface for the CDrawImage class.
//
//***************************************************************************

#ifndef	__DRAWIMAGE_H__
#define	__DRAWIMAGE_H__

#include <Image/Canvas.h>

//***************************************************************************
//
class CImageInfo
{
public:
	CImageInfo()
	{
		Reset();
	}
	~CImageInfo() {};

	void Reset(void)
	{
		memset(&m_CacheMemInfo, 0, sizeof(AVATAR_IMAGE_CACHE_MEMORY_INFO));
	}

	BOOL CompareByRequestImageUrl(TCHAR* tszRequestImageURL)
	{
		if( _tcscmp(tszRequestImageURL, m_CacheMemInfo.m_tszRequestImageURL) == 0 )
			return true;
		return false;
	}

	const CImageInfo& operator=(const CImageInfo& ImageInfo)
	{
		memcpy(&m_CacheMemInfo, &ImageInfo.m_CacheMemInfo, sizeof(AVATAR_IMAGE_CACHE_MEMORY_INFO));
		return *this;
	}

	void GetStream(TCHAR* ptszBuffer, const size_t bufSize)
	{
		_sntprintf_s(ptszBuffer, bufSize, _TRUNCATE, _T("%s,%d"), m_CacheMemInfo.m_tszRequestImageURL, m_CacheMemInfo.m_nBufSize);
	}

	AVATAR_IMAGE_CACHE_MEMORY_INFO	m_CacheMemInfo;
};

//***************************************************************************
//
class CDrawImage
{
public:
	CDrawImage();
	CDrawImage(TCHAR* pszSprFilePath, int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType = 0);
	~CDrawImage();

public:
	void		SetCanvasInfo(TCHAR* pszSprFilePath, int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType);

	BOOL		SetItemInfoMemDB(int nLayerExpr, TCHAR* tszDirection, int nCharacter, int nExprImageNum, int* pnItemNumber, int nItemCount, RECT rRect, int nWidth, int nHeight);
	BOOL		SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, CImageInfo& ImageInfo);
	BOOL		SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, BYTE*& pbBuffer, int& nBufferSize);
	BOOL		SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CImageInfo& ImageInfo);
	BOOL		SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, BYTE*& pbBuffer, int& nBufferSize);
	BOOL		SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, CImageInfo& ImageInfo);
	BOOL		SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, BYTE*& pbBuffer, int& nBufferSize);

private:
	void		Init(TCHAR* ptszSprFilePath);
	void		Create(int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType = 0);

	BOOL		Draw(int nLayerExpr, TCHAR* tszDirection, int nCharacter, int nExprImageNum, RECT rRect, int nWidth, int nHeight, CItemInfoList*& pItemInfoList);
	BOOL		Draw(int nLayerExpr, int nDrawType, TCHAR* tszDirection, CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo);
	BOOL		Draw(int nLayerExpr, int nDrawType, TCHAR* tszDirection, CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize);
	BOOL		Draw(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo);
	BOOL		Draw(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize);
	BOOL		Draw(CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo);
	BOOL		Draw(CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize);

	BOOL		SetCanvas(CItemInfoList*& pItemInfoList, CCanvas& Canvas);
	BOOL		SetCanvas(CItemInfoList*& pItemInfoList, CCanvas& Canvas, int nLayerExpr);

private:
	TCHAR		m_tszSprFilelocation[DIRECTORY_STRLEN];
	int			m_nWidth;
	int			m_nHeight;
	int			m_nCanvasBit;
	int			m_nAlphaBit;
	int			m_nImageType;
	int			m_nTotalLayer;
};

#endif // ndef __DRAWIMAGE_H__
