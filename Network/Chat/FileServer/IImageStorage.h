
//***************************************************************************
// IImageStorage.h : 파일 서버 저장소 추상 인터페이스
//
//***************************************************************************

#ifndef UC_IIMAGESTORAGE_H
#define UC_IIMAGESTORAGE_H

#include <string>
#include <vector>

//***************************************************************************
// @interface IImageStorage
// @brief 업로드된 이미지 바이트를 실제로 어디에 저장/조회할지를 추상화한다.
// @details [설계 의도] 지금은 CLocalFileImageStorage(파일 서버 로컬 디스크)만
// 있지만, 나중에 S3/CDN 등으로 옮길 때 이 인터페이스를 구현하는 클래스만
// 새로 추가하면 된다 — FileServerMain은 이 인터페이스 타입으로만 저장소를
// 참조한다. (채팅 서버가 한때 갖고 있던 것과 동일한 인터페이스를 파일
// 서버로 옮겨왔다 — 이미지 저장을 파일 서버로 분리하면서.)
//***************************************************************************
class IImageStorage
{
public:
	virtual ~IImageStorage() = default;

	//***************************************************************************
	// @brief 이미지 데이터를 저장하고, 이후 LoadImage()로 다시 읽어올 수 있는
	//        상대 경로(파일 시스템 관점)를 돌려준다.
	// @param ownerId 저장 경로를 구분하는 식별자 — 보통 Redis에서 업로드
	//        토큰을 조회해 얻은 계정의 public_id(16진 문자열)를 넘긴다.
	//        업로드 토큰 원문 자체는 재사용/추측 방지를 위해 경로에 안 쓴다.
	// @param fileExtension 확장자(".png" 등, 점 포함) — 저장 파일명에 사용.
	// @param data 실제 이미지 바이트.
	// @param outRelativePath [out] 성공 시 baseDir 기준 상대 경로(예:
	//        "a1b2.../3f9c....png") — 공개 URL을 만들 때 이 값을 그대로 붙인다.
	// @return 저장 성공 시 true.
	//***************************************************************************
	virtual bool SaveImage(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::vector<BYTE>& data,
		std::string& outRelativePath) = 0;

	//***************************************************************************
	// @brief SaveImage()가 돌려준 상대 경로로 저장된 이미지를 다시 읽어온다.
	// @return 성공 시 true(파일이 없거나 손상됐으면 false).
	//***************************************************************************
	virtual bool LoadImage(const std::string& relativePath, std::vector<BYTE>& outData) = 0;

	//***************************************************************************
	// @brief SaveImage()로 저장했던 이미지를 삭제한다(DELETE /images/{path} 처리용).
	// @return 성공(또는 애초에 파일이 없었음) 시 true — "이미 없는 걸 지우려는"
	//         경우까지 실패로 취급하면 채팅 서버가 삭제 요청을 재시도하다
	//         끝없이 실패로 보고받는 혼란이 생기므로, "그 경로에 파일이 없는
	//         상태"라는 최종 결과 자체는 성공으로 본다.
	//***************************************************************************
	virtual bool DeleteImage(const std::string& relativePath) = 0;
};

#endif // ndef UC_IIMAGESTORAGE_H