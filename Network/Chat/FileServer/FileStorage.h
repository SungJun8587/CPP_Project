
//***************************************************************************
// FileStorage.h : 파일 서버 저장소 추상 인터페이스
//
//***************************************************************************

#ifndef UC_FILESTORAGE_H
#define UC_FILESTORAGE_H

#include <Util/FileStream.h>

#include <string>
#include <vector>
#include <memory>

//***************************************************************************
// @interface IFileStorage
// @brief 업로드된 파일 바이트를 실제로 어디에 저장/조회할지를 추상화한다.
// @details [설계 의도] 지금은 CLocalFileStorage(파일 서버 로컬 디스크)만
// 있지만, 나중에 S3/CDN 등으로 옮길 때 이 인터페이스를 구현하는 클래스만
// 새로 추가하면 된다 — FileServerMain은 이 인터페이스 타입으로만 저장소를
// 참조한다.
//***************************************************************************
class IFileStorage
{
public:
	virtual ~IFileStorage() = default;

	//***************************************************************************
	// @brief 파일 데이터를 저장하고, 이후 LoadFile()로 다시 읽어올 수 있는
	//        상대 경로(파일 시스템 관점)를 돌려준다.
	// @param ownerId 저장 경로를 구분하는 식별자 — 보통 Redis에서 업로드
	//        토큰을 조회해 얻은 계정의 public_id(16진 문자열)를 넘긴다.
	//        업로드 토큰 원문 자체는 재사용/추측 방지를 위해 경로에 안 쓴다.
	// @param fileExtension 확장자(".png", ".pdf" 등, 점 포함) — 저장 파일명에 사용.
	// @param data 실제 파일 바이트.
	// @param outRelativePath [out] 성공 시 baseDir 기준 상대 경로(예:
	//        "a1b2.../3f9c....png") — 공개 URL을 만들 때 이 값을 그대로 붙인다.
	// @return 저장 성공 시 true.
	//***************************************************************************
	virtual bool SaveFile(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::vector<BYTE>& data,
		std::string& outRelativePath) = 0;

	//***************************************************************************
	// @brief [추가] 이미 디스크에 존재하는 파일(sourceFilePath)을 최종 저장
	//        위치로 옮긴다 — 대용량 업로드 스트리밍용. 업로드된 바이트가
	//        이미 임시 파일에 다 쓰여있는 상태이므로, SaveFile()처럼 메모리
	//        버퍼를 다시 통째로 읽고 쓰는 대신 파일 자체를 옮기기만 하면
	//        된다(같은 볼륨이면 rename 한 번으로 끝나 추가 디스크 I/O가
	//        거의 없다).
	// @param sourceFilePath 이미 완성된 파일의 현재 경로(호출 성공 시
	//        이 위치의 파일은 사라진다 — 최종 위치로 옮겨졌으므로).
	// @return 성공 시 true. 실패해도 sourceFilePath의 파일은 그대로
	//         남아있다(호출부가 정리 여부를 결정할 수 있도록).
	//***************************************************************************
	virtual bool SaveFileFromPath(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::string& sourceFilePath,
		std::string& outRelativePath) = 0;

	//***************************************************************************
	// @brief SaveFile()이 돌려준 상대 경로로 저장된 파일을 다시 읽어온다.
	// @return 성공 시 true(파일이 없거나 손상됐으면 false).
	//***************************************************************************
	virtual bool LoadFile(const std::string& relativePath, std::vector<BYTE>& outData) = 0;

	//***************************************************************************
	// @brief SaveFile()로 저장했던 파일을 삭제한다(DELETE /files/{path} 처리용).
	// @return 성공(또는 애초에 파일이 없었음) 시 true — "이미 없는 걸 지우려는"
	//         경우까지 실패로 취급하면 채팅 서버가 삭제 요청을 재시도하다
	//         끝없이 실패로 보고받는 혼란이 생기므로, "그 경로에 파일이 없는
	//         상태"라는 최종 결과 자체는 성공으로 본다.
	//***************************************************************************
	virtual bool DeleteFile(const std::string& relativePath) = 0;

	//***************************************************************************
	// @brief [추가] relativePath의 파일을 스트림으로 연다 — LoadFile()과
	//        달리 파일 전체를 메모리에 올리지 않고, CFileStream을 통해
	//        필요한 만큼만(청크 단위로) 읽을 수 있게 한다. 큰 파일을
	//        Range 요청으로 일부만 받아가는 경우 이쪽을 쓰는 게 훨씬
	//        효율적이다(CFileDownloadHandler가 사용).
	// @return 성공 시 열려있는 CFileStream(IsOpen()==true). 파일이 없거나
	//         경로 조작 시도("../")면 nullptr.
	//***************************************************************************
	virtual std::unique_ptr<CFileStream> OpenStream(const std::string& relativePath) = 0;
};

#endif // ndef UC_FILESTORAGE_H