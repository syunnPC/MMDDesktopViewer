#include "WicTexture.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"
#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <stdexcept>

#pragma comment(lib, "windowscodecs.lib")

using winrt::com_ptr;

namespace WicTexture
{

	WicImage LoadRgba(const std::filesystem::path& path)
	{
		com_ptr<IWICImagingFactory> factory;
		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(factory.put())
		);
		DX_CALL(hr);

		com_ptr<IWICBitmapDecoder> decoder;
		hr = factory->CreateDecoderFromFilename(
			path.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, decoder.put()
		);
		DX_CALL(hr);

		com_ptr<IWICBitmapFrameDecode> frame;
		hr = decoder->GetFrame(0, frame.put());
		DX_CALL(hr);

		UINT w = 0, h = 0;
		hr = frame->GetSize(&w, &h);
		DX_CALL(hr);
		if (w == 0 || h == 0) throw std::runtime_error("WIC GetSize failed.");

		com_ptr<IWICFormatConverter> converter;
		hr = factory->CreateFormatConverter(converter.put());
		DX_CALL(hr);

		hr = converter->Initialize(
			frame.get(),
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
}
