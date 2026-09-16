
//***************************************************************************
// ImageResizeUtil.cpp : implementation of the ImageResizeUtil Functions.
//
//***************************************************************************

#include "pch.h"
#include "ImageResizeUtil.h"

bool ImageResizeUtil::Startup()
{
	return true;
}

void ImageResizeUtil::Shutdown()
{
}

bool ImageResizeUtil::ResizeIfLarger(const std::vector<BYTE>& imageData, const std::string& fileExtension, int maxDimension, std::vector<BYTE>& outData)
{
	if( imageData.empty() || maxDimension <= 0 )
		return false;

	const ImageFormat format = ImageIO::FormatFromExtension(fileExtension);
	if( format == ImageFormat::Unknown )
	{
		// 확장자를 못 알아보는 포맷(예: HandleUpload()가 확장자를 못
		// 찾았을 때 붙이는 기본값 ".bin") — 리사이즈 없이 원본 그대로
		// 저장하게 둔다.
		LOG_ERROR(_T("ImageResizeUtil::ResizeIfLarger: 지원하지 않는 확장자(%hs) — 원본 그대로 저장"), fileExtension.c_str());
		return false;
	}

	try
	{
		// std::vector<BYTE>(=unsigned char, 이 프로젝트의 BYTE 재정의)와
		// std::vector<uint8_t>는 바이트 단위로 완전히 같은 표현이지만
		// 타입은 서로 다른 별개의 vector라 그대로 넘길 수 없다 — 복사해서
		// 타입만 맞춘다(데이터 자체는 바이트 그대로 동일).
		const std::vector<uint8_t> srcBytes(imageData.begin(), imageData.end());

		ImageProcessor processor;
		processor.LoadFromMemory(srcBytes);

		const uint32_t width = processor.Width();
		const uint32_t height = processor.Height();

		if( width == 0 || height == 0 )
		{
			LOG_ERROR(_T("ImageResizeUtil::ResizeIfLarger: 디코딩 실패(width/height=0) — 원본 그대로 저장"));
			return false;
		}

		if( static_cast<int>(width) <= maxDimension && static_cast<int>(height) <= maxDimension )
			return false; // 이미 충분히 작음 — 리사이즈 불필요

		const double scale = static_cast<double>(maxDimension) / (std::max)(width, height);

		// [주의] ImageProcessor::Resize()는 newWidth/newHeight가 0이면
		// 내부에서 0으로 나누기가 발생한다(라이브러리 자체는 안 건드리기로
		// 함) — 그래서 여기서 최소 1을 보장한다.
		const uint32_t newWidth = (std::max)(1u, static_cast<uint32_t>(width * scale));
		const uint32_t newHeight = (std::max)(1u, static_cast<uint32_t>(height * scale));

		processor.Resize(newWidth, newHeight, ResizeMethod::Bilinear);

		const std::vector<uint8_t> encoded = processor.SaveToMemory(format);
		if( encoded.empty() )
		{
			LOG_ERROR(_T("ImageResizeUtil::ResizeIfLarger: 인코딩 결과가 비어있음"));
			return false;
		}

		outData.assign(encoded.begin(), encoded.end());

		LOG_INFO(_T("ImageResizeUtil::ResizeIfLarger: %ux%u -> %ux%u (%zu -> %zu bytes)"),
			width, height, newWidth, newHeight, imageData.size(), outData.size());

		return true;
	}
	catch( const std::exception& ex )
	{
		// ImageException(ImageIO/코덱 쪽에서 던짐) 포함 std::exception 전체를
		// 잡는다 — 리사이즈 과정에서 무엇이 터지든 절대 예외가 호출부까지
		// 새어나가지 않고 "실패 -> 원본 그대로 저장" 경로로 안전하게
		// 수렴하게 한다.
		LOG_ERROR(_T("ImageResizeUtil::ResizeIfLarger: 예외 발생 (%hs) — 원본 그대로 저장"), ex.what());
		outData.clear();
		return false;
	}
}