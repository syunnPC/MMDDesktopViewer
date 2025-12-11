#include "WicTexture.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl.h>
#include <stdexcept>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace WicTexture
{

	WicImage LoadRgba(const std::filesystem::path& path)
	{
		ComPtr<IWICImagingFactory> factory;
		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory)
		);
		DX_CALL(hr);

		ComPtr<IWICBitmapDecoder> decoder;
		hr = factory->CreateDecoderFromFilename(
			path.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, &decoder
		);
		DX_CALL(hr);

		ComPtr<IWICBitmapFrameDecode> frame;
		hr = decoder->GetFrame(0, &frame);
		DX_CALL(hr);

		UINT w = 0, h = 0;
		hr = frame->GetSize(&w, &h);
		DX_CALL(hr);
		if (w == 0 || h == 0) throw std::runtime_error("WIC GetSize failed.");

		ComPtr<IWICFormatConverter> converter;
		hr = factory->CreateFormatConverter(&converter);
		DX_CALL(hr);

		hr = converter->Initialize(
			frame.Get(),
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			nullptr, 0.0,
			WICBitmapPaletteTypeCustom
		);
		DX_CALL(hr);

		WicImage img;
		img.width = static_cast<uint32_t>(w);
		img.height = static_cast<uint32_t>(h);
		img.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

		hr = converter->CopyPixels(
			nullptr,
			static_cast<UINT>(w * 4),
			static_cast<UINT>(img.rgba.size()),
			img.rgba.data()
		);
		DX_CALL(hr);

		return img;
	}

} // namespace
