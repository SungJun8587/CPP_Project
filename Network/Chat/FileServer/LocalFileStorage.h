
//***************************************************************************
// LocalFileStorage.h : interface for the CLocalFileStorage class.
//
// [설계 변경 — 일반화] LocalFileImageStorage.h를 대체한다. IFileStorage와
// 짝을 맞춰 이름만 바꿨다 — 실제 저장 로직(무작위 파일명, ownerId별 하위
// 디렉터리, "../" 경로 조작 방어)은 이전 CLocalFileImageStorage와 동일하다.
//***************************************************************************

#ifndef UC_LOCALFILESTORAGE_H
#define UC_LOCALFILESTORAGE_H

#include "FileStorage.h"
#include <BaseRedefineDataType.h>

//***************************************************************************
// @class CLocalFileStorage
// @brief IFileStorage를 로컬 디스크(파일 서버가 돌아가는 머신)에 구현한다.
// @details baseDir 아래에 "{ownerId}/{무작위16바이트16진}{확장자}" 형태로
//          저장한다. ownerId별 하위 디렉터리로 나누는 이유는 순전히 파일
//          시스템 관리 편의(한 디렉터리에 파일이 무한정 쌓이는 것 방지)를
//          위함이며, 접근 권한 분리 등의 보안 경계는 아니다(파일 서버
//          자체가 그런 경계를 두지 않음 — DELETE 인증 부재 등 기존에
//          알려진 한계와 같은 선상).
//***************************************************************************
class CLocalFileStorage : public IFileStorage
{
public:
	//***************************************************************************
	// @brief CLocalFileStorage 생성자
	// @param baseDir 모든 파일이 저장될 최상위 디렉터리(존재하지 않으면
	//        SaveFile() 호출 시 필요한 하위 디렉터리까지 생성 시도함)
	//***************************************************************************
	explicit CLocalFileStorage(_tstring baseDir);

	virtual bool SaveFile(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::vector<BYTE>& data,
		std::string& outRelativePath) override;

	virtual bool SaveFileFromPath(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::string& sourceFilePath,
		std::string& outRelativePath) override;

	virtual bool LoadFile(const std::string& relativePath, std::vector<BYTE>& outData) override;

	virtual bool DeleteFile(const std::string& relativePath) override;

	virtual std::unique_ptr<CFileStream> OpenStream(const std::string& relativePath) override;

private:
	_tstring _baseDir;
};

#endif // ndef UC_LOCALFILESTORAGE_H