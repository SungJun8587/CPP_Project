
//***************************************************************************
// LocalFileImageStorage.h : interface for the CLocalFileImageStorage class.
//
//***************************************************************************

#ifndef UC_LOCALFILEIMAGESTORAGE_H
#define UC_LOCALFILEIMAGESTORAGE_H

#include "IImageStorage.h"

//***************************************************************************
// @class CLocalFileImageStorage
// @brief 파일 서버가 실행되는 머신의 로컬 디스크에 이미지를 저장하는
//        IImageStorage 구현.
// @details 저장 경로: {baseDir}/{ownerId}/{파일명}. 파일명은 충돌 방지를
// 위해 매번 새로 생성한 무작위 16바이트를 16진 인코딩해 만든다(같은
// 계정이 여러 장을 올려도 서로 안 겹침).
//***************************************************************************
class CLocalFileImageStorage : public IImageStorage
{
public:
	//***************************************************************************
	// @brief baseDir이 상대 경로면 실행 파일 기준, 절대 경로면 그대로 사용한다.
	//        디렉터리가 없으면 SaveImage() 호출 시점에 필요한 만큼 생성한다.
	//***************************************************************************
	explicit CLocalFileImageStorage(_tstring baseDir);
	~CLocalFileImageStorage() override = default;

	bool SaveImage(
		const std::string& ownerId,
		const std::string& fileExtension,
		const std::vector<BYTE>& data,
		std::string& outRelativePath) override;

	bool LoadImage(const std::string& relativePath, std::vector<BYTE>& outData) override;

	bool DeleteImage(const std::string& relativePath) override;

private:
	_tstring	_baseDir;
};

#endif // ndef UC_LOCALFILEIMAGESTORAGE_H