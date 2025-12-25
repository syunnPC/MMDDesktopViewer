#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <string_view>
#include <windows.h>

namespace StringUtil
{
	std::wstring MultiByteToWide(std::string_view input, UINT codePage, DWORD flags = 0);
	std::string WideToMultiByte(std::wstring_view input, UINT codePage, DWORD flags = 0);

	std::wstring Utf8ToWide(std::string_view input);
	std::wstring Utf8ToWideAllowAcpFallback(std::string_view input);
	std::string WideToUtf8(std::wstring_view input);
}