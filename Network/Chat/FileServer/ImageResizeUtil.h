
//***************************************************************************
// ImageResizeUtil.h : interface for the ImageResizeUtil Functions.
//
//***************************************************************************

#ifndef UC_IMAGERESIZEUTIL_H
#define UC_IMAGERESIZEUTIL_H

#include <vector>
#include <string>

namespace ImageResizeUtil
{
	//***************************************************************************
	// @brief imageData(원본 이미지 바이트)의 가로/세로 중 큰 쪽이 maxDimension을
	//        넘으면, 비율을 유지한 채 줄여서 outData에 새로 인코딩해 담는다.
	// @param imageData 원본 이미지 바이트(BMP/PNG/JPEG — ImageProcessor가
	//        지원하는 포맷).
	// @param fileExtension 저장할 확장자(".jpg" 등, 점 포함) — 이 확장자에
	//        맞는 인코더로 다시 인코딩한다(원본 포맷과 달라도 상관없음 —
	//        예: 큰 PNG를 리사이즈해서 다시 PNG로).
	// @param maxDimension 허용하는 최대 가로/세로(픽셀). 둘 다 이 값 이하면
	//        손대지 않는다.
	// @param outData [out] 리사이즈된 새 이미지 바이트(성공 시에만 채워짐).
	// @return true면 리사이즈됨(outData 사용) — false면 리사이즈 불필요
	//         (이미 작음) 또는 실패(디코딩 실패, 인코더 못 찾음 등) — 두
	//         경우 모두 호출부는 원본 imageData를 그대로 저장하면 된다.
	//         "불필요"와 "실패"를 구분하지 않는 건 호출부 입장에서 둘 다
	//         "원본 그대로 저장"이라는 같은 동작으로 이어지기 때문이다.
	//***************************************************************************
	bool ResizeIfLarger(const std::vector<BYTE>& imageData, const std::string& fileExtension, int maxDimension, std::vector<BYTE>& outData);
}

#endif // ndef UC_IMAGERESIZEUTIL_H