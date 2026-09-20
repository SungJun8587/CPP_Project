
//***************************************************************************
// FileMetadata.h : 업로드된 파일 하나에 대한 메타데이터.
//
// [설계 배경] 지금까지는 저장 파일명이 무작위 16진 문자열뿐이라, 원본
// 파일명/크기/업로드 시각 같은 정보를 서버가 전혀 모른다 — 채팅 클라이언트가
// "[FILE:원본이름:크기]url" 형식으로 메시지 텍스트에 직접 실어 보내는 것으로
// 임시方편(client-side workaround)했다. 이 구조체는 그 정보를 서버 쪽에도
// relativePath 기준으로 조회 가능하게 만들어서, 나중에 "누가 언제 뭘
// 올렸는지" 감사/정리(예: 오래된 미사용 파일 정리 배치)가 필요할 때
// 클라이언트가 보낸 텍스트에 의존하지 않고 서버 스스로 답할 수 있게 한다.
//***************************************************************************

#ifndef UC_FILEMETADATA_H
#define UC_FILEMETADATA_H

#include <string>
#include <chrono>

//***************************************************************************
// @struct SFileMetadata
// @brief 업로드된 파일 하나의 메타데이터 스냅샷.
//***************************************************************************
struct SFileMetadata
{
	std::string	relativePath;		// IFileStorage::SaveFile()이 돌려준 상대 경로 — 조회/삭제 키로 쓰임
	std::string	ownerId;			// 업로드한 계정의 public_id(16진 문자열)
	std::string	originalFileName;	// 업로드 시점의 원본 파일명(확장자 포함, 사용자가 고른 그대로)
	std::string	contentType;		// 클라이언트가 보낸 Content-Type(예: "image/jpeg") — 없으면 빈 문자열
	int64		fileSizeBytes = 0;	// 저장된 파일 크기(바이트) — 리사이즈된 경우 리사이즈 이후 크기
	std::chrono::system_clock::time_point uploadedAt;	// 업로드(저장 성공) 시각
};

#endif // ndef UC_FILEMETADATA_H