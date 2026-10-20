//***************************************************************************
// DrawImage.cpp: implementation of the CDrawImage class.
//
//***************************************************************************

#include "pch.h"
#include "DrawImage.h"

#include <fstream>
#include <memory>
#include <new>

namespace
{
	// 레이어 배열 소유자(어느 경로로 빠져도 자동 해제)
	using LayerArray = std::unique_ptr<CItemInfoList[]>;

	//***************************************************************************
	// @brief 레이어 배열을 0으로 초기화해 할당
	// @param nCount 레이어 개수(SetCanvasInfo로 설정된 전체 레이어 수)
	// @return 할당된 배열. nCount가 올바르지 않거나 할당에 실패하면 null
	//***************************************************************************
	LayerArray AllocLayers(int nCount)
	{
		if( nCount <= 0 )
		{
			LOG_ERROR(_T(" # CDrawImage canvas is not set (TotalLayer = %d) #"), nCount);
			return nullptr;
		}

		LayerArray layers(new(std::nothrow) CItemInfoList[nCount]);
		if( !layers )
		{
			LOG_ERROR(_T(" # CItemInfoList allocation failed (TotalLayer = %d) #"), nCount);
			return nullptr;
		}

		memset(layers.get(), 0, sizeof(CItemInfoList) * nCount);
		return layers;
	}

	//***************************************************************************
	// @brief 인코딩된 이미지를 ImageInfo의 캐시 버퍼로 복사
	// @param pbBuffer 인코딩된 이미지 데이터
	// @param nLen 인코딩된 이미지 길이(바이트)
	// @param ImageInfo 복사 대상
	// @return 복사에 성공하면 TRUE. 인코딩 실패이거나 캐시 버퍼보다 크면 FALSE
	//***************************************************************************
	BOOL CopyToImageInfo(const BYTE* pbBuffer, int nLen, CImageInfo& ImageInfo)
	{
		ImageInfo.m_CacheMemInfo.m_nBufSize = 0;

		if( pbBuffer == NULL || nLen <= 0 )
		{
			LOG_ERROR(_T(" # Image Encode Error #, QueryString = %s, ImageSize = %d "), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL, nLen);
			return FALSE;
		}

		if( nLen > static_cast<int>(sizeof(ImageInfo.m_CacheMemInfo.m_bBuffer)) )
		{
			LOG_ERROR(_T(" # Image Size Error #, QueryString = %s, ImageSize = %d "), ImageInfo.m_CacheMemInfo.m_tszRequestImageURL, nLen);
			return FALSE;
		}

		memcpy(ImageInfo.m_CacheMemInfo.m_bBuffer, pbBuffer, nLen);
		ImageInfo.m_CacheMemInfo.m_nBufSize = nLen;
		return TRUE;
	}
}

//***************************************************************************
// Construction/Destruction
//***************************************************************************

CDrawImage::CDrawImage()
	: m_nWidth(0), m_nHeight(0), m_nCanvasBit(0), m_nAlphaBit(0), m_nTotalLayer(0), m_nImageType(0)
{
	m_tszSprFilelocation[0] = _T('\0');
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
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	return Draw(nLayerExpr, tszDirection, nCharacter, nExprImageNum, rRect, nWidth, nHeight, pItemInfoList);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, CImageInfo& ImageInfo)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(nCharacter);
	Character.SetExpression(nExpression);

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	return Draw(nLayerExpr, nDrawType, tszDirection, pItemInfoList, ImageInfo);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int nLayerExpr, int nDrawType, TCHAR* tszDirection, int nCharacter, int nExpression, int* pnItemNumber, int nItemCount, BYTE*& pbBuffer, int& nBufferSize)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(nCharacter);
	Character.SetExpression(nExpression);

	if( !Character.SetEquipments(pnItemNumber, nItemCount, pItemInfoList) )
		return false;

	return Draw(nLayerExpr, nDrawType, tszDirection, pItemInfoList, pbBuffer, nBufferSize);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, CImageInfo& ImageInfo)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(DrawItemInfo.m_nCharacter);
	Character.SetExpression(DrawItemInfo.m_nExpression);

	if( !Character.SetEquipments(DrawItemInfo.m_nItemNumber, DrawItemInfo.m_nItemCount, pItemInfoList) )
		return false;

	return Draw(DrawItemInfo, pItemInfoList, ImageInfo);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(AVATAR_DRAWING_ITEM_INFO DrawItemInfo, BYTE*& pbBuffer, int& nBufferSize)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(DrawItemInfo.m_nCharacter);
	Character.SetExpression(DrawItemInfo.m_nExpression);

	if( !Character.SetEquipments(DrawItemInfo.m_nItemNumber, DrawItemInfo.m_nItemCount, pItemInfoList) )
		return false;

	return Draw(DrawItemInfo, pItemInfoList, pbBuffer, nBufferSize);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, CImageInfo& ImageInfo)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(0);
	Character.SetExpression(0);
	Character.SetEquipments(pnSrcItem, nSrcSize, pItemInfoList);

	return Draw(pItemInfoList, ImageInfo);
}

//*************************************************************************
//
BOOL CDrawImage::SetItemInfoMemDB(int* pnSrcItem, int nSrcSize, BYTE*& pbBuffer, int& nBufferSize)
{
	LayerArray layers = AllocLayers(m_nTotalLayer);
	if( !layers ) return false;

	CItemInfoList* pItemInfoList = layers.get();

	CCharacter	Character;

	Character.SetCharacter(0);
	Character.SetExpression(0);
	Character.SetEquipments(pnSrcItem, nSrcSize, pItemInfoList);

	return Draw(pItemInfoList, pbBuffer, nBufferSize);
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
	if( pbBuffer == NULL || nFilelen <= 0 )
	{
		LOG_ERROR(_T(" # Image Encode Error #, %s, ImageSize = %d "), tszFullpath, nFilelen);
		return false;
	}

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

	// 인코딩된 길이만큼만 복사한다(실패/초과 시 FALSE).
	BOOL bResult = CopyToImageInfo(pbBuffer, nFilelen, ImageInfo);

	if( pbBuffer ) free(pbBuffer);

	return bResult;
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

	return pbBuffer != NULL && nBufferSize > 0;
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

	// 인코딩된 길이만큼만 복사한다(실패/초과 시 FALSE).
	BOOL bResult = CopyToImageInfo(pbBuffer, nFileLen, ImageInfo);

	if( pbBuffer ) free(pbBuffer);

	return bResult;
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

	return pbBuffer != NULL && nBufferSize > 0;
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

	// 인코딩된 길이만큼만 복사한다(실패/초과 시 FALSE).
	BOOL bResult = CopyToImageInfo(pbBuffer, nFileLen, ImageInfo);

	if( pbBuffer ) free(pbBuffer);

	return bResult;
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

	return pbBuffer != NULL && nBufferSize > 0;
}