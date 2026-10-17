
//***************************************************************************
// AvatarImageStream.h : interface for the AvatarImageStream.
//
//***************************************************************************

#ifndef AVATARIMAGESTREAM_H
#define AVATARIMAGESTREAM_H

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_DRESSITEM_INFO
{
	int				m_nItemNumber;									// 아이템 번호
	int				m_nCirculation;									// 유통(생성) 번호
	int				m_nSubType;										// 보조 타입(DressItemSubType)
	TCHAR			m_tszItemName[AVATARITEM_ITEM_NAME_LEN];		// 아이템 이름
	TCHAR			m_tszGender[AVATARITEM_ITEM_GENDER_LEN];		// 성별
	int				m_nCoverageMask;								// 장착 부위 마스크
	int				m_nExcludeMask;									// 장착 불가 마스크
	int				m_nLinkImage[7];								// 연결 이미지 번호 목록
	int				m_nLinkImageMask;								// 연결 이미지 마스크

} AVATAR_IMAGE_DRESSITEM_INFO, * PAVATAR_IMAGE_DRESSITEM_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_DRESSITEM_IMAGE_INFO
{
	int				m_nImageNumber;									// 이미지 번호
	TCHAR			m_tszFileNumber[3];								// 스프라이트 파일 번호(문자열, 2자리)
	int				m_nSprNum;										// 스프라이트 인덱스
	int				m_nImageLayer;									// 이미지 레이어
	TCHAR			m_tszImageDesc[MAX_BUFFER_SIZE];				// 이미지 설명

} AVATAR_IMAGE_DRESSITEM_IMAGE_INFO, * PAVATAR_IMAGE_DRESSITEM_IMAGE_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_CHARACTER_INFO
{
	int		m_nChrNumber;									// 캐릭터 번호
	TCHAR	m_tszChrName[AVATARITEM_CHARACTER_NAME_LEN];	// 캐릭터 명
	TCHAR	m_tszGender[AVATARITEM_ITEM_GENDER_LEN];		// 성별	
	int		m_nBodyItemNum;									// 몸 아이템 인덱스
	int		m_nHairItemNum;									// 머리 아이템 인덱스
	int		m_nJacketItemNum;								// 상의 아이템 인덱스
	int		m_nPantsItemNum;								// 하의 아이템 인덱스
	int		m_nShadowItemNum;								// 그림자 아이템 인덱스

} AVATAR_IMAGE_CHARACTER_INFO, * PAVATAR_IMAGE_CHARACTER_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_ITEMIMAGE_INFO
{
	int		m_nItemImageSeq;							// 아이템 이미지 정보 시퀀스
	int		m_nItemNumber;								// 아이템 번호
	int		m_nSubType;									// 보조 타입
	TCHAR	m_tszName[AVATARITEM_ITEM_NAME_LEN];		// 아이템 이름
	TCHAR	m_tszGender[AVATARITEM_ITEM_GENDER_LEN];	// 성별	
	int		m_nLinkImageMask;							// 연결 이미지 마스크
	int		m_nCoverageMask;							// 장착 부위
	int		m_nExcludeMask;								// 장착 불가
	int		m_nPos;										// Position of SpriteFile					
	int		m_nLayer;									// Image Layer
	TCHAR   m_tszSprFileName[FILENAMEEXT_STRLEN];		// 일반 파일 이름

} AVATAR_IMAGE_ITEMIMAGE_INFO, * PAVATAR_IMAGE_ITEMIMAGE_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_EXPRESSIONIMAGE_INFO
{
	int		m_nItemNumber;									// 아이템 번호
	int		m_nChrNumber;									// 캐릭터 번호
	int		m_nExprNumber;									// 표정 번호
	TCHAR	m_tszExprName[AVATARITEM_EXPRESSION_NAME_LEN];	// 표정명

} AVATAR_IMAGE_EXPRESSIONIMAGE_INFO, * PAVATAR_IMAGE_EXPRESSIONIMAGE_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_SPRITE_FILE_INFO
{
	int				m_nFileIndex;										// 스프라이트 파일 인덱스(조회 키)
	TCHAR			m_tszFileName[AVATARITEM_SPRITE_FILENAME_LEN];		// 파일 이름(예: "0.spr")
	TCHAR			m_tszFileTheme[AVATARITEM_SPRITE_FILETHEME_LEN];	// 테마
	int				m_nSpriteCount;										// 스프라이트 개수
	TCHAR			m_tszFileDesc[AVATARITEM_SPRITE_FILEDESC_LEN];		// 파일 설명

} AVATAR_IMAGE_SPRITE_FILE_INFO, * PAVATAR_IMAGE_SPRITE_FILE_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_TEMP_MEMORY_INFO
{
	BYTE    m_bBuffer[MAX_TEMP_BUFFER_SIZE];
	int		m_nBufSize;

} AVATAR_IMAGE_TEMP_MEMORY_INFO, * PAVATAR_IMAGE_TEMP_MEMORY_INFO;

//***************************************************************************
//	
typedef struct _AVATAR_IMAGE_CACHE_MEMORY_INFO
{
	TCHAR	m_tszRequestImageURL[MAX_BUFFER_SIZE];

	BYTE    m_bBuffer[MAX_TEMP_BUFFER_SIZE];
	int		m_nBufSize;

} AVATAR_IMAGE_CACHE_MEMORY_INFO, * PAVATAR_IMAGE_CACHE_MEMORY_INFO;

//***************************************************************************
//
typedef struct _AVATAR_DRAWING_ITEM_INFO
{
	int		m_nLayerExpr;
	int		m_nDrawType;
	TCHAR	m_tszDirection[AVATARITEM_QUERYSTRING_DIRECTION_STR_LEN];
	int		m_nCharacter;
	int		m_nExpression;
	int		m_nItemNumber[AVATARITEM_MAX_EQUIPMENTS];
	int		m_nItemCount;
	TCHAR	m_tszRequestImageURL[REQUEST_URL_STRLEN];

} AVATAR_DRAWING_ITEM_INFO, * PAVATAR_DRAWING_ITEM_INFO;

#endif // _ndef AVATARIMAGESTREAM_H
