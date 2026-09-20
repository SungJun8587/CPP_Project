
//***************************************************************************
// FileSendBufferPool.cpp: implementation of the CFileSendBufferPool class.
//
//***************************************************************************

#include "pch.h"
#include "FileSendBufferPool.h"

std::mutex						CFileSendBufferPool::_mutex;
std::vector<std::vector<BYTE>>	CFileSendBufferPool::_pool;

//***************************************************************************
// @brief 풀에서 청크 버퍼 하나를 빌려온다. 풀이 비어있으면 새로 할당한다.
// @return std::vector<BYTE> kChunkSize 크기로 할당되거나 재사용된 청크 버퍼
//***************************************************************************
std::vector<BYTE> CFileSendBufferPool::Acquire()
{
	std::lock_guard<std::mutex> lock(_mutex);

	if( !_pool.empty() )
	{
		std::vector<BYTE> buffer = std::move(_pool.back());
		_pool.pop_back();
		return buffer;
	}

	return std::vector<BYTE>(kChunkSize);
}

//***************************************************************************
// @brief 다 쓴 청크 버퍼를 풀에 반납한다.
// @param buffer 반납할 청크 버퍼
//***************************************************************************
void CFileSendBufferPool::Release(std::vector<BYTE> buffer)
{
	if( buffer.size() != kChunkSize )
		return; // 일관되지 않은 크기 — 풀을 오염시키지 않고 그냥 버림(호출부 버그 방어)

	std::lock_guard<std::mutex> lock(_mutex);

	// [설계] 풀 크기 상한을 두지 않았다 — 이 서버의 동시 다운로드 요청
	// 수는 세션 수 이내로 자연스럽게 제한되고(최대 kMaxSessionCount),
	// 그 이상 쌓일 상황 자체가 없다고 판단했다. 만약 나중에 매우 많은
	// 동시 다운로드를 지원해야 한다면 여기에 상한(넘으면 그냥 버림)을
	// 추가하는 게 좋다.
	_pool.push_back(std::move(buffer));
}