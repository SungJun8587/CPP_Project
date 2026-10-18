
//***************************************************************************
// DrawImage.cpp: implementation of the CDrawImage class.
//
//***************************************************************************

#include "pch.h"
#include "DrawImage.h"

//***************************************************************************
// Construction/Destruction
//***************************************************************************

CDrawImage::CDrawImage()
{
}

CDrawImage::CDrawImage(const TCHAR* ptszSprFilePath, int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType)
{
	Init(ptszSprFilePath);
	Create(nWidth, nHeight, nCanvasBit, nAlphaBit, nTotalLayer, nImageType);
}

CDrawImage::~CDrawImage()
{
}

//***************************************************************************
//
void CDrawImage::Init(const TCHAR* ptszSprFilePath)
{
	_tcsncpy_s(m_tszSprFilelocation, _countof(m_tszSprFilelocation), ptszSprFilePath, _TRUNCATE);
}

//***************************************************************************
//
void CDrawImage::Create(int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType)
{
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_nCanvasBit = nCanvasBit;
	m_nAlphaBit = nAlphaBit;
	m_nTotalLayer = nTotalLayer;
	m_nImageType = nImageType;
}

//***************************************************************************
//
void CDrawImage::SetCanvasInfo(const TCHAR* ptszSprFilePath, int nWidth, int nHeight, int nCanvasBit, int nAlphaBit, int nTotalLayer, int nImageType)
{
	Init(ptszSprFilePath);
	Create(nWidth, nHeight, nCanvasBit, nAlphaBit, nTotalLayer, nImageType);
}

//***************************************************************************
//	
BOOL CDrawImage::SetCanvas(CItemInfoList*& pItemInfoList, CCanvas& Canvas)
{
	int		nRetcode = 0;
	int		nAdjustX, nAdjustY;
	TCHAR   tszFileNameExt[FILENAMEEXT_STRLEN];

	CSprite		Sprite;

	nAdjustX = AVATARITEM_SPRITE_IMAGE_ADJUSTX;
	nAdjustY = AVATARITEM_SPRITE_IMAGE_ADJUSTY;
	for( int i = 0; i < m_nTotalLayer; i++ )
	{
		_tcsncpy_s(tszFileNameExt, _countof(tszFileNameExt), pItemInfoList[i].m_ItemImageInfo.m_tszSprFileName, _TRUNCATE);
		if( tszFileNameExt[0] != '\0' )
		{
			nRetcode = Sprite.Load(m_tszSprFilelocation, tszFileNameExt);
			if( nRetcode > -1 )
			{
				Canvas.DrawImage(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
				Canvas.DrawAlpha(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
			}
			else
			{
				LOG_ERROR(_T(" # SPRITE FILE LOADING ERROR(TOTAL LAYER) #, %d, %s, %s, ReturnCode = %d "), i, m_tszSprFilelocation, tszFileNameExt, nRetcode);
			}
		}
	}

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::SetCanvas(CItemInfoList*& pItemInfoList, CCanvas& Canvas, int nLayerExpr)
{
	int		nRetcode = 0;
	int		nAdjustX, nAdjustY;
	TCHAR   tszFileNameExt[FILENAMEEXT_STRLEN];

	CSprite		Sprite;

	nAdjustX = AVATARITEM_SPRITE_IMAGE_ADJUSTX;
	nAdjustY = AVATARITEM_SPRITE_IMAGE_ADJUSTY;
	if( nLayerExpr == AVATARITEM_IMAGE_TOTAL_LAYER )
	{
		for( int i = 0; i < m_nTotalLayer; i++ )
		{
			_tcsncpy_s(tszFileNameExt, _countof(tszFileNameExt), pItemInfoList[i].m_ItemImageInfo.m_tszSprFileName, _TRUNCATE);
			if( tszFileNameExt[0] != '\0' )
			{
				nRetcode = Sprite.Load(m_tszSprFilelocation, tszFileNameExt);
				if( nRetcode > -1 )
				{
					Canvas.DrawImage(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
					Canvas.DrawAlpha(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
				}
				else
				{

					LOG_ERROR(_T(" # SPRITE FILE LOADING ERROR(TOTAL LAYER) #, %d, %s, %s, ReturnCode = %d "), i, m_tszSprFilelocation, tszFileNameExt, nRetcode);
				}
			}
		}
	}
	else if( nLayerExpr == AVATARITEM_IMAGE_ABOVE_LAYER )
	{
		for( int i = 0; i < AVATARITEM_IMAGE_LAYER_EXPRESSION; i++ )
		{
			_tcsncpy_s(tszFileNameExt, _countof(tszFileNameExt), pItemInfoList[i].m_ItemImageInfo.m_tszSprFileName, _TRUNCATE);
			if( tszFileNameExt[0] != '\0' )
			{
				nRetcode = Sprite.Load(m_tszSprFilelocation, tszFileNameExt);
				if( nRetcode > -1 )
				{
					Canvas.DrawImage(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
					Canvas.DrawAlpha(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
				}
				else
				{

					LOG_ERROR(_T(" # SPRITE FILE LOADING ERROR(ABOVE LAYER) #, %d, %s, %s, ReturnCode = %d "), i, m_tszSprFilelocation, tszFileNameExt, nRetcode);
				}
			}
		}
	}
	else if( nLayerExpr == AVATARITEM_IMAGE_EXPR_LAYER )
	{
		int i = AVATARITEM_IMAGE_LAYER_EXPRESSION;
		_tcsncpy_s(tszFileNameExt, _countof(tszFileNameExt), pItemInfoList[i].m_ItemImageInfo.m_tszSprFileName, _TRUNCATE);
		if( tszFileNameExt[0] != '\0' )
		{
			nRetcode = Sprite.Load(m_tszSprFilelocation, tszFileNameExt);
			if( nRetcode > -1 )
			{
				Canvas.DrawImage(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
				Canvas.DrawAlpha(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
			}
			else
			{
				LOG_ERROR(_T(" # SPRITE FILE LOADING ERROR(EXPR LAYER) #, %d, %s, %s, ReturnCode = %d "), i, m_tszSprFilelocation, tszFileNameExt, nRetcode);
			}
		}
	}
	else if( nLayerExpr == AVATARITEM_IMAGE_UNDER_LAYER )
	{
		for( int i = AVATARITEM_IMAGE_LAYER_EXPRESSION + 1; i < m_nTotalLayer; i++ )
		{
			_tcsncpy_s(tszFileNameExt, _countof(tszFileNameExt), pItemInfoList[i].m_ItemImageInfo.m_tszSprFileName, _TRUNCATE);
			if( tszFileNameExt[0] != '\0' )
			{
				nRetcode = Sprite.Load(m_tszSprFilelocation, tszFileNameExt);
				if( nRetcode > -1 )
				{
					Canvas.DrawImage(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
					Canvas.DrawAlpha(&Sprite, nAdjustX, nAdjustY, pItemInfoList[i].m_ItemImageInfo.m_nPos);
				}
				else
				{
					LOG_ERROR(_T(" # SPRITE FILE LOADING ERROR(UNDER LAYER) #, %d, %s, %s, ReturnCode = %d "), i, m_tszSprFilelocation, tszFileNameExt, nRetcode);
				}
			}
		}
	}

	return true;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int nLayerExpr, TCHAR* tszDirection, int nCharacter, int nExprImageNum, int* pnItemNumber, int nItemCount, RECT rRect, int nWidth, int nHeight)
{
	BOOL		bResult = false;

	CCharacter				Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	bResult = Draw(nLayerExpr, tszDirection, nCharacter, nExprImageNum, rRect, nWidth, nHeight, pItemInfoList);

	if( pItemInfoList ) delete[]pItemInfoList;

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, CImageInfo& ImageInfo)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(nCharacter);
	Character.SetExpression(nExpression);

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	bResult = Draw(nLayerExpr, nDrawType, tszDirection, pItemInfoList, ImageInfo);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, BYTE*& pbBuffer, int& nBufferSize)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(nCharacter);
	Character.SetExpression(nExpression);

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	bResult = Draw(nLayerExpr, nDrawType, tszDirection, pItemInfoList, pbBuffer, nBufferSize);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CImageInfo& ImageInfo)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(DrawItemInfo.m_nCharacter);
	Character.SetExpression(DrawItemInfo.m_nExpression);

	if( !Character.SetEquipments(DrawItemInfo.m_nItemNumber, DrawItemInfo.m_nItemCount, pItemInfoList) )
		return false;

	bResult = Draw(DrawItemInfo, pItemInfoList, ImageInfo);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, BYTE*& pbBuffer, int& nBufferSize)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(DrawItemInfo.m_nCharacter);
	Character.SetExpression(DrawItemInfo.m_nExpression);

	if( !Character.SetEquipments(DrawItemInfo.m_nItemNumber, DrawItemInfo.m_nItemCount, pItemInfoList) )
		return false;

	bResult = Draw(DrawItemInfo, pItemInfoList, pbBuffer, nBufferSize);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, CImageInfo& ImageInfo)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(0);
	Character.SetExpression(0);
	Character.SetEquipments(pnSrcItem, nSrcSize, pItemInfoList);

	bResult = Draw(pItemInfoList, ImageInfo);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, BYTE*& pbBuffer, int& nBufferSize)
{
	BOOL		bResult = false;

	CCharacter			Character;
	CItemInfoList* pItemInfoList = NULL;

	pItemInfoList = new CItemInfoList[m_nTotalLayer];

	memset(pItemInfoList, 0, sizeof(CItemInfoList) * m_nTotalLayer);

	Character.SetCharacter(0);
	Character.SetExpression(0);
	Character.SetEquipments(pnSrcItem, nSrcSize, pItemInfoList);

	bResult = Draw(pItemInfoList, pbBuffer, nBufferSize);

	if( pItemInfoList )
	{
		delete[]pItemInfoList;
		pItemInfoList = NULL;
	}

	return bResult;
}

//***************************************************************************
// CCanvas::Encode()는 PNG로 인코딩한다(GifCodec은 디코드 전용이라 저장 불가).
// 과거 CxImage 기반 GIF 저장 경로를 PNG 직접 저장으로 교체 — 확장자도 .gif -> .png.
//***************************************************************************
BOOL CDrawImage::Draw(int nLayerExpr, TCHAR* tszDirection, int nCharacter, int nExprImageNum, RECT rRect, int nWidth, int nHeight, CItemInfoList*& pItemInfoList)
{
	BYTE* pbBuffer = NULL;
	TCHAR		tszFullpath[FULLPATH_STRLEN];
	TCHAR		tszDirectory[DIRECTORY_STRLEN];

	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas, nLayerExpr) ) return false;

	Canvas.Crop(rRect);
	Canvas.Resize(nWidth, nHeight);

	if( nWidth == 36 )
	{
		_sntprintf_s(tszDirectory, _countof(tszDirectory), _TRUNCATE, _T("%s\\%d\\Reduce\\"), m_tszSprFilelocation, nCharacter);
		CreateDirectoryRecursive(tszDirectory);
	}
	else if( nWidth == 16 )
	{
		_sntprintf_s(tszDirectory, _countof(tszDirectory), _TRUNCATE, _T("%s\\%d\\Icon\\"), m_tszSprFilelocation, nCharacter);
		CreateDirectoryRecursive(tszDirectory);
	}
	else
	{
		_sntprintf_s(tszDirectory, _countof(tszDirectory), _TRUNCATE, _T("%s\\%d\\Normal\\"), m_tszSprFilelocation, nCharacter);
		CreateDirectoryRecursive(tszDirectory);
	}

	if( tszDirection[0] == AVATARITEM_IMAGE_RIGHT_DIRECTION )
	{
		_sntprintf_s(tszFullpath, _countof(tszFullpath), _TRUNCATE, _T("%s\\%dR.png"), tszDirectory, nExprImageNum);
		Canvas.MirrorImage();
	}
	else _sntprintf_s(tszFullpath, _countof(tszFullpath), _TRUNCATE, _T("%s\\%dL.png"), tszDirectory, nExprImageNum);

	Canvas.FilterImage();

	int nFilelen = Canvas.Encode(pbBuffer);

	std::ofstream outFile(tszFullpath, std::ios::binary);
	if( outFile ) outFile.write(reinterpret_cast<const char*>(pbBuffer), nFilelen);

	free(pbBuffer);

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(int nLayerExpr, int nDrawType, TCHAR* tszDirection, CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo)
{
	BYTE* pbBuffer = NULL;

	RECT		rRect;
	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas, nLayerExpr) ) return false;

	if( nDrawType == DRAW_TYPE_BUST )
	{
		rRect.top = AVATARITEM_DRAW_BUST_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_BUST_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_BUST_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_BUST_CROP_RIGHT;

		Canvas.Crop(rRect);
	}
	else if( nDrawType == DRAW_TYPE_FACE )
	{
		rRect.top = AVATARITEM_DRAW_FACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_FACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_FACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_FACE_CROP_RIGHT;

		Canvas.Crop(rRect);
	}
	else if( nDrawType == DRAW_TYPE_REDUCEFACE )
	{
		rRect.top = AVATARITEM_DRAW_REDUCEFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_REDUCEFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_REDUCEFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_REDUCEFACE_CROP_RIGHT;

		Canvas.Crop(rRect);
		Canvas.Resize(AVATARITEM_DRAW_REDUCEFACE_RESIZE_WIDTH, AVATARITEM_DRAW_REDUCEFACE_RESIZE_HEIGHT);
	}
	else if( nDrawType == DRAW_TYPE_ICONFACE )
	{
		rRect.top = AVATARITEM_DRAW_ICONFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_ICONFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_ICONFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_ICONFACE_CROP_RIGHT;

		Canvas.Crop(rRect);
		Canvas.Resize(AVATARITEM_DRAW_ICONFACE_RESIZE_WIDTH, AVATARITEM_DRAW_ICONFACE_RESIZE_HEIGHT);
	}

	if( tszDirection[0] == AVATARITEM_IMAGE_RIGHT_DIRECTION )
		Canvas.MirrorImage();

	Canvas.FilterImage();

	int nFilelen = Canvas.Encode(pbBuffer);

	memcpy(&ImageInfo.m_CacheMemInfo.m_bBuffer, pbBuffer, sizeof(ImageInfo.m_CacheMemInfo.m_bBuffer));
	ImageInfo.m_CacheMemInfo.m_nBufSize = nFilelen;

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(int nLayerExpr, int nDrawType, TCHAR* tszDirection, CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize)
{
	BOOL	bCrop = true;
	BOOL    bResizing = true;

	RECT		rRect;
	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas, nLayerExpr) ) return false;

	if( nDrawType == DRAW_TYPE_BUST )
	{
		rRect.top = AVATARITEM_DRAW_BUST_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_BUST_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_BUST_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_BUST_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_FACE )
	{
		rRect.top = AVATARITEM_DRAW_FACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_FACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_FACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_FACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_REDUCEFACE )
	{
		rRect.top = AVATARITEM_DRAW_REDUCEFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_REDUCEFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_REDUCEFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_REDUCEFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_REDUCEFACE_RESIZE_WIDTH, AVATARITEM_DRAW_REDUCEFACE_RESIZE_HEIGHT) ) bResizing = false;
	}
	else if( nDrawType == DRAW_TYPE_ICONFACE )
	{
		rRect.top = AVATARITEM_DRAW_ICONFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_ICONFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_ICONFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_ICONFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_ICONFACE_RESIZE_WIDTH, AVATARITEM_DRAW_ICONFACE_RESIZE_HEIGHT) ) bResizing = false;
	}

	if( !bCrop )
	{
		LOG_ERROR(_T(" # Crop Process Error #"));
	}

	if( !bResizing )
	{
		LOG_ERROR(_T(" # Resizing Process Error #"));
	}

	if( tszDirection[0] == AVATARITEM_IMAGE_RIGHT_DIRECTION )
	{
		if( !Canvas.MirrorImage() )
		{
			LOG_ERROR(_T(" # MirrorImage Process Error #"));
		}
	}

	Canvas.FilterImage();

	nBufferSize = Canvas.Encode(pbBuffer);

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo)
{
	BOOL	bCrop = true;
	BOOL    bResizing = true;
	int		nDrawType = 0;

	BYTE* pbBuffer = NULL;

	RECT		rRect;
	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas, DrawItemInfo.m_nLayerExpr) ) return false;

	nDrawType = DrawItemInfo.m_nDrawType;
	if( nDrawType == DRAW_TYPE_BUST )
	{
		rRect.top = AVATARITEM_DRAW_BUST_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_BUST_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_BUST_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_BUST_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_FACE )
	{
		rRect.top = AVATARITEM_DRAW_FACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_FACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_FACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_FACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_REDUCEFACE )
	{
		rRect.top = AVATARITEM_DRAW_REDUCEFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_REDUCEFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_REDUCEFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_REDUCEFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_REDUCEFACE_RESIZE_WIDTH, AVATARITEM_DRAW_REDUCEFACE_RESIZE_HEIGHT) ) bResizing = false;
	}
	else if( nDrawType == DRAW_TYPE_ICONFACE )
	{
		rRect.top = AVATARITEM_DRAW_ICONFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_ICONFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_ICONFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_ICONFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_ICONFACE_RESIZE_WIDTH, AVATARITEM_DRAW_ICONFACE_RESIZE_HEIGHT) ) bResizing = false;
	}

	if( !bCrop )
	{
		LOG_ERROR(_T(" # Crop Process Error #, QueryString = %s"), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL);
	}

	if( !bResizing )
	{
		LOG_ERROR(_T(" # Resizing Process Error #, QueryString = %s"), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL);
	}

	if( DrawItemInfo.m_tszDirection[0] == AVATARITEM_IMAGE_RIGHT_DIRECTION )
	{
		if( !Canvas.MirrorImage() )
		{
			LOG_ERROR(_T(" # MirrorImage Process Error #, QueryString = %s"), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL);
		}
	}

	Canvas.FilterImage();

	int nFileLen = Canvas.Encode(pbBuffer);

	if( nFileLen <= AVATARITEM_AVATAR_IMAGE_BUFFER_SIZE )
	{
		memcpy(&ImageInfo.m_CacheMemInfo.m_bBuffer, pbBuffer, nFileLen);
		ImageInfo.m_CacheMemInfo.m_nBufSize = nFileLen;
	}
	else
	{
		LOG_ERROR(_T(" # Image Size Error #, QueryString = %s, ImageSize = %d "), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL, nFileLen);
	}

	free(pbBuffer);

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize)
{
	BOOL	bCrop = true;
	BOOL    bResizing = true;
	int		nDrawType = 0;

	RECT		rRect;
	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas, DrawItemInfo.m_nLayerExpr) ) return false;

	nDrawType = DrawItemInfo.m_nDrawType;
	if( nDrawType == DRAW_TYPE_BUST )
	{
		rRect.top = AVATARITEM_DRAW_BUST_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_BUST_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_BUST_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_BUST_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_FACE )
	{
		rRect.top = AVATARITEM_DRAW_FACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_FACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_FACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_FACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
	}
	else if( nDrawType == DRAW_TYPE_REDUCEFACE )
	{
		rRect.top = AVATARITEM_DRAW_REDUCEFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_REDUCEFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_REDUCEFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_REDUCEFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_REDUCEFACE_RESIZE_WIDTH, AVATARITEM_DRAW_REDUCEFACE_RESIZE_HEIGHT) ) bResizing = false;
	}
	else if( nDrawType == DRAW_TYPE_ICONFACE )
	{
		rRect.top = AVATARITEM_DRAW_ICONFACE_CROP_TOP;
		rRect.bottom = AVATARITEM_DRAW_ICONFACE_CROP_BOTTOM;
		rRect.left = AVATARITEM_DRAW_ICONFACE_CROP_LEFT;
		rRect.right = AVATARITEM_DRAW_ICONFACE_CROP_RIGHT;

		if( !Canvas.Crop(rRect) ) bCrop = false;
		if( !Canvas.Resize(AVATARITEM_DRAW_ICONFACE_RESIZE_WIDTH, AVATARITEM_DRAW_ICONFACE_RESIZE_HEIGHT) ) bResizing = false;
	}

	if( !bCrop )
	{
		LOG_ERROR(_T(" # Crop Process Error #"));
	}

	if( !bResizing )
	{
		LOG_ERROR(_T(" # Resizing Process Error #"));
	}

	if( DrawItemInfo.m_tszDirection[0] == AVATARITEM_IMAGE_RIGHT_DIRECTION )
	{
		if( !Canvas.MirrorImage() )
		{
			LOG_ERROR(_T(" # MirrorImage Process Error #"));
		}
	}

	Canvas.FilterImage();

	nBufferSize = Canvas.Encode(pbBuffer);

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(CItemInfoList*& pItemInfoList, CImageInfo& ImageInfo)
{
	BYTE* pbBuffer = NULL;

	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas) ) return false;

	Canvas.FilterImage();

	int nFileLen = Canvas.Encode(pbBuffer);

	if( nFileLen <= MAX_BUFFER_SIZE )
	{
		memcpy(&ImageInfo.m_CacheMemInfo.m_bBuffer, pbBuffer, nFileLen);
		ImageInfo.m_CacheMemInfo.m_nBufSize = nFileLen;
	}
	else
	{
		LOG_ERROR(_T(" # Image Size Error #, QueryString = %s, ImageSize = %d "), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL, nFileLen);
	}

	free(pbBuffer);

	return true;
}

//***************************************************************************
//	
BOOL CDrawImage::Draw(CItemInfoList*& pItemInfoList, BYTE*& pbBuffer, int& nBufferSize)
{
	CCanvas		Canvas;

	Canvas.Init(m_nWidth, m_nHeight);

	if( !SetCanvas(pItemInfoList, Canvas) ) return false;

	Canvas.FilterImage();

	nBufferSize = Canvas.Encode(pbBuffer);

	return true;
}