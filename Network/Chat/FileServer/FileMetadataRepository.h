
//***************************************************************************
// FileMetadataRepository.h : interface for the CFileMetadataRepository class.
//
// [설계 — 영속성 없음, 알려진 한계] 현재 파일 서버는 DB를 전혀 갖고 있지
// 않다(Redis만 씀 — 업로드 토큰, 대기 중인 삭제 큐). 이 리포지토리도 같은
// 선상에서 순수 인메모리로 구현했다 — 서버 프로세스가 재시작되면 이미
// 저장된 파일들의 메타데이터는 전부 사라진다(파일 자체는 디스크에 남아있지만
// 메타데이터 조회는 안 됨). 재시작 후에도 유지해야 한다면 SQLite 파일 하나
// 붙이는 정도로 충분할 것 같은데, 실제로 필요해지면 그때 이 클래스의
// 내부 구현만 바꾸면 된다(공개 인터페이스는 그대로 유지 가능하도록 설계).
//***************************************************************************

#ifndef UC_FILEMETADATAREPOSITORY_H
#define UC_FILEMETADATAREPOSITORY_H

#include "FileMetadata.h"

#include <string>
#include <unordered_map>
#include <mutex>

//***************************************************************************
// @class CFileMetadataRepository
// @brief relativePath를 키로 SFileMetadata를 등록/조회/삭제하는 스레드 세이프
//        인메모리 저장소.
//***************************************************************************
class CFileMetadataRepository
{
public:
	//***************************************************************************
	// @brief 파일 하나의 메타데이터를 등록한다. 업로드 처리(FileUploadHandler)가
	//        IFileStorage::SaveFile() 성공 직후 호출한다.
	// @details relativePath가 이미 등록돼 있으면 덮어쓴다 — 정상 흐름에서는
	//          SaveFile()이 매번 새 무작위 경로를 만들어주므로 충돌이
	//          생기지 않지만, 방어적으로 덮어쓰기를 허용해둔다.
	//***************************************************************************
	void Register(SFileMetadata metadata);

	//***************************************************************************
	// @brief relativePath로 메타데이터를 조회한다.
	// @return 등록돼 있으면 true(outMetadata에 복사됨), 없으면 false.
	//***************************************************************************
	bool Find(const std::string& relativePath, SFileMetadata& outMetadata) const;

	//***************************************************************************
	// @brief relativePath의 메타데이터를 제거한다(파일 삭제 처리와 짝을 맞춰
	//        FileDeleteHandler가 호출). 애초에 없던 경로를 지워도 안전(no-op).
	//***************************************************************************
	void Remove(const std::string& relativePath);

private:
	mutable std::mutex									_mutex;
	std::unordered_map<std::string, SFileMetadata>		_entries;	// key: relativePath
};

#endif // ndef UC_FILEMETADATAREPOSITORY_H