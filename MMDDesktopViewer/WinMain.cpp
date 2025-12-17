#include "App.hpp"
#include <string>
#include <windows.h>

static std::wstring Utf8ToW(const char* s)
{
	if (!s) return L"";
	int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
	if (len <= 0)
	{
		// fallback
		len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
		if (len <= 0) return L"";
		std::wstring out(len - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), len);
		return out;
	}
	std::wstring out(len - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out.data(), len);
	return out;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
#ifdef __AVX2__
	if (!IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE))
	{
		MessageBoxW(nullptr, L"このプロセッサーではAVX2命令セットが利用できません。", L"MMDDesk", MB_ICONERROR);
		return 0;
	}
#endif

	try
	{
		App app{ hInstance };
		return app.Run();
	}
	catch (const std::exception& e)
	{
		std::wstring msg = L"Fatal error:\n";
		msg += Utf8ToW(e.what());
		MessageBoxW(nullptr, msg.c_str(), L"MMDDesk", MB_ICONERROR);
		OutputDebugStringW(msg.c_str());
		return -1;
	}
	catch (...)
	{
		MessageBoxW(nullptr, L"Fatal error: unknown exception", L"MMDDesk", MB_ICONERROR);
		return -1;
	}
}
