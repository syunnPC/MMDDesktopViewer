#include "StringUtil.hpp"

#include <stdexcept>
#include <system_error>

namespace
{
	std::wstring ConvertToWide(std::string_view input, UINT codePage, DWORD flags)
	{
		if (input.empty()) return {};

		int len = MultiByteToWideChar(codePage, flags, input.data(), static_cast<int>(input.size()), nullptr, 0);
		if (len <= 0)
		{
			throw std::system_error(::GetLastError(), std::system_category(), "MultiByteToWideChar size query failed");
		}

		std::wstring output(static_cast<size_t>(len), L'\0');
		if (MultiByteToWideChar(codePage, flags, input.data(), static_cast<int>(input.size()), output.data(), len) <= 0)
		{
			throw std::system_error(::GetLastError(), std::system_category(), "MultiByteToWideChar conversion failed");
		}

		return output;
	}

	std::string ConvertToMultiByte(std::wstring_view input, UINT codePage, DWORD flags)
	{
		if (input.empty()) return {};

		int len = WideCharToMultiByte(codePage, flags, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
		{
			throw std::system_error(::GetLastError(), std::system_category(), "WideCharToMultiByte size query failed");
		}

		std::string output(static_cast<size_t>(len), '\0');
		if (WideCharToMultiByte(codePage, flags, input.data(), static_cast<int>(input.size()), output.data(), len, nullptr, nullptr) <= 0)
		{
			throw std::system_error(::GetLastError(), std::system_category(), "WideCharToMultiByte conversion failed");
		}

		return output;
	}
}

namespace StringUtil
{
	std::wstring MultiByteToWide(std::string_view input, UINT codePage, DWORD flags)
	{
		return ConvertToWide(input, codePage, flags);
	}

	std::string WideToMultiByte(std::wstring_view input, UINT codePage, DWORD flags)
	{
		return ConvertToMultiByte(input, codePage, flags);
	}

	std::wstring Utf8ToWide(std::string_view input)
	{
		return ConvertToWide(input, CP_UTF8, MB_ERR_INVALID_CHARS);
	}

	std::wstring Utf8ToWideAllowAcpFallback(std::string_view input)
	{
		if (input.empty()) return {};
		try
		{
			return Utf8ToWide(input);
		}
		catch (const std::exception&)
		{
			return ConvertToWide(input, CP_ACP, 0);
		}
	}

	std::string WideToUtf8(std::wstring_view input)
	{
		return ConvertToMultiByte(input, CP_UTF8, 0);
	}
}