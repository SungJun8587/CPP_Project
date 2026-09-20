
//***************************************************************************
// FileMetadataRepository.cpp: implementation of the CFileMetadataRepository class.
//
//***************************************************************************

#include "pch.h"
#include "FileMetadataRepository.h"

//***************************************************************************
// @brief 파일 하나의 메타데이터를 등록한다.
//***************************************************************************
void CFileMetadataRepository::Register(SFileMetadata metadata)
{
	std::lock_guard<std::mutex> lock(_mutex);
	// relativePath를 복사해서 키로 쓴다 — metadata를 move해서 값 쪽에 넣은
	// 뒤에 그 안의 relativePath를 인덱스 키로 다시 읽으면 정의되지 않은
	// 순서(move 후 읽기)가 되므로, 반드시 move 이전에 키를 먼저 뽑아둔다.
	const std::string key = metadata.relativePath;
	_entries[key] = std::move(metadata);
}

//***************************************************************************
// @brief relativePath로 메타데이터를 조회한다.
//***************************************************************************
bool CFileMetadataRepository::Find(const std::string& relativePath, SFileMetadata& outMetadata) const
{
	std::lock_guard<std::mutex> lock(_mutex);

	auto it = _entries.find(relativePath);
	if( it == _entries.end() )
		return false;

	outMetadata = it->second;
	return true;
}

//***************************************************************************
// @brief relativePath의 메타데이터를 제거한다.
//***************************************************************************
void CFileMetadataRepository::Remove(const std::string& relativePath)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_entries.erase(relativePath);
}