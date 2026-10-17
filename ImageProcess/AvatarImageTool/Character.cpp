
//***************************************************************************
// Character.cpp: implementation of the CCharacter class.
//
//***************************************************************************

#include "pch.h"
#include "Character.h"

//***************************************************************************
// Construction/Destruction
//***************************************************************************

CCharacter::CCharacter()
{
}

CCharacter::~CCharacter()
{
}

//***************************************************************************
//
void CCharacter::SetCharacter(int nCharacter)
{
	m_nCharacter = nCharacter;
}

//***************************************************************************
//
void CCharacter::SetExpression(int nExpression)
{
	m_nExpression = nExpression;
}

//***************************************************************************
// 0 루시안 1 조슈아 2 막시민 3 보리스 4 란지에   5 시벨린 6 이자크 7 마네킨
// 0 이스핀 1 티치엘 2 클로에 3 레이   4 아나이스 5 밀라   6   벤야 7 마네킨
BOOL CCharacter::SetEquipments(int* pnItemImageInfo, int nNum, CItemInfoList*& pItemInfoList)
{
	BOOL	bHair = false;
	BOOL	bJacket = false;
	BOOL	bPants = false;
	BOOL	bCheck = false;
	BYTE	bHairMask = 0x00;
	BYTE	bHairMaskTemp = 0x00;
	int		nSubtype = -1;
	int		nCoverageMask = -1;
	int		nExcludeMask = -1;
	TCHAR	tszItemName[AVATARITEM_ITEM_NAME_LEN];

	CCharacterInfo		CharacterInfo;
	CExpressionInfo		ExpressionInfo;

	GetCharacterInfo(m_nCharacter, CharacterInfo);

	if( m_nCharacter > 0 && m_nExpression > 0 )
		GetExprImageInfo(m_nCharacter, m_nExpression, ExpressionInfo);

	// Shadow
	if( CharacterInfo.m_CharacterInfo.m_nShadowItemNum > 0 )
		GetItemImageInfo(CharacterInfo.m_CharacterInfo.m_nShadowItemNum, pItemInfoList);
	// Body
	GetItemImageInfo(CharacterInfo.m_CharacterInfo.m_nBodyItemNum, pItemInfoList);

	// General Expression
	if( ExpressionInfo.m_ExpressionInfo.m_nItemNumber > 0 )
		GetItemImageInfo(ExpressionInfo.m_ExpressionInfo.m_nItemNumber, pItemInfoList);

	for( int i = 0; i < nNum; i++ )
	{
		GetItemImageInfo(pnItemImageInfo[i], pItemInfoList);

		bHairMaskTemp = 0x00;
		bCheck = g_ItemInfoListMem.GetImageInfo(pnItemImageInfo[i], tszItemName, bHairMaskTemp, nSubtype, nCoverageMask, nExcludeMask);

		if( bCheck )
		{
			if( nSubtype == DSIST_WIG )
			{
				int nTempCoverageMask = g_ItemInfoListMem.GetCoverageMask(CharacterInfo.m_CharacterInfo.m_nHairItemNum);
				int	nTempExcludeMask = g_ItemInfoListMem.GetExcludeMask(CharacterInfo.m_CharacterInfo.m_nHairItemNum);

				int nMask = (nTempCoverageMask | nTempExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bHair = true;

				nMask = (nCoverageMask | nExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bHair = true;
			}
			else if( nSubtype == DSIST_JACKET )
			{
				int nTempCoverageMask = g_ItemInfoListMem.GetCoverageMask(CharacterInfo.m_CharacterInfo.m_nJacketItemNum);
				int	nTempExcludeMask = g_ItemInfoListMem.GetExcludeMask(CharacterInfo.m_CharacterInfo.m_nJacketItemNum);

				int nMask = (nTempCoverageMask | nTempExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bJacket = true;

				nMask = (nCoverageMask | nExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bJacket = true;

				nTempCoverageMask = g_ItemInfoListMem.GetCoverageMask(CharacterInfo.m_CharacterInfo.m_nPantsItemNum);
				nTempExcludeMask = g_ItemInfoListMem.GetExcludeMask(CharacterInfo.m_CharacterInfo.m_nPantsItemNum);

				nMask = (nTempCoverageMask | nTempExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bPants = true;

				nMask = (nCoverageMask | nExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bPants = true;
			}
			else if( nSubtype == DSIST_PANTS )
			{
				int nTempCoverageMask = g_ItemInfoListMem.GetCoverageMask(CharacterInfo.m_CharacterInfo.m_nPantsItemNum);
				int	nTempExcludeMask = g_ItemInfoListMem.GetExcludeMask(CharacterInfo.m_CharacterInfo.m_nPantsItemNum);

				int nMask = (nTempCoverageMask | nTempExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bPants = true;

				nMask = (nCoverageMask | nExcludeMask);

				if( (nCoverageMask & nMask) != 0 ) bPants = true;
			}
			else if( nSubtype == DSIST_BODY )
			{
				if( !_tcsstr(tszItemName, AVATARITEM_SHADOW_BODY_TOKEN) &&
					!_tcsstr(tszItemName, AVATARITEM_SUNTEN_BODY_TOKEN) )
				{
					LOG_ERROR(_T(" # Body Item Overlapping Error #, Avatar = %d, Expression = %d, Subtype = %d, ItemNumber = %d "),
						m_nCharacter, m_nExpression, nSubtype, pnItemImageInfo[i]);
					return false;
				}
			}
			else if( nSubtype == DSIST_EXPRESSION )
			{
				LOG_ERROR(_T(" # Expression Item Overlapping Error #, Avatar = %d, Expression = %d, Subtype = %d, ItemNumber = %d "),
					m_nCharacter, m_nExpression, nSubtype, pnItemImageInfo[i]);
				return false;
			}

			if( bHairMaskTemp != 0 )
				if( bHairMaskTemp >= bHairMask ) bHairMask = bHairMaskTemp;
		}
		else
		{
			LOG_ERROR(_T(" # Get ItemImage Infomation Error #, Avatar = %d, Expression = %d, ItemNumber = %d "),
				m_nCharacter, m_nExpression, pnItemImageInfo[i]);
		}
	}

	// General Hair
	if( !bHair )
		GetItemImageInfo(CharacterInfo.m_CharacterInfo.m_nHairItemNum, pItemInfoList);
	// General Jacket
	if( !bJacket )
		GetItemImageInfo(CharacterInfo.m_CharacterInfo.m_nJacketItemNum, pItemInfoList);
	// General Pants
	if( !bPants )
		GetItemImageInfo(CharacterInfo.m_CharacterInfo.m_nPantsItemNum, pItemInfoList);

	// LinkImageMask
	if( bHairMask != 0 )
	{
		if( (bHairMask & 0x01) == 0x01 )
			memset(&pItemInfoList[9].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
		if( (bHairMask & 0x02) == 0x02 )
			memset(&pItemInfoList[10].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
		if( (bHairMask & 0x04) == 0x04 )
			memset(&pItemInfoList[35].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
		if( (bHairMask & 0x08) == 0x08 )
			memset(&pItemInfoList[37].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
		if( (bHairMask & 0x10) == 0x10 )
			memset(&pItemInfoList[94].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
		if( (bHairMask & 0x20) == 0x20 )
			memset(&pItemInfoList[100].m_ItemImageInfo, 0, sizeof(AVATAR_IMAGE_ITEMIMAGE_INFO));
	}

	return true;
}

//***************************************************************************
//
BOOL CCharacter::GetCharacterInfo(int nChrNumber, CCharacterInfo& CharacterInfo)
{
	if( g_CharacterInfoMem.Search(nChrNumber, CharacterInfo) < 0 )
	{
		LOG_ERROR(_T(" # GetCharacterInfo Error # , Character Number = %d "), nChrNumber);
		return false;
	}

	return true;
}

//***************************************************************************
//
BOOL CCharacter::GetItemImageInfo(int nItemNumber, CItemInfoList*& pItemInfoList)
{
	if( g_ItemInfoListMem.Search(nItemNumber, pItemInfoList) < 0 )
	{
		LOG_ERROR(_T(" # GetItemImageInfo Error # , ItemNumber = %d "), nItemNumber);
		return false;
	}

	return true;
}

//***************************************************************************
//
BOOL CCharacter::GetExprImageInfo(int nChrNumber, int nExprNumber, CExpressionInfo& ExpressionInfo)
{
	if( g_ExpressionInfoMem.Search(nChrNumber, nExprNumber, ExpressionInfo) < 0 )
	{
		LOG_ERROR(_T(" # GetExprImageInfo Error # , Character Number = %d, Expression Number = %d "), nChrNumber, nExprNumber);
		return false;
	}

	return true;
}