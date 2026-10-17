
//***************************************************************************
// AvatarInfoMemDBEX.h: interface for the CAvatarInfoMemDBEX class.
//
//***************************************************************************

#ifndef AVATARINFOMEMDBEX_H
#define AVATARINFOMEMDBEX_H

#include <Thread/PlatformLock.h>
#include <Util/KeyedTable.h>
#include <Util/TextFieldParser.h>
#include <Util/DelimitedRowFile.h>
#include <Util/FileUtil.h>

#include <tchar.h>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>
#include <utility>
#include <cctype>

//***************************************************************************
//
enum
{	// 그리는 방식
	DRAW_TYPE_WHOLEBODY = 0,	// 전체
	DRAW_TYPE_BUST,				// 흉상			
	DRAW_TYPE_FACE,				// 머리			
	DRAW_TYPE_REDUCEFACE,		// 줄인 머리				
	DRAW_TYPE_ICONFACE,			// 머리 아이콘	
};

//***************************************************************************
//
enum DressItemSubType
{
	DSIST_BODY = 1,					// 몸
	DSIST_EXPRESSION,				// 표정
	DSIST_WIG,						// 가발
	DSIST_CAP,                  	// 모자
	DSIST_JACKET,					// 상의
	DSIST_PANTS,					// 하의
	DSIST_SHOE,						// 신발
	DSIST_WEAPON,					// 무기
	DSIST_DECORATION,				// 장식		
	DSIST_BACKGROUND				// 배경
};

//***************************************************************************
// 행(Row) 클래스
//  - 모두 POD 구조체 멤버 하나만 가지므로 복사/이동은 컴파일러 기본 구현을 사용한다.
//    (사용자 정의 operator= / 소멸자를 제거하여 trivially copyable 유지 -> vector 복사가 memmove)
//  - GetStream(buf, cch): 버퍼 크기를 받는 안전 버전. GetStream(buf)는 MAX_BUFFER_SIZE 가정(호환용).
//***************************************************************************
class CCharacterInfo
{
public:
	void Reset(void)
	{
		m_CharacterInfo = AVATAR_IMAGE_CHARACTER_INFO();
	}

	void GetStream(TCHAR* ptszBuffer, size_t cchBuffer) const
	{
		_sntprintf_s(ptszBuffer, cchBuffer, _TRUNCATE, _T("%d\t%-30s\t%s\t%d\t%d\t%d\t%d\t%d\n"),
			m_CharacterInfo.m_nChrNumber, m_CharacterInfo.m_tszChrName,
			m_CharacterInfo.m_tszGender, m_CharacterInfo.m_nBodyItemNum, m_CharacterInfo.m_nHairItemNum,
			m_CharacterInfo.m_nJacketItemNum, m_CharacterInfo.m_nPantsItemNum, m_CharacterInfo.m_nShadowItemNum);
	}

	void GetStream(TCHAR* ptszBuffer) const { GetStream(ptszBuffer, MAX_BUFFER_SIZE); }

	AVATAR_IMAGE_CHARACTER_INFO	m_CharacterInfo = AVATAR_IMAGE_CHARACTER_INFO();	// 캐릭터 원본 데이터
};

//***************************************************************************
//
class CItemInfoList
{
public:
	void Reset(void)
	{
		m_ItemImageInfo = AVATAR_IMAGE_ITEMIMAGE_INFO();
	}

	void GetStream(TCHAR* ptszBuffer, size_t cchBuffer) const
	{
		_sntprintf_s(ptszBuffer, cchBuffer, _TRUNCATE, _T("%d\t%d\t%d\t%-41s\t%s\t%d\t%-10d\t%-10d\t%d\t%d\t%-30s\n"),
			m_ItemImageInfo.m_nItemImageSeq, m_ItemImageInfo.m_nItemNumber,
			m_ItemImageInfo.m_nSubType, m_ItemImageInfo.m_tszName, m_ItemImageInfo.m_tszGender, m_ItemImageInfo.m_nLinkImageMask,
			m_ItemImageInfo.m_nCoverageMask, m_ItemImageInfo.m_nExcludeMask, m_ItemImageInfo.m_nPos, m_ItemImageInfo.m_nLayer,
			m_ItemImageInfo.m_tszSprFileName);
	}

	void GetStream(TCHAR* ptszBuffer) const { GetStream(ptszBuffer, MAX_BUFFER_SIZE); }

	AVATAR_IMAGE_ITEMIMAGE_INFO	m_ItemImageInfo = AVATAR_IMAGE_ITEMIMAGE_INFO();	// 아이템 이미지 원본 데이터
};

//***************************************************************************
//
class CExpressionInfo
{
public:
	void Reset(void)
	{
		m_ExpressionInfo = AVATAR_IMAGE_EXPRESSIONIMAGE_INFO();
	}

	void GetStream(TCHAR* ptszBuffer, size_t cchBuffer) const
	{
		_sntprintf_s(ptszBuffer, cchBuffer, _TRUNCATE, _T("%d\t%d\t%d\t%s\n"),
			m_ExpressionInfo.m_nItemNumber, m_ExpressionInfo.m_nChrNumber, m_ExpressionInfo.m_nExprNumber, m_ExpressionInfo.m_tszExprName);
	}

	void GetStream(TCHAR* ptszBuffer) const { GetStream(ptszBuffer, MAX_BUFFER_SIZE); }

	AVATAR_IMAGE_EXPRESSIONIMAGE_INFO	m_ExpressionInfo = AVATAR_IMAGE_EXPRESSIONIMAGE_INFO();	// 표정 원본 데이터
};

//***************************************************************************
//
class CSpriteFileInfo
{
public:
	void Reset(void)
	{
		m_SpriteFileInfo = AVATAR_IMAGE_SPRITE_FILE_INFO();
	}

	void GetStream(TCHAR* ptszBuffer, size_t cchBuffer) const
	{
		_sntprintf_s(ptszBuffer, cchBuffer, _TRUNCATE, _T("%d\t%s\t%s\t%d\t%s\n"),
			m_SpriteFileInfo.m_nFileIndex, m_SpriteFileInfo.m_tszFileName, m_SpriteFileInfo.m_tszFileTheme,
			m_SpriteFileInfo.m_nSpriteCount, m_SpriteFileInfo.m_tszFileDesc);
	}

	void GetStream(TCHAR* ptszBuffer) const { GetStream(ptszBuffer, MAX_BUFFER_SIZE); }

	AVATAR_IMAGE_SPRITE_FILE_INFO	m_SpriteFileInfo = AVATAR_IMAGE_SPRITE_FILE_INFO();	// 스프라이트 파일 원본 데이터
};

// CKeyedTable : Util/KeyedTable.h 참조 (정렬 벡터 + 이진 탐색 키드 테이블 — 공용 헤더로 분리됨)

//***************************************************************************
// CCharacterInfoMemDBEx : characterinfo.txt (탭 구분)
//***************************************************************************
class CCharacterInfoMemDBEx
{
public:
	BOOL		Init(long lSize = 26);		// 호환용: 빈 상태로 초기화(lSize는 무시)
	BOOL		Init(long lSize, const CCharacterInfo* pCharacterInfo);
	BOOL		Init(std::vector<CCharacterInfo>&& rows);
	BOOL		Load(const TCHAR* ptszFile);	// 파일 읽기 + 캐싱. 실패 시 기존 데이터 유지

	void		RemoveAll(void);
	size_t		GetCount(void) const;

	int			Search(int nChrNumber, CCharacterInfo& CharacterInfo) const;

private:
	CKeyedTable<CCharacterInfo>	m_Table;	// 로드된 행 + 정렬 인덱스(읽기 전용, 리로드 시 교체됨)
	mutable PRWLock	m_RWLock;		// CCharacterInfo 캐시 보호용 읽기/쓰기 락
};

//***************************************************************************
// CItemInfoListMemDBEx : iteminfo.txt(탭 구분), ItemInfoFile.txt(쉼표 구분)
//  - 두 파일은 컬럼 순서만 다르며 동일한 CItemInfoList 행으로 캐싱된다.
//  - 같은 아이템 번호가 레이어별로 여러 행 존재할 수 있다.
//***************************************************************************
class CItemInfoListMemDBEx
{
public:
	BOOL		Init(long lSize = 3000);
	BOOL		Init(long lSize, const CItemInfoList* pItemInfoList);
	BOOL		Init(std::vector<CItemInfoList>&& rows);
	BOOL		Load(const TCHAR* ptszFile);			// iteminfo.txt
	BOOL		LoadItemInfoFile(const TCHAR* ptszFile);	// ItemInfoFile.txt

	void		RemoveAll(void);
	size_t		GetCount(void) const;

	int			GetSubtype(int nItemNumber) const;
	int			GetCoverageMask(int nItemNumber) const;
	int			GetExcludeMask(int nItemNumber) const;
	BOOL		GetMask(int nItemNumber, int& nCoverageMask, int& nExcludeMask) const;

	// pszItemName 버퍼 크기: 크기 미지정 버전은 SM_ITEM_NAME_LEN 가정(호환용)
	BOOL		GetItemName(int nItemNumber, TCHAR* pszItemName) const;
	BOOL		GetItemName(int nItemNumber, TCHAR* pszItemName, size_t cchItemName) const;
	BOOL		GetLinkImageMask(int nItemNumber, BYTE& bHairMask) const;
	BOOL		GetImageInfo(int nItemNumber, TCHAR* pszItemName, BYTE& bHairMask, int& nSubType, int& nCoverageMask, int& nExcludeMask) const;
	BOOL		GetImageInfo(int nItemNumber, TCHAR* pszItemName, size_t cchItemName, BYTE& bHairMask, int& nSubType, int& nCoverageMask, int& nExcludeMask) const;

	// 첫 번째 일치 행을 복사하고 그 인덱스를 반환, 없으면 -1
	int			Search(int nItemNumber, CItemInfoList& ItemInfoList) const;

	// [레거시] pItemInfoList[m_nLayer] 위치에 각 레이어 행을 채운다.
	//  배열 크기를 알 수 없으므로 호출자가 (최대 레이어 + 1) 이상을 보장해야 한다. 신규 코드는 GetImages 사용.
	BOOL		Search(int nItemNumber, CItemInfoList*& pItemInfoList) const;

	// 해당 아이템의 모든 레이어 행을 파일 순서대로 out에 추가. 추가된 개수 반환(0이면 없음).
	int			GetImages(int nItemNumber, std::vector<CItemInfoList>& out) const;

private:
	const CItemInfoList* FindNoLock(int nItemNumber) const;	// 락을 잡은 상태에서만 호출

	CKeyedTable<CItemInfoList>	m_Table;	// 로드된 행 + 정렬 인덱스(읽기 전용, 리로드 시 교체됨)
	mutable PRWLock	m_RWLock;		// CItemInfoList 캐시 보호용 읽기/쓰기 락
};

//***************************************************************************
// CExpressionInfoMemDBEx : expressioninfo.txt (탭 구분)
//***************************************************************************
class CExpressionInfoMemDBEx
{
public:
	BOOL		Init(long lSize = 2000);
	BOOL		Init(long lSize, const CExpressionInfo* pExpressionInfo);
	BOOL		Init(std::vector<CExpressionInfo>&& rows);
	BOOL		Load(const TCHAR* ptszFile);

	void		RemoveAll(void);
	size_t		GetCount(void) const;

	int			Search(int nChrNumber, int nExprNumber, CExpressionInfo& ExpressionInfo) const;

private:
	CKeyedTable<CExpressionInfo>	m_Table;	// 로드된 행 + 정렬 인덱스(읽기 전용, 리로드 시 교체됨)
	mutable PRWLock	m_RWLock;		// CExpressionInfo 캐시 보호용 읽기/쓰기 락
};

//***************************************************************************
// CSpriteFileInfoMemDBEx : .spr 스프라이트 파일 정보 보관/조회 (키: m_nFileIndex)
//  - 채우기는 Init(rows) 로 한다.
//***************************************************************************
class CSpriteFileInfoMemDBEx
{
public:
	BOOL		Init(long lSize = 500);
	BOOL		Init(long lSize, const CSpriteFileInfo* pSpriteFileInfo);
	BOOL		Init(std::vector<CSpriteFileInfo>&& rows);

	void		RemoveAll(void);
	size_t		GetCount(void) const;

	BOOL		Search(int nFileIndex, CSpriteFileInfo& SpriteFileInfo) const;

private:
	CKeyedTable<CSpriteFileInfo>	m_Table;	// 로드된 행 + 정렬 인덱스(읽기 전용, 리로드 시 교체됨)
	mutable PRWLock	m_RWLock;		// CSpriteFileInfo 캐시 보호용 읽기/쓰기 락
};

//***************************************************************************
// 정렬 헬퍼 (레거시 호환 - 내부는 std::stable_sort 사용)
//***************************************************************************
void	Swap(CItemInfoList& pSource, CItemInfoList& pDest);
int		Partition(CItemInfoList*& pItemInfoList, int nF, int nL);
void	Quicksort(CItemInfoList*& pItemInfoList, int nF, int nL);		// 아이템 번호 오름차순, [nF, nL]
void	Bubblesort(CItemInfoList*& pItemInfoList, int nMaxSize);		// 아이템 번호 내림차순

//***************************************************************************
// 파일 입출력
//  - vector 버전 : 권장. 소유권 문제 없음.
//  - 포인터 버전 : 호환용. 성공 시 new[]로 할당한 (nSize+1)개 배열을 돌려주며 호출자가 delete[] 해야 한다.
//***************************************************************************
BOOL	ReadCharacterInfo(const TCHAR* pszTargetFile, std::vector<CCharacterInfo>& rows);
BOOL	ReadItemImageInfoList(const TCHAR* pszTargetFile, std::vector<CItemInfoList>& rows);	// iteminfo.txt (탭)
BOOL	ReadItemInfoFile(const TCHAR* pszTargetFile, std::vector<CItemInfoList>& rows);		// ItemInfoFile.txt (쉼표)
BOOL	ReadExpressionInfo(const TCHAR* pszTargetFile, std::vector<CExpressionInfo>& rows);
BOOL	ReadSpriteFileInfo(const TCHAR* pszTargetFile, std::vector<CSpriteFileInfo>& rows);

BOOL	ReadCharacterInfo(const TCHAR* pszTargetFile, CCharacterInfo*& pCharacterInfo, int& nSize);
BOOL	ReadItemImageInfoList(const TCHAR* pszTargetFile, CItemInfoList*& pItemInfoList, int& nSize);
BOOL	ReadExpressionInfo(const TCHAR* pszTargetFile, CExpressionInfo*& pExpressionInfo, int& nSize);
BOOL	ReadSpriteFileInfo(const TCHAR* pszTargetFile, CSpriteFileInfo*& pSpriteFileInfo, int& nSize);

BOOL	WriteCharacterInfo(const TCHAR* pszTargetFile, const CCharacterInfo* pCharacterInfo, int nSize);
BOOL	WriteItemImageInfoList(const TCHAR* pszTargetFile, const CItemInfoList* pItemInfoList, int nSize);
BOOL	WriteExpressionInfo(const TCHAR* pszTargetFile, const CExpressionInfo* pExpressionInfo, int nSize);
BOOL	WriteSpriteFileInfo(const TCHAR* pszTargetFile, const CSpriteFileInfo* pSpriteFileInfo, int nSize);

#endif // ndef AVATARINFOMEMDBEX_H