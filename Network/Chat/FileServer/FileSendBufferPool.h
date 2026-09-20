
//***************************************************************************
// FileSendBufferPool.h : 파일 스트리밍 전송용 청크 버퍼 재사용 풀.
//
// [설계 배경] CFileDownloadHandler가 파일을 청크 단위로 읽어 보낼 때마다
// std::vector<BYTE>(kChunkSize)를 새로 할당/해제하면, 요청이 몰릴 때
// 힙 할당 비용이 누적된다. 이 풀은 그 청크 버퍼를 재사용해서 할당 횟수를
// 줄인다 — 이 프로젝트의 네트워크 송신 버퍼(CSendBufferManager/
// CSendBufferChunk, net-send-recv-buffer 영역)와 같은 목적의, 다만 훨씬
// 단순화된 버전이다(네트워크 송신 버퍼는 참조 카운트 기반 청크 체인을
// 쓰지만, 여기는 "디스크에서 읽어 담았다가 곧바로 SendRaw로 넘기고 즉시
// 반납"하는 훨씬 짧은 수명이라 굳이 그 정도 복잡도가 필요 없다고 판단했다).
//***************************************************************************

#ifndef UC_FILESENDBUFFERPOOL_H
#define UC_FILESENDBUFFERPOOL_H

#include <vector>
#include <mutex>
#include <cstddef>

//***************************************************************************
// @brief kChunkSize 크기의 BYTE 버퍼를 빌려주고(Acquire) 돌려받는(Release) 프로세스 전역 정적 풀
// @details 스레드 세이프(mutex 기반) — 여러 다운로드 요청이 동시에 진행돼도 안전하게 공유한다.
//***************************************************************************
class CFileSendBufferPool
{
public:
	// 청크 하나의 크기 — Range 요청으로 파일 앞부분만 살짝 보는 경우와
	// 전체 다운로드 양쪽에서 무난한 절충점으로 64KB를 골랐다. 너무 작으면
	// SendRaw/디스크 read 호출 횟수가 늘고, 너무 크면 청크 단위 스트리밍의
	// 메모리 절감 효과가 옅어진다.
	static constexpr size_t kChunkSize = 64 * 1024;

	static std::vector<BYTE> Acquire();
	static void Release(std::vector<BYTE> buffer);

private:
	static std::mutex						_mutex; // 스레드 동기화를 위한 뮤텍스
	static std::vector<std::vector<BYTE>>	_pool;  // 재사용 가능한 버퍼들을 보관하는 정적 벡터 풀
};

#endif // ndef UC_FILESENDBUFFERPOOL_H