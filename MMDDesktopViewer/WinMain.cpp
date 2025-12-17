#include "App.hpp"
#include <string>
#include <algorithm>
#include <format>
#include <windows.h>

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

//思ってたのと違うから使わないかも
[[maybe_unused]] static void ApplyCpuHardCapPercent(int percent)
{
	percent = std::clamp(percent, 1, 100);

	if (!g_cpuJob)
	{
		g_cpuJob = CreateJobObjectW(nullptr, nullptr);
		if (!g_cpuJob) return;

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
		eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_cpuJob, JobObjectExtendedLimitInformation, &eli, sizeof(eli));

		if (!AssignProcessToJobObject(g_cpuJob, GetCurrentProcess()))
		{
			CloseHandle(g_cpuJob);
			g_cpuJob = nullptr;
			return;
		}
	}

	JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
	cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

	cpu.CpuRate = static_cast<DWORD>(percent * 100);
	if (!SetInformationJobObject(g_cpuJob, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu)))
	{
		OutputDebugStringW(L"SetInformationJobObject() failed; ignoring.");
	}
}

[[maybe_unused]] static void ApplyCpuWeightBased(int weight)
{
	weight = std::clamp(weight, 1, 9);

	if (!g_cpuJob)
	{
		g_cpuJob = CreateJobObjectW(nullptr, nullptr);
		if (!g_cpuJob) return;

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
		eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_cpuJob, JobObjectExtendedLimitInformation, &eli, sizeof(eli));

		if (!AssignProcessToJobObject(g_cpuJob, GetCurrentProcess()))
		{
			CloseHandle(g_cpuJob);
			g_cpuJob = nullptr;
			return;
		}
	}

	JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
	cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED;
	cpu.Weight = (DWORD)weight;

	SetInformationJobObject(g_cpuJob, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu));
}

static void SetProcessEcoQoS(bool enable)
{
	PROCESS_POWER_THROTTLING_STATE s{};
	s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	s.StateMask = enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
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

	//優先度を下げる
	if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS))
	{
		OutputDebugStringW(std::format(L"SetPriorityClass() failed. GetLastError: {}", GetLastError()).c_str());
	}

	SetProcessEcoQoS(true);

#ifdef WEIGHT_ENABLE

#if WEIGHT_ENABLE > 9 || WEIGHT_ENABLE < 1
#error WEIGHT_ENABLEの指定が間違っています。[1,9]で指定してください。
#endif //WEIGHT_ENABLE > 9 || WEIGHT_ENABLE < 1

#if WEIGHT_ENABLE > 5
#warning WEIGHT_ENABLEがWindowsのデフォルトより高い数値に設定されています。
#endif //WEIGHT_ENABLE > 5

	ApplyCpuWeightBased(WEIGHT_ENABLE);
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
