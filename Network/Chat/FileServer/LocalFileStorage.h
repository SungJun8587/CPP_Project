
//***************************************************************************
// LocalFileStorage.h : interface for the CLocalFileStorage class.
//
//***************************************************************************

#ifndef LOCALFILESTORAGE_H
#define LOCALFILESTORAGE_H

#include "FileStorage.h"
#include <BaseRedefineDataType.h>

#include <filesystem>

//***************************************************************************
// @class CLocalFileStorage
// @brief IFileStorage를 로컬 디스크(파일 서버가 돌아가는 머신)에 구현한다.
// @details baseDir 아래에 "{ownerId}/{무작위16바이트16진}{확장자}" 형태로
//          저장한다. ownerId별 하위 디렉터리는 파일시스템 관리 편의용이며
//          접근 권한 경계는 아니다. 외부에서 들어오는 상대 경로(GET 경로,
//          예약 삭제 큐)는 ResolvePath()에서 "{owner}/{name}" 형태만
//          허용하도록 엄격하게 검증한다.
//***************************************************************************
class CLocalFileStorage : public IFileStorage
{
public:
	//***************************************************************************
	// @param baseDir 모든 파일이 저장될 최상위 디렉터리(없으면 저장 시 생성)
	//***************************************************************************
	explicit CLocalFileStorage(_tstring baseDir);

	virtual bool SaveFileFromPath(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::string& sourceFilePath,
		std::string& outRelativePath) override;

	virtual bool DeleteFile(const std::string& relativePath) override;

	virtual std::unique_ptr<CFileStream> OpenStream(const std::string& relativePath) override;

private:
	//***************************************************************************
	// @brief relativePath가 "{owner}/{무작위 16진 32자}.{영숫자 1~8자}" 형태
	//        (owner는 영숫자/'_'/'-')인지 검증하고 baseDir 기준 전체 경로를 만든다.
	// @return 유효하면 true.
	//***************************************************************************
	bool ResolvePath(const std::string& relativePath, std::filesystem::path& outFullPath) const;

	//***************************************************************************
	// @brief ownerId 검증 + 무작위 파일명 + 확장자 정제로 새 상대 경로를 만든다.
	//***************************************************************************
	static bool NewRelativePath(const std::string& ownerId, const std::string& fileExtension, std::string& outRelativePath);

private:
	_tstring _baseDir;
};

#endif // ndef LOCALFILESTORAGE_H