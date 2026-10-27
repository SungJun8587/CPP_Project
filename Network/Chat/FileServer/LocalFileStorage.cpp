
//***************************************************************************
// LocalFileStorage.cpp: implementation of the CLocalFileStorage class.
//
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

namespace
{
	// ".png" 같은 확장자를 정제한다 — 점 + 영숫자 1~8자가 아니면 ".bin".
	std::string SanitizeExtension(const std::string& ext)
	{
		if( ext.size() < 2 || ext.size() > 1 + FileServerKeys::kStoredExtMaxLen || ext[0] != '.' )
			return ".bin";
		for( size_t i = 1; i < ext.size(); ++i )
			if( !FileServerKeys::IsAlnum(ext[i]) )
				return ".bin";
		return ext;
	}
}

bool CLocalFileStorage::ResolvePath(const std::string& relativePath, fs::path& outFullPath) const
{
	// 채팅 서버와 공유하는 규칙(Common/FileServerKeys.h)으로 검증한다.
	if( !FileServerKeys::IsValidRelativePath(relativePath) )
		return false;

	const size_t slash = relativePath.find('/');
	const std::string owner = relativePath.substr(0, slash);
	const std::string name = relativePath.substr(slash + 1);

	outFullPath = fs::path(_baseDir) / fs::path(Utf8ToTString(owner)) / fs::path(Utf8ToTString(name));
	return true;
}

bool CLocalFileStorage::NewRelativePath(const std::string& ownerId, const std::string& fileExtension, std::string& outRelativePath)
{
	if( !FileServerKeys::IsValidOwnerId(ownerId) )
	{
		LOG_ERROR(_T("CLocalFileStorage: 유효하지 않은 ownerId"));
		return false;
	}

	// 파일명 충돌/추측 방지 — 매번 새로 생성한 무작위 16바이트를 16진 인코딩.
	BYTE randomName[16] = {};
	if( !Crypto::CCryptoUtil::GenerateRandomBytes(randomName, sizeof(randomName)) )
		return false;

	outRelativePath = ownerId + "/" + Crypto::CCryptoUtil::ToHex(randomName, sizeof(randomName)) + SanitizeExtension(fileExtension);
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
	std::string relativePath;
	if( !NewRelativePath(ownerId, fileExtension, relativePath) )
		return false;

	fs::path fullPath;
	if( !ResolvePath(relativePath, fullPath) )
		return false;

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
// @brief relativePath의 파일을 스트림으로 연다.
//***************************************************************************
std::unique_ptr<CFileStream> CLocalFileStorage::OpenStream(const std::string& relativePath)
{
	fs::path fullPath;
	if( !ResolvePath(relativePath, fullPath) )
		return nullptr;

	auto stream = std::make_unique<CFileStream>();
	if( !stream->Open(fullPath.string()) )
		return nullptr;

	return stream;
}

//***************************************************************************
// @brief relativePath의 파일을 삭제한다.
// @details 파일이 애초에 없으면 성공(최종 상태가 "없음"). 경로가 유효하지
//          않거나 파일시스템 오류면 false.
//***************************************************************************
bool CLocalFileStorage::DeleteFile(const std::string& relativePath)
{
	fs::path fullPath;
	if( !ResolvePath(relativePath, fullPath) )
		return false;

	std::error_code ec;
	fs::remove(fullPath, ec); // 없는 파일이면 ec 없이 false 반환
	if( ec )
	{
		LOG_ERROR(_T("CLocalFileStorage::DeleteFile: 삭제 실패 (%hs)"), ec.message().c_str());
		return false;
	}
	return true;
}