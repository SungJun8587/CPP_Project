//***************************************************************************
// LocalFileStorage.cpp: implementation of the CLocalFileStorage class.
//
// [설계 변경 — 일반화] LocalFileImageStorage.cpp를 대체한다. 로직은 완전히
// 동일하고 클래스/메서드 이름과 로그 태그만 IFileStorage/CLocalFileStorage에
// 맞춰 바꿨다.
//***************************************************************************

#include "pch.h"
#include "LocalFileStorage.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

//***************************************************************************
// @brief CLocalFileStorage 클래스의 생성자
//***************************************************************************
CLocalFileStorage::CLocalFileStorage(_tstring baseDir)
	: _baseDir(std::move(baseDir))
{
}

//***************************************************************************
// @brief 파일을 {baseDir}/{ownerId}/{무작위파일명}{확장자}에 저장한다.
//***************************************************************************
bool CLocalFileStorage::SaveFile(
	const std::string& ownerId,
	const std::string& fileExtension,
	const std::vector<BYTE>& data,
	std::string& outRelativePath)
{
	// 파일명 충돌 방지 — 같은 계정이 여러 파일을 올려도 서로 안 겹치게 매번
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
		LOG_ERROR(_T("CLocalFileStorage::SaveFile: create_directories 실패 (%hs)"), ec.message().c_str());
		return false;
	}

	std::ofstream ofs(fullPath, std::ios::binary | std::ios::trunc);
	if( !ofs.is_open() )
	{
		LOG_ERROR(_T("CLocalFileStorage::SaveFile: 파일 열기 실패"));
		return false;
	}

	if( !data.empty() )
		ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

	if( !ofs.good() )
	{
		LOG_ERROR(_T("CLocalFileStorage::SaveFile: 쓰기 실패"));
		return false;
	}

	outRelativePath = relativePath;
	return true;
}

//***************************************************************************
// @brief 이미 디스크에 존재하는 파일을 {baseDir}/{ownerId}/{무작위파일명}
//        {확장자}로 옮긴다.
// @details rename()은 원자적이고 같은 볼륨이면 추가 디스크 I/O가 사실상
//          없다 — 임시 디렉터리(sourceFilePath가 있던 곳)와 baseDir이
//          서로 다른 볼륨/드라이브일 수 있으므로(예: 임시 파일은 C:\Temp,
//          baseDir은 D:\Storage), 그 경우 rename()이
//          std::errc::cross_device_link로 실패하는 게 정상이다 — 이때만
//          복사 후 원본 삭제로 폴백한다(대용량이면 이 폴백 경로가 느릴 수
//          있지만, 애초에 이런 배치라면 어차피 한 번은 전체를 옮겨야 하므로
//          불가피하다).
//***************************************************************************
bool CLocalFileStorage::SaveFileFromPath(
	const std::string& ownerId,
	const std::string& fileExtension,
	const std::string& sourceFilePath,
	std::string& outRelativePath)
{
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
		LOG_ERROR(_T("CLocalFileStorage::SaveFileFromPath: create_directories 실패 (%hs)"), ec.message().c_str());
		return false;
	}

	const fs::path sourcePath = fs::path(Utf8ToTString(sourceFilePath));

	fs::rename(sourcePath, fullPath, ec);
	if( ec )
	{
		// 볼륨이 달라 rename이 안 되는 경우 등 — 복사 후 원본 삭제로 폴백.
		std::error_code copyEc;
		fs::copy_file(sourcePath, fullPath, fs::copy_options::overwrite_existing, copyEc);
		if( copyEc )
		{
			LOG_ERROR(_T("CLocalFileStorage::SaveFileFromPath: rename/copy 둘 다 실패 (%hs)"), copyEc.message().c_str());
			return false;
		}

		std::error_code removeEc;
		fs::remove(sourcePath, removeEc); // 원본 정리 실패는 치명적이지 않음(임시 디렉터리라 OS가 언젠가 정리) — 로그만 남김
		if( removeEc )
			LOG_ERROR(_T("CLocalFileStorage::SaveFileFromPath: 임시 원본 삭제 실패 (%hs)"), removeEc.message().c_str());
	}

	outRelativePath = relativePath;
	return true;
}

//***************************************************************************
// @brief relativePath로 저장된 파일을 그대로 읽어온다.
//***************************************************************************
bool CLocalFileStorage::LoadFile(const std::string& relativePath, std::vector<BYTE>& outData)
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
// @brief relativePath의 파일을 스트림으로 연다.
//***************************************************************************
std::unique_ptr<CFileStream> CLocalFileStorage::OpenStream(const std::string& relativePath)
{
	// [방어] LoadFile()/DeleteFile()과 동일한 이유 — "../" 등으로 baseDir
	// 바깥을 가리키려는 경로 조작 시도를 막는다.
	if( relativePath.find("..") != std::string::npos )
		return nullptr;

	const fs::path fullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(relativePath));

	auto stream = std::make_unique<CFileStream>();
	if( !stream->Open(fullPath.string()) )
		return nullptr;

	return stream;
}

//***************************************************************************
// @brief relativePath의 파일을 스트림으로 연다.
//***************************************************************************
bool CLocalFileStorage::DeleteFile(const std::string& relativePath)
{
	// [방어] LoadFile()와 동일한 이유 — "../" 등으로 baseDir 바깥을
	// 가리키려는 경로 조작 시도를 막는다.
	if( relativePath.find("..") != std::string::npos )
		return false;

	const fs::path fullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(relativePath));

	std::error_code ec;
	fs::remove(fullPath, ec);

	// [설계] 애초에 파일이 없었던 경우까지 실패로 취급하지 않는다 —
	// IFileStorage::DeleteFile() 문서 참고. "그 경로에 파일이 없는
	// 상태"라는 최종 결과 자체를 성공으로 본다(ec 유무와 무관하게).
	return true;
}