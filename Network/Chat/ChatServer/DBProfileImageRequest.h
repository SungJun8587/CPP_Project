
//***************************************************************************
// DBProfileImageRequest.h : 프로필 이미지 DB 비동기 요청 구조체 모음
//
//***************************************************************************

#ifndef DBPROFILEIMAGEREQUEST_H
#define DBPROFILEIMAGEREQUEST_H

#include <DB/DBAsyncSrv.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @brief 프로필 이미지 DB 연산 종류.
// @details 프로필 이미지 기능의 모든 요청은 kDbCallIdent_ProfileImage(ChatPacketTypes.h)
//          하나와 핸들러 하나(ProfileImageDBHandler.cpp)로 처리하고, 연산은 이 값으로
//          구분한다. 연산마다 입력과 결과 콜백의 모양이 달라서 요청 구조체는 연산별로
//          두고(이 파일 아래), 모두 ST_PROFILE_IMAGE_REQ_BASE를 상속해 op를 실어 보낸다.
//***************************************************************************
enum class EProfileImageOp : uint8
{
	Set,		// 외부 URL을 새 갤러리 항목으로 등록하고 대표로 지정(빈 문자열이면 대표 해제) — ST_SET_PROFILE_IMAGE_URL_REQ
	List,		// 갤러리 목록 조회 — ST_LIST_PROFILE_IMAGES_REQ
	Select,		// 갤러리의 이미지 하나를 대표로 지정 — ST_SELECT_PROFILE_IMAGE_REQ
	Delete,		// 갤러리의 이미지 하나를 삭제 — ST_DELETE_PROFILE_IMAGE_REQ
};

//***************************************************************************
// @struct ST_PROFILE_IMAGE_REQ_BASE
// @brief 프로필 이미지 DB 요청의 공통 부분. 핸들러가 op를 보고 실제 구조체로 캐스팅한다.
//***************************************************************************
struct ST_PROFILE_IMAGE_REQ_BASE : public st_DBAsyncRq
{
	explicit ST_PROFILE_IMAGE_REQ_BASE(EProfileImageOp operation) : op(operation)
	{
		callIdent = kDbCallIdent_ProfileImage;
		bReTry = false;
	}

	const EProfileImageOp	op;
};

//***************************************************************************
// @struct ST_SET_PROFILE_IMAGE_URL_REQ
// @brief 로그인된 계정(publicId로 식별)에 외부 URL을 새 갤러리 항목으로
//        등록하고 대표(status=1)로 지정하는 요청.
// @details 빈 문자열이면 "대표 이미지 해제"(기존 status=1 행을 0으로만
// 내림, 새로 등록하지 않음)로 처리한다. AccountDBHandler.cpp의 닉네임 변경과는
// 달리 UPDATE 한 번이 아니라 "기존 대표 해제 + 신규 등록"의 두 단계
// 쿼리다(ProfileImageDBHandler.cpp 참고).
//***************************************************************************
struct ST_SET_PROFILE_IMAGE_URL_REQ : public ST_PROFILE_IMAGE_REQ_BASE
{
	ST_SET_PROFILE_IMAGE_URL_REQ() : ST_PROFILE_IMAGE_REQ_BASE(EProfileImageOp::Set) {}

	BYTE	publicId[kPublicIdBytes] = {};				// 대상 계정의 안정 식별자
	char	url[kProfileImageUrlBytes] = {};			// 새 프로필 이미지 URL(UTF-8). 빈 문자열이면 해제

	std::function<void(
		ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newUrl,
		int64 newImageId)>	onComplete;					// newImageId: 새로 등록된 행의 image_id(해제 요청이면 0)
};

//***************************************************************************
// @brief 갤러리 항목 하나를 DB 왕복용으로 담는 평범한 구조체(프로토콜의
//        ListProfileImagesItemResPacket과는 별개 — 이쪽은 고정폭 char 배열이
//        아니라 std::string을 쓴다).
//***************************************************************************
struct SProfileImageEntry
{
	int64		imageId = 0;
	std::string	imageRef;
	bool		isActive = false;
};

//***************************************************************************
// @struct ST_LIST_PROFILE_IMAGES_REQ
// @brief 로그인된 계정(publicId로 식별)이 갖고 있는 프로필 이미지 전체 목록을 조회.
//***************************************************************************
struct ST_LIST_PROFILE_IMAGES_REQ : public ST_PROFILE_IMAGE_REQ_BASE
{
	ST_LIST_PROFILE_IMAGES_REQ() : ST_PROFILE_IMAGE_REQ_BASE(EProfileImageOp::List) {}

	BYTE	publicId[kPublicIdBytes] = {};

	std::function<void(ELoginResult result, const std::vector<SProfileImageEntry>& images)>	onComplete;
};

//***************************************************************************
// @struct ST_SELECT_PROFILE_IMAGE_REQ
// @brief 갤러리에 이미 있는 이미지 중 하나(imageId)를 대표(status=1)로 지정.
// @details WHERE 절에 user_public_id도 같이 걸어 소유자 검증을 겸한다 —
// 남의 image_id를 넣어도 그 행이 매치되지 않아 아무 일도 안 일어난다.
//***************************************************************************
struct ST_SELECT_PROFILE_IMAGE_REQ : public ST_PROFILE_IMAGE_REQ_BASE
{
	ST_SELECT_PROFILE_IMAGE_REQ() : ST_PROFILE_IMAGE_REQ_BASE(EProfileImageOp::Select) {}

	BYTE	publicId[kPublicIdBytes] = {};
	int64	imageId = 0;

	// selectedImageRef: 성공 시 새로 대표가 된 이미지의 image_ref(세션의
	// 표시용 값을 곧바로 갱신하는 데 쓰인다 — 다시 SELECT 안 해도 되게).
	std::function<void(ELoginResult result, int64 imageId, const std::string& selectedImageRef)>	onComplete;
};

//***************************************************************************
// @struct ST_DELETE_PROFILE_IMAGE_REQ
// @brief 갤러리에서 이미지 하나(imageId)를 삭제. WHERE에 user_public_id도
// 같이 걸어 소유자 검증을 겸한다.
//***************************************************************************
struct ST_DELETE_PROFILE_IMAGE_REQ : public ST_PROFILE_IMAGE_REQ_BASE
{
	ST_DELETE_PROFILE_IMAGE_REQ() : ST_PROFILE_IMAGE_REQ_BASE(EProfileImageOp::Delete) {}

	BYTE	publicId[kPublicIdBytes] = {};
	int64	imageId = 0;

	// wasActive/deletedImageRef: 삭제 "전" 시점의 값을 그대로 돌려준다 —
	// 호출부가 이걸로 대표 이미지를 지운 거면 세션의 표시용 값을 비우는
	// 데 쓴다. deletedImageRef가 가리키던 실제 파일을 지우는 건 이제 이
	// 서버의 책임이 아니다(이미지 저장을 별도 파일 서버로 분리했으므로) —
	// 그래도 "무엇을 지웠었는지" 정보 자체는 로그/추후 확장을 위해 그대로
	// 콜백에 실어 보낸다.
	std::function<void(ELoginResult result, int64 imageId, bool wasActive, const std::string& deletedImageRef)>	onComplete;
};

#endif // ndef DBPROFILEIMAGEREQUEST_H