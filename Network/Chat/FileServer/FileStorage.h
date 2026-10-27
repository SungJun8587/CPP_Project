
//***************************************************************************
// FileStorage.h : 파일 서버 저장소 추상 인터페이스
//
//***************************************************************************

#ifndef FILESTORAGE_H
#define FILESTORAGE_H

#include <Util/FileStream.h>

#include <string>
#include <memory>

//***************************************************************************
// @interface IFileStorage
// @brief 업로드된 파일 바이트를 실제로 어디에 저장/조회할지를 추상화한다.
// @details [설계 의도] 지금은 CLocalFileStorage(파일 서버 로컬 디스크)만
// 있지만, 나중에 S3/CDN 등으로 옮길 때 이 인터페이스를 구현하는 클래스만
// 새로 추가하면 된다 — FileServerMain은 이 인터페이스 타입으로만 저장소를
// 참조한다. 상대 경로는 항상 "{ownerId}/{이름}{확장자}" 형태다.
//***************************************************************************
class IFileStorage
{
public:
	virtual ~IFileStorage() = default;

	//***************************************************************************
	// @brief 이미 디스크에 존재하는 파일(sourceFilePath)을 최종 저장 위치로
	//        옮긴다 — 업로드는 임시 파일로 스트리밍되므로 메모리 버퍼 없이
	//        파일 자체를 옮기기만 하면 된다(같은 볼륨이면 rename 한 번).
	// @param ownerId 저장 경로를 구분하는 식별자 — 업로드 토큰으로 얻은 계정의
	//        public_id(16진 문자열). 영숫자/'_'/'-'만 허용(아니면 실패).
	// @param fileExtension 확장자(".png" 등, 점 포함). 영숫자 8자 이하가
	//        아니면 ".bin"으로 대체된다.
	// @param outRelativePath [out] 성공 시 baseDir 기준 상대 경로
	//        ("{ownerId}/{무작위16진}{확장자}") — 공개 URL에 그대로 붙인다.
	// @return 성공 시 true. 실패해도 sourceFilePath의 파일은 그대로 남는다
	//         (호출부가 정리 여부를 결정할 수 있도록).
	//***************************************************************************
	virtual bool SaveFileFromPath(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::string& sourceFilePath,
		std::string& outRelativePath) = 0;

	//***************************************************************************
	// @brief SaveFileFromPath()로 저장했던 파일을 삭제한다(예약 삭제 처리용).
	// @return 삭제됐거나 애초에 파일이 없었으면 true(최종 상태가 "파일 없음"
	//         이면 성공). 경로가 유효하지 않거나 파일시스템 오류(권한, 사용
	//         중 등)면 false — 호출부가 실패 목록에 남길 수 있게 한다.
	//***************************************************************************
	virtual bool DeleteFile(const std::string& relativePath) = 0;

	//***************************************************************************
	// @brief relativePath의 파일을 스트림으로 연다 — 파일 전체를 메모리에
	//        올리지 않고 필요한 만큼만(청크 단위로) 읽는다(CFileDownloadHandler).
	// @return 성공 시 열려있는 CFileStream. 파일이 없거나 경로가 유효하지
	//         않으면 nullptr.
	//***************************************************************************
	virtual std::unique_ptr<CFileStream> OpenStream(const std::string& relativePath) = 0;
};

#endif // ndef FILESTORAGE_H