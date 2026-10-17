
//***************************************************************************
// AvatarInfoMemDBEX.cpp: implementation of the CAvatarInfoMemDBEX class.
//
//***************************************************************************


#include "pch.h"
#include "AvatarInfoMemDBEx.h"

#include <cstdarg>
#include <cstdlib>
#include <climits>

//***************************************************************************
// 내부 도구
//***************************************************************************
namespace
{
	//-----------------------------------------------------------------------
	// 텍스트 파서 : 공용 CTextFieldParser(Util/TextFieldParser.h)를 사용한다.
	// _tcstok 는 연속 구분자를 하나로 합쳐 빈 필드가 사라지므로(컬럼 밀림)
	// 사용하지 않는다.
	//-----------------------------------------------------------------------
	using TFP = CTextFieldParser;
	using Field = TFP::Field;

	//-----------------------------------------------------------------------
	// 파일별 컬럼 정의
	//-----------------------------------------------------------------------
	namespace ChrCol	// characterinfo.txt (탭)
	{
		enum { Number, Name, Gender, Body, Hair, Jacket, Pants, Shadow };
	}

	// 아이템 이미지 행의 컬럼 위치. iteminfo.txt 와 ItemInfoFile.txt 는 컬럼 순서만 다르다.
	struct ItemColumns
	{
		size_t seq, itemNumber, subType, name, gender, linkMask, coverage, exclude, pos, layer, sprFile;
	};

	// iteminfo.txt (탭)     : Seq, ItemNumber, SubType, Name, Gender, LinkMask, Coverage, Exclude, Pos, Layer, SprFile
	const ItemColumns kItemTabColumns = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	// ItemInfoFile.txt (쉼표): Seq, ItemNumber, Name, Gender, SubType, LinkMask, Coverage, Exclude, Pos, Layer, SprFile, [Desc]
	const ItemColumns kItemCsvColumns = { 0, 1, 4, 2, 3, 5, 6, 7, 8, 9, 10 };

	namespace ExprCol	// expressioninfo.txt (탭)
	{
		enum { ItemNumber, ChrNumber, ExprNumber, Name };
	}

	namespace SprCol	// 스프라이트 파일 정보 텍스트(쉼표) - 레거시 ReadSpriteFileInfo 용
	{
		enum { FileNumber = 0, Theme = 1, SpriteCount = 4, Desc = 5 };
		const TCHAR chDelim = _T(',');
	}

	//-----------------------------------------------------------------------
	// 라인 -> 행 변환. 키(필수) 필드가 잘못된 라인은 버린다.
	//-----------------------------------------------------------------------
	bool ParseCharacter(const Field* f, size_t n, CCharacterInfo& row)
	{
		AVATAR_IMAGE_CHARACTER_INFO& c = row.m_CharacterInfo;

		if( n <= ChrCol::Number || !TFP::ParseInt(f[ChrCol::Number], c.m_nChrNumber) ) return false;

		TFP::OptStr(f, n, ChrCol::Name, c.m_tszChrName);
		TFP::OptStr(f, n, ChrCol::Gender, c.m_tszGender);
		TFP::OptInt(f, n, ChrCol::Body, c.m_nBodyItemNum);
		TFP::OptInt(f, n, ChrCol::Hair, c.m_nHairItemNum);
		TFP::OptInt(f, n, ChrCol::Jacket, c.m_nJacketItemNum);
		TFP::OptInt(f, n, ChrCol::Pants, c.m_nPantsItemNum);
		TFP::OptInt(f, n, ChrCol::Shadow, c.m_nShadowItemNum);
		return true;
	}

	bool ParseItemImage(const Field* f, size_t n, const ItemColumns& col, CItemInfoList& row)
	{
		AVATAR_IMAGE_ITEMIMAGE_INFO& c = row.m_ItemImageInfo;

		if( n <= col.itemNumber || !TFP::ParseInt(f[col.itemNumber], c.m_nItemNumber) ) return false;

		TFP::OptInt(f, n, col.seq, c.m_nItemImageSeq);
		TFP::OptInt(f, n, col.subType, c.m_nSubType);
		TFP::OptStr(f, n, col.name, c.m_tszName);
		TFP::OptStr(f, n, col.gender, c.m_tszGender);
		TFP::OptInt(f, n, col.linkMask, c.m_nLinkImageMask);
		TFP::OptInt(f, n, col.coverage, c.m_nCoverageMask);
		TFP::OptInt(f, n, col.exclude, c.m_nExcludeMask);
		TFP::OptInt(f, n, col.pos, c.m_nPos);
		TFP::OptInt(f, n, col.layer, c.m_nLayer);
		TFP::OptStr(f, n, col.sprFile, c.m_tszSprFileName);
		return true;
	}

	bool ParseExpression(const Field* f, size_t n, CExpressionInfo& row)
	{
		AVATAR_IMAGE_EXPRESSIONIMAGE_INFO& c = row.m_ExpressionInfo;

		if( n <= ExprCol::ExprNumber ) return false;
		if( !TFP::ParseInt(f[ExprCol::ItemNumber], c.m_nItemNumber) ) return false;
		if( !TFP::ParseInt(f[ExprCol::ChrNumber], c.m_nChrNumber) ) return false;
		if( !TFP::ParseInt(f[ExprCol::ExprNumber], c.m_nExprNumber) ) return false;

		TFP::OptStr(f, n, ExprCol::Name, c.m_tszExprName);
		return true;
	}

	bool ParseSpriteFile(const Field* f, size_t n, CSpriteFileInfo& row)
	{
		AVATAR_IMAGE_SPRITE_FILE_INFO& c = row.m_SpriteFileInfo;

		int nFileNumber = 0;
		if( n <= SprCol::FileNumber || !TFP::ParseInt(f[SprCol::FileNumber], nFileNumber) || nFileNumber == INT_MAX ) return false;

		c.m_nFileIndex = nFileNumber + 1;

		Field name = f[SprCol::FileNumber];
		TFP::TrimBoth(name);
		_sntprintf_s(c.m_tszFileName, _countof(c.m_tszFileName), _TRUNCATE, _T("%.*s.spr"), static_cast<int>(name.e - name.b), name.b);

		TFP::OptStr(f, n, SprCol::Theme, c.m_tszFileTheme);
		TFP::OptInt(f, n, SprCol::SpriteCount, c.m_nSpriteCount);
		TFP::OptStr(f, n, SprCol::Desc, c.m_tszFileDesc);
		return true;
	}

	// 파일 로드/저장 : Util/DelimitedRowFile.h 의 LoadDelimitedRows/ToLegacyArray/
	// WriteDelimitedRows 사용 (공용 헤더로 분리됨)

	//-----------------------------------------------------------------------
	// 인덱스 키
	//-----------------------------------------------------------------------
	inline std::uint64_t KeyOf(int n)
	{
		return static_cast<std::uint32_t>(n);
	}

	inline std::uint64_t KeyOf(int nHigh, int nLow)
	{
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(nHigh)) << 32) | static_cast<std::uint32_t>(nLow);
	}
}

//***************************************************************************
// 정렬 헬퍼 (레거시 호환)
//***************************************************************************
void Swap(CItemInfoList& pSource, CItemInfoList& pDest)
{
	std::swap(pSource, pDest);
}

int Partition(CItemInfoList*& pItemInfoList, int nF, int nL)
{
	// 무작위 피벗을 끝(nL)으로 보낸 뒤 Lomuto 분할
	const int nRandomIndex = nF + (rand() % (nL - nF + 1));
	Swap(pItemInfoList[nRandomIndex], pItemInfoList[nL]);

	const int nPivot = pItemInfoList[nL].m_ItemImageInfo.m_nItemNumber;
	int i = nF - 1;

	for( int j = nF; j <= nL - 1; j++ )
	{
		if( pItemInfoList[j].m_ItemImageInfo.m_nItemNumber <= nPivot )
		{
			i = i + 1;
			Swap(pItemInfoList[i], pItemInfoList[j]);
		}
	}

	Swap(pItemInfoList[i + 1], pItemInfoList[nL]);
	return (i + 1);
}

void Quicksort(CItemInfoList*& pItemInfoList, int nF, int nL)
{
	if( !pItemInfoList || nF < 0 || nF >= nL ) return;

	// stable_sort : 같은 아이템 번호(레이어 행)의 파일 내 상대 순서 보존, 재귀 깊이 문제 없음
	std::stable_sort(pItemInfoList + nF, pItemInfoList + nL + 1,
		[](const CItemInfoList& a, const CItemInfoList& b)
		{
			return a.m_ItemImageInfo.m_nItemNumber < b.m_ItemImageInfo.m_nItemNumber;
		});
}

void Bubblesort(CItemInfoList*& pItemInfoList, int nMaxSize)
{
	if( !pItemInfoList || nMaxSize < 2 ) return;

	// 아이템 번호 내림차순
	std::stable_sort(pItemInfoList, pItemInfoList + nMaxSize,
		[](const CItemInfoList& a, const CItemInfoList& b)
		{
			return a.m_ItemImageInfo.m_nItemNumber > b.m_ItemImageInfo.m_nItemNumber;
		});
}

//***************************************************************************
// 파일 입출력
//***************************************************************************
BOOL ReadCharacterInfo(const TCHAR* pszTargetFile, std::vector<CCharacterInfo>& rows)
{
	return LoadDelimitedRows(pszTargetFile, _T('\t'), ParseCharacter, rows);
}

BOOL ReadItemImageInfoList(const TCHAR* pszTargetFile, std::vector<CItemInfoList>& rows)
{
	return LoadDelimitedRows(pszTargetFile, _T('\t'),
		[](const Field* f, size_t n, CItemInfoList& row) { return ParseItemImage(f, n, kItemTabColumns, row); }, rows);
}

BOOL ReadItemInfoFile(const TCHAR* pszTargetFile, std::vector<CItemInfoList>& rows)
{
	return LoadDelimitedRows(pszTargetFile, _T(','),
		[](const Field* f, size_t n, CItemInfoList& row) { return ParseItemImage(f, n, kItemCsvColumns, row); }, rows);
}

BOOL ReadExpressionInfo(const TCHAR* pszTargetFile, std::vector<CExpressionInfo>& rows)
{
	return LoadDelimitedRows(pszTargetFile, _T('\t'), ParseExpression, rows);
}

BOOL ReadSpriteFileInfo(const TCHAR* pszTargetFile, std::vector<CSpriteFileInfo>& rows)
{
	return LoadDelimitedRows(pszTargetFile, SprCol::chDelim, ParseSpriteFile, rows);
}

//***************************************************************************
BOOL ReadCharacterInfo(const TCHAR* pszTargetFile, CCharacterInfo*& pCharacterInfo, int& nSize)
{
	std::vector<CCharacterInfo> rows;
	if( !ReadCharacterInfo(pszTargetFile, rows) ) { pCharacterInfo = nullptr; nSize = 0; return FALSE; }
	return ToLegacyArray(rows, pCharacterInfo, nSize);
}

BOOL ReadItemImageInfoList(const TCHAR* pszTargetFile, CItemInfoList*& pItemInfoList, int& nSize)
{
	std::vector<CItemInfoList> rows;
	if( !ReadItemImageInfoList(pszTargetFile, rows) ) { pItemInfoList = nullptr; nSize = 0; return FALSE; }
	return ToLegacyArray(rows, pItemInfoList, nSize);
}

BOOL ReadExpressionInfo(const TCHAR* pszTargetFile, CExpressionInfo*& pExpressionInfo, int& nSize)
{
	std::vector<CExpressionInfo> rows;
	if( !ReadExpressionInfo(pszTargetFile, rows) ) { pExpressionInfo = nullptr; nSize = 0; return FALSE; }
	return ToLegacyArray(rows, pExpressionInfo, nSize);
}

BOOL ReadSpriteFileInfo(const TCHAR* pszTargetFile, CSpriteFileInfo*& pSpriteFileInfo, int& nSize)
{
	std::vector<CSpriteFileInfo> rows;
	if( !ReadSpriteFileInfo(pszTargetFile, rows) ) { pSpriteFileInfo = nullptr; nSize = 0; return FALSE; }
	return ToLegacyArray(rows, pSpriteFileInfo, nSize);
}

//***************************************************************************
BOOL WriteCharacterInfo(const TCHAR* pszTargetFile, const CCharacterInfo* pCharacterInfo, int nSize)
{
	return WriteDelimitedRows(pszTargetFile, pCharacterInfo, nSize);
}

BOOL WriteItemImageInfoList(const TCHAR* pszTargetFile, const CItemInfoList* pItemInfoList, int nSize)
{
	return WriteDelimitedRows(pszTargetFile, pItemInfoList, nSize);
}

BOOL WriteExpressionInfo(const TCHAR* pszTargetFile, const CExpressionInfo* pExpressionInfo, int nSize)
{
	return WriteDelimitedRows(pszTargetFile, pExpressionInfo, nSize);
}

BOOL WriteSpriteFileInfo(const TCHAR* pszTargetFile, const CSpriteFileInfo* pSpriteFileInfo, int nSize)
{
	return WriteDelimitedRows(pszTargetFile, pSpriteFileInfo, nSize);
}

//***************************************************************************
// CCharacterInfoMemDBEx
//***************************************************************************
BOOL CCharacterInfoMemDBEx::Init(long /*lSize*/)
{
	RemoveAll();
	return TRUE;
}

BOOL CCharacterInfoMemDBEx::Init(long lSize, const CCharacterInfo* pCharacterInfo)
{
	if( pCharacterInfo == NULL || lSize <= 0 ) return FALSE;	// 실패 시 기존 데이터 유지

	return Init(std::vector<CCharacterInfo>(pCharacterInfo, pCharacterInfo + lSize));
}

BOOL CCharacterInfoMemDBEx::Init(std::vector<CCharacterInfo>&& rows)
{
	// 인덱스 구축은 락 밖에서, 락 안에서는 교체(swap)만 -> 읽기 스레드 정지 시간 최소화
	CKeyedTable<CCharacterInfo> table;
	table.Build(std::move(rows), [](const CCharacterInfo& r) { return KeyOf(r.m_CharacterInfo.m_nChrNumber); });

	{
		PRWriteLockGuard guard(m_RWLock);
		m_Table.Swap(table);
	}	// 이전 데이터(table)는 락 해제 후 소멸

	return TRUE;
}

BOOL CCharacterInfoMemDBEx::Load(const TCHAR* ptszFile)
{
	std::vector<CCharacterInfo> rows;
	if( !ReadCharacterInfo(ptszFile, rows) ) return FALSE;

	return Init(std::move(rows));
}

void CCharacterInfoMemDBEx::RemoveAll(void)
{
	CKeyedTable<CCharacterInfo> old;
	{
		PRWriteLockGuard guard(m_RWLock);
		old.Swap(m_Table);
	}

	LOG_INFO(_T(" # CCharacterInfoMemDB RemoveAll #, Count = %zu "), old.Size());
}

size_t CCharacterInfoMemDBEx::GetCount(void) const
{
	PRReadLockGuard guard(m_RWLock);
	return m_Table.Size();
}

int CCharacterInfoMemDBEx::Search(int nChrNumber, CCharacterInfo& CharacterInfo) const
{
	if( nChrNumber < 0 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	const int nPos = m_Table.Find(KeyOf(nChrNumber));
	if( nPos >= 0 ) CharacterInfo = m_Table.At(nPos);

	return nPos;
}

//***************************************************************************
// CItemInfoListMemDBEx
//***************************************************************************
BOOL CItemInfoListMemDBEx::Init(long /*lSize*/)
{
	RemoveAll();
	return TRUE;
}

BOOL CItemInfoListMemDBEx::Init(long lSize, const CItemInfoList* pItemInfoList)
{
	if( pItemInfoList == NULL || lSize <= 0 ) return FALSE;

	return Init(std::vector<CItemInfoList>(pItemInfoList, pItemInfoList + lSize));
}

BOOL CItemInfoListMemDBEx::Init(std::vector<CItemInfoList>&& rows)
{
	CKeyedTable<CItemInfoList> table;
	table.Build(std::move(rows), [](const CItemInfoList& r) { return KeyOf(r.m_ItemImageInfo.m_nItemNumber); });

	{
		PRWriteLockGuard guard(m_RWLock);
		m_Table.Swap(table);
	}

	return TRUE;
}

BOOL CItemInfoListMemDBEx::Load(const TCHAR* ptszFile)
{
	std::vector<CItemInfoList> rows;
	if( !ReadItemImageInfoList(ptszFile, rows) ) return FALSE;

	return Init(std::move(rows));
}

BOOL CItemInfoListMemDBEx::LoadItemInfoFile(const TCHAR* ptszFile)
{
	std::vector<CItemInfoList> rows;
	if( !ReadItemInfoFile(ptszFile, rows) ) return FALSE;

	return Init(std::move(rows));
}

void CItemInfoListMemDBEx::RemoveAll(void)
{
	CKeyedTable<CItemInfoList> old;
	{
		PRWriteLockGuard guard(m_RWLock);
		old.Swap(m_Table);
	}

	LOG_INFO(_T(" # CItemInfoListMemDB RemoveAll #, Count = %zu "), old.Size());
}

size_t CItemInfoListMemDBEx::GetCount(void) const
{
	PRReadLockGuard guard(m_RWLock);
	return m_Table.Size();
}

const CItemInfoList* CItemInfoListMemDBEx::FindNoLock(int nItemNumber) const
{
	if( nItemNumber < 0 ) return NULL;

	const int nPos = m_Table.Find(KeyOf(nItemNumber));
	return (nPos >= 0) ? &m_Table.At(nPos) : NULL;
}

//*************************************************************************
// 반환 규약 : 비었거나 음수 입력 -1, 못 찾음 0
int CItemInfoListMemDBEx::GetSubtype(int nItemNumber) const
{
	if( nItemNumber < 0 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	if( m_Table.Empty() ) return -1;

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	return pItem ? pItem->m_ItemImageInfo.m_nSubType : 0;
}

int CItemInfoListMemDBEx::GetCoverageMask(int nItemNumber) const
{
	if( nItemNumber < 0 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	return pItem ? pItem->m_ItemImageInfo.m_nCoverageMask : -1;
}

int CItemInfoListMemDBEx::GetExcludeMask(int nItemNumber) const
{
	if( nItemNumber < 0 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	return pItem ? pItem->m_ItemImageInfo.m_nExcludeMask : -1;
}

BOOL CItemInfoListMemDBEx::GetMask(int nItemNumber, int& nCoverageMask, int& nExcludeMask) const
{
	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	if( !pItem ) return FALSE;

	nCoverageMask = pItem->m_ItemImageInfo.m_nCoverageMask;
	nExcludeMask = pItem->m_ItemImageInfo.m_nExcludeMask;
	return TRUE;
}

BOOL CItemInfoListMemDBEx::GetItemName(int nItemNumber, TCHAR* pszItemName) const
{
	return GetItemName(nItemNumber, pszItemName, AVATARITEM_ITEM_NAME_LEN);
}

BOOL CItemInfoListMemDBEx::GetItemName(int nItemNumber, TCHAR* pszItemName, size_t cchItemName) const
{
	if( !pszItemName || cchItemName == 0 ) return FALSE;

	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	if( !pItem ) return FALSE;

	CTextFieldParser::CopyTruncate(pszItemName, cchItemName, pItem->m_ItemImageInfo.m_tszName);
	return TRUE;
}

BOOL CItemInfoListMemDBEx::GetLinkImageMask(int nItemNumber, BYTE& bHairMask) const
{
	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	if( !pItem ) return FALSE;

	bHairMask = static_cast<BYTE>(bHairMask | pItem->m_ItemImageInfo.m_nLinkImageMask);	// 하위 8비트만 사용
	return TRUE;
}

BOOL CItemInfoListMemDBEx::GetImageInfo(int nItemNumber, TCHAR* pszItemName, BYTE& bHairMask, int& nSubType, int& nCoverageMask, int& nExcludeMask) const
{
	return GetImageInfo(nItemNumber, pszItemName, AVATARITEM_ITEM_NAME_LEN, bHairMask, nSubType, nCoverageMask, nExcludeMask);
}

BOOL CItemInfoListMemDBEx::GetImageInfo(int nItemNumber, TCHAR* pszItemName, size_t cchItemName, BYTE& bHairMask, int& nSubType, int& nCoverageMask, int& nExcludeMask) const
{
	PRReadLockGuard guard(m_RWLock);

	const CItemInfoList* pItem = FindNoLock(nItemNumber);
	if( !pItem ) return FALSE;

	const AVATAR_IMAGE_ITEMIMAGE_INFO& info = pItem->m_ItemImageInfo;

	bHairMask = static_cast<BYTE>(bHairMask | info.m_nLinkImageMask);
	nSubType = info.m_nSubType;
	nCoverageMask = info.m_nCoverageMask;
	nExcludeMask = info.m_nExcludeMask;
	CTextFieldParser::CopyTruncate(pszItemName, cchItemName, info.m_tszName);
	return TRUE;
}

int CItemInfoListMemDBEx::Search(int nItemNumber, CItemInfoList& ItemInfoList) const
{
	if( nItemNumber < 0 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	const int nPos = m_Table.Find(KeyOf(nItemNumber));
	if( nPos >= 0 ) ItemInfoList = m_Table.At(nPos);

	return nPos;
}

// [레거시] 찾은 경우에만 TRUE
BOOL CItemInfoListMemDBEx::Search(int nItemNumber, CItemInfoList*& pItemInfoList) const
{
	if( !pItemInfoList || nItemNumber < 0 ) return FALSE;

	PRReadLockGuard guard(m_RWLock);

	const auto range = m_Table.EqualRange(KeyOf(nItemNumber));
	BOOL bFound = FALSE;

	for( const KeyedTableIndexEntry* it = range.first; it != range.second; ++it )
	{
		const CItemInfoList& row = m_Table.At(it->pos);
		const int nLayer = row.m_ItemImageInfo.m_nLayer;

		if( nLayer < 0 ) continue;

		pItemInfoList[nLayer] = row;
		bFound = TRUE;
	}

	return bFound;
}

int CItemInfoListMemDBEx::GetImages(int nItemNumber, std::vector<CItemInfoList>& out) const
{
	if( nItemNumber < 0 ) return 0;

	PRReadLockGuard guard(m_RWLock);

	const auto range = m_Table.EqualRange(KeyOf(nItemNumber));
	out.reserve(out.size() + static_cast<size_t>(range.second - range.first));

	int nCount = 0;
	for( const KeyedTableIndexEntry* it = range.first; it != range.second; ++it, ++nCount )
	{
		out.push_back(m_Table.At(it->pos));
	}

	return nCount;
}

//***************************************************************************
// CExpressionInfoMemDBEx
//***************************************************************************
BOOL CExpressionInfoMemDBEx::Init(long /*lSize*/)
{
	RemoveAll();
	return TRUE;
}

BOOL CExpressionInfoMemDBEx::Init(long lSize, const CExpressionInfo* pExpressionInfo)
{
	if( pExpressionInfo == NULL || lSize <= 0 ) return FALSE;

	return Init(std::vector<CExpressionInfo>(pExpressionInfo, pExpressionInfo + lSize));
}

BOOL CExpressionInfoMemDBEx::Init(std::vector<CExpressionInfo>&& rows)
{
	CKeyedTable<CExpressionInfo> table;
	table.Build(std::move(rows), [](const CExpressionInfo& r)
		{
			return KeyOf(r.m_ExpressionInfo.m_nChrNumber, r.m_ExpressionInfo.m_nExprNumber);
		});

	{
		PRWriteLockGuard guard(m_RWLock);
		m_Table.Swap(table);
	}

	return TRUE;
}

BOOL CExpressionInfoMemDBEx::Load(const TCHAR* ptszFile)
{
	std::vector<CExpressionInfo> rows;
	if( !ReadExpressionInfo(ptszFile, rows) ) return FALSE;

	return Init(std::move(rows));
}

void CExpressionInfoMemDBEx::RemoveAll(void)
{
	CKeyedTable<CExpressionInfo> old;
	{
		PRWriteLockGuard guard(m_RWLock);
		old.Swap(m_Table);
	}

	LOG_INFO(_T(" # CExpressionInfoMemDB RemoveAll #, Count = %zu "), old.Size());
}

size_t CExpressionInfoMemDBEx::GetCount(void) const
{
	PRReadLockGuard guard(m_RWLock);
	return m_Table.Size();
}

int CExpressionInfoMemDBEx::Search(int nChrNumber, int nExprNumber, CExpressionInfo& ExpressionInfo) const
{
	if( nChrNumber < 1 || nExprNumber < 1 ) return -1;

	PRReadLockGuard guard(m_RWLock);

	const int nPos = m_Table.Find(KeyOf(nChrNumber, nExprNumber));
	if( nPos >= 0 ) ExpressionInfo = m_Table.At(nPos);

	return nPos;
}

//***************************************************************************
// CSpriteFileInfoMemDBEx
//***************************************************************************
BOOL CSpriteFileInfoMemDBEx::Init(long /*lSize*/)
{
	RemoveAll();
	return TRUE;
}

BOOL CSpriteFileInfoMemDBEx::Init(long lSize, const CSpriteFileInfo* pSpriteFileInfo)
{
	if( pSpriteFileInfo == NULL || lSize <= 0 ) return FALSE;

	return Init(std::vector<CSpriteFileInfo>(pSpriteFileInfo, pSpriteFileInfo + lSize));
}

BOOL CSpriteFileInfoMemDBEx::Init(std::vector<CSpriteFileInfo>&& rows)
{
	CKeyedTable<CSpriteFileInfo> table;
	table.Build(std::move(rows), [](const CSpriteFileInfo& r) { return KeyOf(r.m_SpriteFileInfo.m_nFileIndex); });

	{
		PRWriteLockGuard guard(m_RWLock);
		m_Table.Swap(table);
	}

	return TRUE;
}

void CSpriteFileInfoMemDBEx::RemoveAll(void)
{
	CKeyedTable<CSpriteFileInfo> old;
	{
		PRWriteLockGuard guard(m_RWLock);
		old.Swap(m_Table);
	}

	LOG_INFO(_T(" # CSpriteFileInfoMemDB RemoveAll #, Count = %zu "), old.Size());
}

size_t CSpriteFileInfoMemDBEx::GetCount(void) const
{
	PRReadLockGuard guard(m_RWLock);
	return m_Table.Size();
}

BOOL CSpriteFileInfoMemDBEx::Search(int nFileIndex, CSpriteFileInfo& SpriteFileInfo) const
{
	if( nFileIndex < 1 ) return FALSE;

	PRReadLockGuard guard(m_RWLock);

	const int nPos = m_Table.Find(KeyOf(nFileIndex));
	if( nPos < 0 ) return FALSE;

	SpriteFileInfo = m_Table.At(nPos);
	return TRUE;
}