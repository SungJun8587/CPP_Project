
//***************************************************************************
// ChatStressAccounts.cpp : implementation of the CChatStressAccounts class.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressAccounts.h"

//***************************************************************************
// @brief 계정 파일을 읽어 메모리에 적재한다.
// @details 파일이 없으면 조용히 false를 반환한다. 성공 시 g_runTag도 파일의
//          태그로 복원한다.
// @param path 계정 파일 경로
// @return 파일을 열었으면 true, 열지 못했으면 false
//***************************************************************************
bool CChatStressAccounts::Load(const _tstring& path)
{
	std::ifstream in(path.c_str());	// MSVC STL: wchar_t* 경로 생성자(내부적으로 CreateFileW)
	if( !in.is_open() )
		return false;

	std::lock_guard<std::mutex> guard(_mutex);
	_accounts.clear();

	std::string line;
	while( std::getline(in, line) )
	{
		std::istringstream ss(line);
		if( line.rfind("TAG ", 0) == 0 )
		{
			g_runTag = line.substr(4);
			continue;
		}

		int idx = 0;
		std::string idHex, tokHex;
		if( !(ss >> idx >> idHex >> tokHex) )
			continue;

		ChatStressAccount acc;
		if( Crypto::CCryptoUtil::FromHex(idHex, acc.publicId.data(), acc.publicId.size())
			&& Crypto::CCryptoUtil::FromHex(tokHex, acc.token.data(), acc.token.size()) )
			_accounts[idx] = acc;
	}
	_dirty = false;	// 방금 파일에서 읽은 상태이므로 저장할 변경이 없다
	return true;
}

//***************************************************************************
// @brief 현재 계정 목록을 파일로 저장한다.
// @details 임시 파일(.tmp)에 먼저 쓰고 교체하므로 저장 도중 종료돼도 기존 파일이
//          깨지지 않는다.
// @param path 저장할 계정 파일 경로
// @return 저장과 교체에 성공하면 true
//***************************************************************************
bool CChatStressAccounts::Save(const _tstring& path)
{
	std::lock_guard<std::mutex> guard(_mutex);

	// 임시 파일에 쓰고 교체 — 저장 도중 종료돼도 기존 파일이 깨지지 않게 한다
	_tstring tmp = path + _T(".tmp");
	{
		std::ofstream out(tmp.c_str(), std::ios::trunc);
		if( !out.is_open() )
			return false;

		out << "TAG " << g_runTag << '\n';
		for( const auto& kv : _accounts )
		{
			out << kv.first << ' '
				<< Crypto::CCryptoUtil::ToHex(kv.second.publicId.data(), kv.second.publicId.size()) << ' '
				<< Crypto::CCryptoUtil::ToHex(kv.second.token.data(), kv.second.token.size()) << '\n';
		}
		if( !out.good() )
			return false;
	}
	if( ::MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE )
		return false;

	_dirty = false;	// Update()도 _mutex를 잡으므로 쓰는 도중의 변경을 놓치지 않는다
	return true;
}

//***************************************************************************
// @brief 마지막 저장 이후 변경이 있을 때만 계정 파일로 저장한다.
// @details 실행 도중 주기적으로 호출해 두면 비정상 종료(디버거 중지, 크래시)가 되어도
//          그때까지 로그인에 성공한 계정의 토큰이 파일에 남는다.
// @param path 저장할 계정 파일 경로
// @return 저장할 변경이 없었거나 저장에 성공하면 true, 저장에 실패하면 false
//***************************************************************************
bool CChatStressAccounts::SaveIfDirty(const _tstring& path)
{
	if( !_dirty.load() )
		return true;

	return Save(path);
}

//***************************************************************************
// @brief 클라이언트 번호로 저장된 계정을 찾는다.
// @param index 클라이언트 번호
// @param out 찾은 계정이 채워질 참조
// @return 계정이 있으면 true, 없으면 false
//***************************************************************************
bool CChatStressAccounts::Find(int index, ChatStressAccount& out)
{
	std::lock_guard<std::mutex> guard(_mutex);
	auto it = _accounts.find(index);
	if( it == _accounts.end() )
		return false;
	out = it->second;
	return true;
}

//***************************************************************************
// @brief 클라이언트의 계정을 최신 값으로 갱신한다(없으면 추가).
// @param index 클라이언트 번호
// @param account 저장할 계정
//***************************************************************************
void CChatStressAccounts::Update(int index, const ChatStressAccount& account)
{
	std::lock_guard<std::mutex> guard(_mutex);
	_accounts[index] = account;
	_dirty = true;
}