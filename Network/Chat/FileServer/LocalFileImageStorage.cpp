
//***************************************************************************
// LocalFileImageStorage.cpp: implementation of the CLocalFileImageStorage class.
//
//***************************************************************************

#include "pch.h"
#include "LocalFileImageStorage.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

//***************************************************************************
// @brief CLocalFileImageStorage 클래스의 생성자
//***************************************************************************
CLocalFileImageStorage::CLocalFileImageStorage(_tstring baseDir)
	: _baseDir(std::move(baseDir))
{
}

//***************************************************************************
// @brief 이미지를 {baseDir}/{ownerId}/{무작위파일명}{확장자}에 저장한다.
//***************************************************************************
bool CLocalFileImageStorage::SaveImage(
	const std::string& ownerId,
	const std::string& fileExtension,
	const std::vector<BYTE>& data,
	std::string& outRelativePath)
{
	// 파일명 충돌 방지 — 같은 계정이 여러 장을 올려도 서로 안 겹치게 매번
	// 새로 생성한 무작위 16바이트를 16진 인코딩해서 쓴다.
	BYTE randomName[16] = {};
	if( !Crypto::CCryptoUtil::GenerateRandomBytes(randomName, sizeof(randomName)) )
		return false;

	const std::string fileNameHex = Crypto::CCryptoUtil::ToHex(randomName, sizeof(randomName));
	const std::string relativePath = ownerId + "/" + fileNameHex + fileExtension;

	const fs::path fullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(relativePath));

	std::error_code ec;
	fs::create_directories(fullPath.parent_path(), ec);
	if( ec )
	{
		LOG_ERROR(_T("CLocalFileImageStorage::SaveImage: create_directories 실패 (%hs)"), ec.message().c_str());
		return false;
	}

	std::ofstream ofs(fullPath, std::ios::binary | std::ios::trunc);
	if( !ofs.is_open() )
	{
		LOG_ERROR(_T("CLocalFileImageStorage::SaveImage: 파일 열기 실패"));
		return false;
	}

	if( !data.empty() )
		ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

	if( !ofs.good() )
	{
		LOG_ERROR(_T("CLocalFileImageStorage::SaveImage: 쓰기 실패"));
		return false;
	}

	outRelativePath = relativePath;
	return true;
}

//***************************************************************************
// @brief relativePath로 저장된 파일을 그대로 읽어온다.
//***************************************************************************
bool CLocalFileImageStorage::LoadImage(const std::string& relativePath, std::vector<BYTE>& outData)
{
	// [방어] "../" 등으로 baseDir 바깥을 가리키려는 경로 조작 시도를 막는다 —
	// GET 요청 경로에서 그대로 넘어올 수 있는 값이라 반드시 검증해야 한다.
	if( relativePath.find("..") != std::string::npos )
		return false;

	const fs::path fullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(relativePath));

	std::ifstream ifs(fullPath, std::ios::binary | std::ios::ate);
	if( !ifs.is_open() )
		return false;

	const std::streamsize fileSize = ifs.tellg();
	if( fileSize < 0 )
		return false;

	ifs.seekg(0, std::ios::beg);

	outData.resize(static_cast<size_t>(fileSize));
	if( fileSize > 0 && !ifs.read(reinterpret_cast<char*>(outData.data()), fileSize) )
		return false;

	return true;
}

//***************************************************************************
// @brief relativePath가 가리키는 파일을 삭제한다.
//***************************************************************************
bool CLocalFileImageStorage::DeleteImage(const std::string& relativePath)
{
	// [방어] LoadImage()와 동일한 이유 — "../" 등으로 baseDir 바깥을
	// 가리키려는 경로 조작 시도를 막는다.
	if( relativePath.find("..") != std::string::npos )
		return false;

	const fs::path fullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(relativePath));

	std::error_code ec;
	fs::remove(fullPath, ec);

	// [설계] 애초에 파일이 없었던 경우까지 실패로 취급하지 않는다 —
	// IImageStorage::DeleteImage() 문서 참고. "그 경로에 파일이 없는
	// 상태"라는 최종 결과 자체를 성공으로 본다(ec 유무와 무관하게).
	return true;
}