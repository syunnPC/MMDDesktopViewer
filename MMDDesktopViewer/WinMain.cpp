#include "App.hpp"
#include <string>
#include <algorithm>
#include <format>
#include <windows.h>
#include <objbase.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static HANDLE g_cpuJob = nullptr;

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

static void SetProcessEcoQoS(bool enable)
{
	PROCESS_POWER_THROTTLING_STATE s{};
	s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	s.StateMask = enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
}

class ScopedComInitializer
{
public:
	ScopedComInitializer()
		: m_hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
	{
	}

	~ScopedComInitializer()
	{
		if (SUCCEEDED(m_hr))
		{
			CoUninitialize();
		}
	}

	bool Initialized() const
	{
		return SUCCEEDED(m_hr);
	}

	bool ChangedMode() const
	{
		return m_hr == RPC_E_CHANGED_MODE;
	}

	HRESULT Result() const
	{
		return m_hr;
	}

private:
	HRESULT m_hr{};
};

static bool HasEnvVar(const wchar_t* name)
{
	wchar_t buf[2];
	DWORD n = GetEnvironmentVariableW(name, buf, 2);
	return n != 0 && n != ERROR_ENVVAR_NOT_FOUND;
}

//OpenMPのビジーループをパッシブに
static void RelaunchWithOpenMpPassiveIfNeeded()
{
	constexpr const wchar_t* kMarker = L"MMD_OMP_BOOTSTRAP_DONE";
	if (HasEnvVar(kMarker))
	{
		return;
	}

	wchar_t policy[64]{};
	DWORD n = GetEnvironmentVariableW(L"OMP_WAIT_POLICY", policy, (DWORD)std::size(policy));
	if (n > 0 && _wcsicmp(policy, L"PASSIVE") == 0)
	{
		SetEnvironmentVariableW(kMarker, L"1");
		return;
	}

	SetEnvironmentVariableW(L"OMP_WAIT_POLICY", L"PASSIVE");
	SetEnvironmentVariableW(kMarker, L"1");

	std::wstring cmd = GetCommandLineW();

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		ExitProcess(0); 
	}
	else
	{
		OutputDebugStringW(L"CreateProcessW() failed.");
		if (MessageBoxW(nullptr, L"OpenMPの環境変数の設定に失敗しました。パフォーマンスが低下する可能性があります。続行しますか?", L"MMDDesk", MB_ICONINFORMATION | MB_YESNO) == IDNO)
		{
			ExitProcess(0);
		}
		SetEnvironmentVariableW(kMarker, nullptr);
	}
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	if (!IsDebuggerPresent())
	{
		RelaunchWithOpenMpPassiveIfNeeded();
	}

#ifdef __AVX2__
	if (!IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE))
	{
		MessageBoxW(nullptr, L"このプロセッサーではAVX2命令セットが利用できません。", L"MMDDesk", MB_ICONERROR);
		return 0;
	}
#endif

	//優先度を下げる
	if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS))
	{
		OutputDebugStringW(std::format(L"SetPriorityClass() failed. GetLastError: {}", GetLastError()).c_str());
	}

	SetProcessEcoQoS(true);

	ScopedComInitializer comInit;
	if (!comInit.Initialized() && !comInit.ChangedMode())
	{
		MessageBoxW(nullptr, L"COM の初期化に失敗しました。", L"MMDDesk", MB_ICONERROR);
		return -1;
	}
	else if (comInit.ChangedMode())
	{
		OutputDebugStringW(L"CoInitializeEx returned RPC_E_CHANGED_MODE; continuing without COM uninitialization.\n");
	}

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
