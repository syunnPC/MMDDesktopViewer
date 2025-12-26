#include "MediaAudioAnalyzer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <appmodel.h>
#include <winrt/base.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Foundation.h>
#include <roapi.h>
#include <cmath>
#include <complex>
#include <algorithm>
#include <format>
#include <cstdint>
#include <cstring>
#include <span>

#pragma comment(lib, "Mmdevapi.lib")
#pragma comment(lib, "mincore.lib")

using winrt::com_ptr;

namespace
{
	constexpr int kFftSize = 1024;
	constexpr double kBeatMinIntervalSeconds = 0.25;
	constexpr double kBeatEnergyThreshold = 1.35;
	// 音量に依存しない「無音判定」のための最小 RMS。
	// ループバックでゼロ埋めされる環境ではこれより十分大きい。
	constexpr double kMinSilenceGateRms = 5e-9;
	// 口パクの自動ゲイン制御が目標とする RMS（-28dBFS 相当）
	constexpr double kAgcTargetRms = 0.04;
	constexpr DWORD kCaptureWaitMs = 150;
	constexpr auto kSessionPollInterval = std::chrono::milliseconds(500);

#ifdef _DEBUG
	void DebugHr(const wchar_t* where, HRESULT hr)
	{
		if (FAILED(hr))
		{
			OutputDebugStringW(std::format(L"[Audio] {} hr=0x{:08X}\r\n", where ? where : L"(null)", static_cast<uint32_t>(hr)).c_str());
		}
	}
#else
	void DebugHr(const wchar_t*, HRESULT)
	{
	}
#endif

	bool TryGetExtensibleFields(const WAVEFORMATEX* format, WORD& outValidBits, DWORD& outChannelMask, GUID& outSubFormat)
	{
		outValidBits = 0;
		outChannelMask = 0;
		outSubFormat = GUID{};

		if (!format) return false;
		if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
		if (format->cbSize < 22) return false;

		// WAVEFORMATEX の「拡張領域」は cbSize 分だけ、WAVEFORMATEX の末尾（18バイト）直後に連結される。
		// ここで sizeof(WAVEFORMATEX) を使うと、コンパイラの末尾パディング（例: 20バイト）に引きずられて
		// オフセットがズレることがあるため、固定 18 バイトを基準にする。
		constexpr size_t kWfxWireSize = 18;

		const uint8_t* p = reinterpret_cast<const uint8_t*>(format);

		WORD validBits = 0;
		DWORD mask = 0;
		GUID sub{};
		memcpy(&validBits, p + kWfxWireSize, sizeof(WORD));
		memcpy(&mask, p + kWfxWireSize + sizeof(WORD), sizeof(DWORD));
		memcpy(&sub, p + kWfxWireSize + sizeof(WORD) + sizeof(DWORD), sizeof(GUID));

		outValidBits = validBits;
		outChannelMask = mask;
		outSubFormat = sub;
		return true;
	}

	bool IsFloatFormat(const WAVEFORMATEX* format)
	{
		if (!format) return false;
		if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;

		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			WORD validBits = 0;
			DWORD mask = 0;
			GUID sub{};
			if (TryGetExtensibleFields(format, validBits, mask, sub))
			{
				return ::IsEqualGUID(sub, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
			}
		}
		return false;
	}

	bool IsPcmFormat(const WAVEFORMATEX* format)
	{
		if (!format) return false;
		if (format->wFormatTag == WAVE_FORMAT_PCM) return true;

		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			WORD validBits = 0;
			DWORD mask = 0;
			GUID sub{};
			if (TryGetExtensibleFields(format, validBits, mask, sub))
			{
				return ::IsEqualGUID(sub, KSDATAFORMAT_SUBTYPE_PCM);
			}
		}
		return false;
	}


	float Clamp01(float v)
	{
		return std::clamp(v, 0.0f, 1.0f);
	}

	int ResolveValidBits(const WAVEFORMATEX* format)
	{
		int validBits = (format && format->wBitsPerSample > 0) ? format->wBitsPerSample : 32;
		if (format && format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			WORD vb = 0;
			DWORD mask = 0;
			GUID sub{};
			if (TryGetExtensibleFields(format, vb, mask, sub) && vb > 0 && vb <= 32)
			{
				validBits = vb;
			}
		}
		return validBits;
	}

	bool DetectLeftAligned(const int32_t* data, size_t sampleCount, int validBits)
	{
		if (validBits >= 32 || !data || sampleCount == 0) return true;
		const int checkN = static_cast<int>(std::min<size_t>(64, sampleCount));
		uint32_t lowMask = (1u << (32 - validBits)) - 1u;
		int nonZeroLow = 0;
		for (int i = 0; i < checkN; ++i)
		{
			if ((static_cast<uint32_t>(data[i]) & lowMask) != 0) nonZeroLow++;
		}
		return (nonZeroLow == 0);
	}

	struct AudioFormatConverter
	{
		static void FillSilence(std::span<float> out)
		{
			std::fill(out.begin(), out.end(), 0.0f);
		}

		static void CopyFloat(const float* in, size_t samples, std::span<float> out)
		{
			if (!in || samples == 0 || out.empty()) return;
			std::copy_n(in, std::min(samples, out.size()), out.begin());
		}

		static void ConvertUnsigned8(const uint8_t* in, size_t samples, std::span<float> out)
		{
			if (!in || samples == 0 || out.empty()) return;
			for (size_t i = 0; i < std::min(samples, out.size()); ++i)
			{
				out[i] = (static_cast<float>(in[i]) - 128.0f) / 128.0f;
			}
		}

		template <class SampleT>
		static void ConvertSigned(const SampleT* in, size_t samples, float scale, std::span<float> out)
		{
			if (!in || samples == 0 || out.empty()) return;
			const size_t count = std::min(samples, out.size());
			for (size_t i = 0; i < count; ++i)
			{
				out[i] = static_cast<float>(static_cast<double>(in[i]) * static_cast<double>(scale));
			}
		}

		static void ConvertPcm24(const uint8_t* in, size_t samples, std::span<float> out)
		{
			if (!in || samples == 0 || out.empty()) return;
			const size_t count = std::min(samples, out.size());
			for (size_t i = 0; i < count; ++i)
			{
				const uint32_t b0 = in[i * 3 + 0];
				const uint32_t b1 = in[i * 3 + 1];
				const uint32_t b2 = in[i * 3 + 2];
				int32_t s = static_cast<int32_t>(b0 | (b1 << 8) | (b2 << 16));
				s = (s << 8) >> 8;
				out[i] = static_cast<float>(static_cast<double>(s) / 8388608.0);
			}
		}

		static void ConvertPcm32(const int32_t* in, size_t samples, int validBits, bool leftAligned, std::span<float> out)
		{
			if (!in || samples == 0 || out.empty()) return;
			const size_t count = std::min(samples, out.size());

			if (leftAligned || validBits >= 32)
			{
				const double denom = 2147483648.0;
				for (size_t i = 0; i < count; ++i)
				{
					out[i] = static_cast<float>(static_cast<double>(in[i]) / denom);
				}
				return;
			}

			const int shift = std::max(0, 32 - validBits);
			const double denom = static_cast<double>(1ULL << (std::max(validBits, 1) - 1));
			for (size_t i = 0; i < count; ++i)
			{
				int32_t s = in[i];
				s = (s << shift) >> shift;
				out[i] = static_cast<float>(static_cast<double>(s) / denom);
			}
		}
	};
}

struct MediaAudioAnalyzer::GsmtcCache
{
	winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };

	bool EnsureManager()
	{
		if (manager)
		{
			return true;
		}
		try
		{
			manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			return static_cast<bool>(manager);
		}
		catch (const winrt::hresult_error&)
		{
			manager = nullptr;
			return false;
		}
	}
};

MediaAudioAnalyzer::MediaAudioAnalyzer()
{
	m_lastNonSilentAudio = std::chrono::steady_clock::now();
	m_worker = std::jthread([&](std::stop_token token) { WorkerLoop(token); });
}

MediaAudioAnalyzer::~MediaAudioAnalyzer()
{
	if (m_worker.joinable())
	{
		m_worker.request_stop();
		m_worker.join();
	}
	StopCapture();
	if (m_mixFormat)
	{
		CoTaskMemFree(m_mixFormat);
		m_mixFormat = nullptr;
	}
	if (m_captureEvent)
	{
		CloseHandle(m_captureEvent);
		m_captureEvent = nullptr;
	}
}

void MediaAudioAnalyzer::SetEnabled(bool enabled)
{
	m_enabled.store(enabled, std::memory_order_relaxed);
}

bool MediaAudioAnalyzer::Enabled() const
{
	return m_enabled.load(std::memory_order_relaxed);
}

AudioReactiveState MediaAudioAnalyzer::GetState() const
{
	std::scoped_lock lock(m_stateMutex);
	return m_state;
}

bool MediaAudioAnalyzer::ConsumeDrmWarning()
{
	return m_drmWarningPending.exchange(false, std::memory_order_acq_rel);
}


void MediaAudioAnalyzer::WorkerLoop(std::stop_token stopToken)
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	m_captureStart = std::chrono::steady_clock::now();

	auto nextPoll = std::chrono::steady_clock::now();
	CaptureTarget target{};

	while (!stopToken.stop_requested())
	{
		if (!m_enabled.load(std::memory_order_relaxed))
		{
			StopCapture();
			{
				std::scoped_lock lock(m_stateMutex);
				m_state.active = false;
			}
			m_lastNonSilentAudio = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		auto now = std::chrono::steady_clock::now();
		if (now >= nextPoll)
		{
			MediaSessionInfo session = QuerySession();
			target.active = session.active;
			target.eligible = session.eligible;
			target.aumid = session.aumid;
			if (target.eligible && !target.aumid.empty())
			{
				target.pid = ResolveProcessId(target.aumid);
			}
			else
			{
				target.pid.reset();
			}
			nextPoll = now + kSessionPollInterval;
		}

		if (!target.active || !target.eligible)
		{
			StopCapture();
			{
				std::scoped_lock lock(m_stateMutex);
				m_state.active = false;
			}
			m_lastNonSilentAudio = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (!EnsureCaptureForTarget(target))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (!CaptureAudioOnce(stopToken))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
	}

	StopCapture();
}

MediaAudioAnalyzer::MediaSessionInfo MediaAudioAnalyzer::QuerySession()
{
	MediaSessionInfo info{};
	try
	{
		auto manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
		auto session = manager.GetCurrentSession();
		if (!session)
		{
			return info;
		}

		auto playback = session.GetPlaybackInfo();
		auto status = playback.PlaybackStatus();
		if (status != winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
		{
			return info;
		}

		auto mediaProps = session.TryGetMediaPropertiesAsync().get();
		auto typeRef = mediaProps.PlaybackType(); // IReference<MediaPlaybackType> (null あり)
		auto v = typeRef ? typeRef.Value() : winrt::Windows::Media::MediaPlaybackType::Unknown;
		const int32_t type = static_cast<int32_t>(v);

		const bool eligible =
			(v == winrt::Windows::Media::MediaPlaybackType::Music) ||
			(v == winrt::Windows::Media::MediaPlaybackType::Video) ||
			(v == winrt::Windows::Media::MediaPlaybackType::Unknown);

#ifdef _DEBUG

		if (eligible)
		{
			OutputDebugStringW(std::format(L"Eligible media playback type {}\r\n", type).c_str());
		}

#endif

		info.active = true;
		info.eligible = eligible;
		info.aumid = session.SourceAppUserModelId().c_str();
	}
	catch (const winrt::hresult_error&)
	{
		return info;
	}
	return info;
}


std::optional<uint32_t> MediaAudioAnalyzer::ResolveProcessId(const std::wstring& aumid)
{
	if (aumid.empty()) return std::nullopt;

	com_ptr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put()));
	if (FAILED(hr))
	{
		DebugHr(L"CoCreateInstance(MMDeviceEnumerator)", hr);
		return std::nullopt;
	}

	com_ptr<IMMDeviceCollection> devices;
	hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put());
	if (FAILED(hr) || !devices)
	{
		DebugHr(L"EnumAudioEndpoints(eRender)", hr);
		return std::nullopt;
	}

	UINT count = 0;
	hr = devices->GetCount(&count);
	if (FAILED(hr))
	{
		DebugHr(L"IMMDeviceCollection::GetCount", hr);
		return std::nullopt;
	}

	for (UINT di = 0; di < count; ++di)
	{
		com_ptr<IMMDevice> device;
		if (FAILED(devices->Item(di, device.put())) || !device) continue;

		com_ptr<IAudioSessionManager2> sessionManager;
		hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(sessionManager.put()));
		if (FAILED(hr) || !sessionManager) continue;

		com_ptr<IAudioSessionEnumerator> sessionEnum;
		hr = sessionManager->GetSessionEnumerator(sessionEnum.put());
		if (FAILED(hr) || !sessionEnum) continue;

		int sessionCount = 0;
		hr = sessionEnum->GetCount(&sessionCount);
		if (FAILED(hr)) continue;

		for (int i = 0; i < sessionCount; ++i)
		{
			com_ptr<IAudioSessionControl> control;
			if (FAILED(sessionEnum->GetSession(i, control.put())) || !control) continue;

			com_ptr<IAudioSessionControl2> control2;
			if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2), control2.put_void())) || !control2) continue;

			DWORD pid = 0;
			if (FAILED(control2->GetProcessId(&pid)) || pid == 0) continue;

			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (!process) continue;

			UINT32 length = 0;
			GetApplicationUserModelId(process, &length, nullptr);
			std::wstring processAumid;
			if (length > 0)
			{
				processAumid.resize(length);
				if (SUCCEEDED(GetApplicationUserModelId(process, &length, processAumid.data())))
				{
					if (!processAumid.empty() && processAumid.back() == L'\0')
					{
						processAumid.pop_back();
					}
					if (processAumid == aumid)
					{
						CloseHandle(process);
						return pid;
					}
				}
			}
			CloseHandle(process);
		}
	}

	return std::nullopt;
}

bool MediaAudioAnalyzer::EnsureCaptureForTarget(const CaptureTarget& target)
{
	if (!target.active || !target.eligible)
	{
		return false;
	}

	// 変化が無いならそのまま
	if (m_currentTarget.eligible == target.eligible &&
		m_currentTarget.active == target.active &&
		m_currentTarget.aumid == target.aumid &&
		m_currentTarget.pid == target.pid &&
		m_audioClient && m_captureClient)
	{
		return true;
	}

	// ターゲット切り替え
	m_currentTarget = target;

	if (target.pid.has_value())
	{
#ifdef _DEBUG
		OutputDebugStringW(std::format(L"[Audio] Try process loopback pid={} aumid={}\r\n", target.pid.value(), target.aumid).c_str());
#endif
		if (StartProcessLoopback(target.pid.value()))
		{
			return true;
		}
#ifdef _DEBUG
		OutputDebugStringW(L"[Audio] Process loopback failed; fallback to system loopback\r\n");
#endif
	}

#ifdef _DEBUG
	OutputDebugStringW(L"[Audio] Start system loopback\r\n");
#endif
	return StartSystemLoopback();
}

bool MediaAudioAnalyzer::StartProcessLoopback(uint32_t pid)
{
	StopCapture();

	AUDIOCLIENT_ACTIVATION_PARAMS activationParams{};
	activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
	activationParams.ProcessLoopbackParams.TargetProcessId = pid;
	activationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

	PROPVARIANT var{};
	var.vt = VT_BLOB;
	var.blob.cbSize = sizeof(activationParams);
	var.blob.pBlobData = reinterpret_cast<BYTE*>(&activationParams);

	struct ActivationHandler : public winrt::implements<ActivationHandler, IActivateAudioInterfaceCompletionHandler>
	{
		HANDLE eventHandle{ nullptr };
		HRESULT hr{ E_FAIL };
		winrt::com_ptr<IAudioClient> client;

		HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) noexcept override
		{
			hr = E_FAIL;
			winrt::com_ptr<IAudioClient> audioClient;
			if (operation)
			{
				HRESULT result = E_FAIL;
				operation->GetActivateResult(&result, reinterpret_cast<IUnknown**>(audioClient.put()));
				hr = result;
				if (SUCCEEDED(result))
				{
					client = audioClient;
				}
			}
			if (eventHandle) SetEvent(eventHandle);
			return S_OK;
		}
	};

	auto handler = winrt::make_self<ActivationHandler>();
	handler->eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!handler->eventHandle) return false;

	winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> asyncOp;
	HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &var, handler.get(), &asyncOp);
	if (FAILED(hr))
	{
		DebugHr(L"ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)", hr);
		CloseHandle(handler->eventHandle);
		return false;
	}

	WaitForSingleObject(handler->eventHandle, 5000);
	CloseHandle(handler->eventHandle);

	if (FAILED(handler->hr) || !handler->client)
	{
		DebugHr(L"ActivateCompleted result", handler->hr);
		return false;
	}

	m_audioClient = handler->client;

	// プロセスループバックでは GetMixFormat/IsFormatSupported が未実装(E_NOTIMPL)のことがあるため、
	// こちらでフォーマットを指定して Initialize する。
	WAVEFORMATEX fmt{};
	fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	fmt.nChannels = 2;
	fmt.nSamplesPerSec = 48000;
	fmt.wBitsPerSample = 32;
	fmt.nBlockAlign = static_cast<WORD>((fmt.nChannels * fmt.wBitsPerSample) / 8);
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
	fmt.cbSize = 0;

	m_mixFormat = reinterpret_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
	if (!m_mixFormat) return false;
	*m_mixFormat = fmt;

#ifdef _DEBUG
	OutputDebugStringW(std::format(L"[Audio] ProcessLoopback pid={} fmt=float32 ch=2 sr=48000\r\n", pid).c_str());
#endif

	if (!m_captureEvent)
	{
		m_captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_captureEvent) return false;
	}

	// まず LOOPBACK フラグありで試し、ダメなら外して再試行。
	DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK;
	REFERENCE_TIME hnsBufferDuration = 1'000'000; // 100ms
	hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, hnsBufferDuration, 0, m_mixFormat, nullptr);
	if (FAILED(hr))
	{
		DebugHr(L"IAudioClient::Initialize(ProcessLoopback, with LOOPBACK)", hr);
		streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
		hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, hnsBufferDuration, 0, m_mixFormat, nullptr);
		if (FAILED(hr))
		{
			DebugHr(L"IAudioClient::Initialize(ProcessLoopback, no LOOPBACK)", hr);
			return false;
		}
	}

	hr = m_audioClient->SetEventHandle(m_captureEvent);
	if (FAILED(hr))
	{
		DebugHr(L"IAudioClient::SetEventHandle(ProcessLoopback)", hr);
		return false;
	}

	hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(m_captureClient.put()));
	if (FAILED(hr) || !m_captureClient)
	{
		DebugHr(L"IAudioClient::GetService(IAudioCaptureClient, ProcessLoopback)", hr);
		return false;
	}

	m_analyzer.Reset(static_cast<double>(m_mixFormat->nSamplesPerSec), static_cast<int>(m_mixFormat->nChannels));
	m_captureStart = std::chrono::steady_clock::now();
	m_lastNonSilentAudio = m_captureStart;

	hr = m_audioClient->Start();
	if (FAILED(hr))
	{
		DebugHr(L"IAudioClient::Start(ProcessLoopback)", hr);
		return false;
	}

	return true;
}

bool MediaAudioAnalyzer::StartSystemLoopback()
{
	StopCapture();

	com_ptr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put()));
	if (FAILED(hr))
	{
		DebugHr(L"CoCreateInstance(MMDeviceEnumerator)", hr);
		return false;
	}

	com_ptr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
	if (FAILED(hr))
	{
		DebugHr(L"GetDefaultAudioEndpoint(eMultimedia)", hr);
		hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr))
		{
			DebugHr(L"GetDefaultAudioEndpoint(eConsole)", hr);
			return false;
		}
	}

	m_captureDevice = device;

	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(m_audioClient.put()));
	if (FAILED(hr) || !m_audioClient)
	{
		DebugHr(L"IMMDevice::Activate(IAudioClient)", hr);
		return false;
	}

	hr = m_audioClient->GetMixFormat(&m_mixFormat);
	if (FAILED(hr) || !m_mixFormat)
	{
		DebugHr(L"IAudioClient::GetMixFormat", hr);
		return false;
	}

#ifdef _DEBUG
	{
		const bool isFloat = IsFloatFormat(m_mixFormat);
		const bool isPcm = IsPcmFormat(m_mixFormat);
		OutputDebugStringW(std::format(L"[Audio] SystemLoopback MixFormat tag={} bits={} ch={} sr={} float={} pcm={}\r\n",
									   m_mixFormat->wFormatTag,
									   m_mixFormat->wBitsPerSample,
									   m_mixFormat->nChannels,
									   m_mixFormat->nSamplesPerSec,
									   isFloat ? 1 : 0,
									   isPcm ? 1 : 0).c_str());
		if (m_mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			WORD validBits = 0;
			DWORD mask = 0;
			GUID sub{};
			wchar_t guidStr[64] = {};
			if (TryGetExtensibleFields(m_mixFormat, validBits, mask, sub))
			{
				StringFromGUID2(sub, guidStr, 64);
				OutputDebugStringW(std::format(L"[Audio] SystemLoopback Extensible validBits={} channelMask=0x{:08X} subFormat={}\r\n", validBits, mask, guidStr).c_str());
			}
			else
			{
				OutputDebugStringW(L"[Audio] SystemLoopback Extensible parse failed\r\n");
			}
		}
	}
#endif

	// endpoint loopback では event-driven が期待通りに動かないことがあるため、ポーリングにする。
	DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
	REFERENCE_TIME hnsBufferDuration = 1'000'000; // 100ms
	hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, hnsBufferDuration, 0, m_mixFormat, nullptr);
	if (FAILED(hr))
	{
		DebugHr(L"IAudioClient::Initialize(SystemLoopback)", hr);
		return false;
	}

	hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(m_captureClient.put()));
	if (FAILED(hr) || !m_captureClient)
	{
		DebugHr(L"IAudioClient::GetService(IAudioCaptureClient)", hr);
		return false;
	}

	m_analyzer.Reset(static_cast<double>(m_mixFormat->nSamplesPerSec), static_cast<int>(m_mixFormat->nChannels));
	m_captureStart = std::chrono::steady_clock::now();
	m_lastNonSilentAudio = m_captureStart;

	hr = m_audioClient->Start();
	if (FAILED(hr))
	{
		DebugHr(L"IAudioClient::Start(SystemLoopback)", hr);
		return false;
	}

	return true;
}

void MediaAudioAnalyzer::StopCapture()
{
	if (m_audioClient)
	{
		m_audioClient->Stop();
	}
	m_captureClient = nullptr;
	m_audioClient = nullptr;
	m_captureDevice = nullptr;
	if (m_mixFormat)
	{
		CoTaskMemFree(m_mixFormat);
		m_mixFormat = nullptr;
	}
	if (m_captureEvent)
	{
		CloseHandle(m_captureEvent);
		m_captureEvent = nullptr;
	}
	m_currentTarget = CaptureTarget{};
}

bool MediaAudioAnalyzer::CaptureAudioOnce(std::stop_token stopToken)
{
	if (!m_audioClient || !m_captureClient || !m_mixFormat)
	{
		return false;
	}

	// event-driven の場合だけ待つ。TIMEOUT でも必ずポーリングする。
	if (m_captureEvent)
	{
		DWORD wait = WaitForSingleObject(m_captureEvent, kCaptureWaitMs);
		if (wait == WAIT_FAILED)
		{
			return false;
		}
		if (stopToken.stop_requested())
		{
			return false;
		}
	}
	else
	{
		Sleep(5);
	}

	const bool isFloat = IsFloatFormat(m_mixFormat);
	const bool isPcm = IsPcmFormat(m_mixFormat);

	const int channels = static_cast<int>(m_mixFormat->nChannels);
	const double sampleRate = static_cast<double>(m_mixFormat->nSamplesPerSec);

	UINT32 packetLength = 0;
	HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
	if (FAILED(hr))
	{
		DebugHr(L"IAudioCaptureClient::GetNextPacketSize", hr);
		return false;
	}

	bool anyPacket = false;
	bool anyNonSilent = false;
	float maxAbs = 0.0f;
	bool needRestart = false;
	std::vector<float> floatBuffer;

	while (packetLength > 0 && !stopToken.stop_requested())
	{
		BYTE* data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;

		hr = m_captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
		if (FAILED(hr))
		{
			DebugHr(L"IAudioCaptureClient::GetBuffer", hr);
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING)
			{
				StopCapture();
				needRestart = true;
			}
			break;
		}

		anyPacket = true;

		const size_t sampleCount = static_cast<size_t>(frames) * static_cast<size_t>(channels);
		floatBuffer.resize(sampleCount);
		auto floatSpan = std::span<float>(floatBuffer.data(), floatBuffer.size());

		if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
		{
			AudioFormatConverter::FillSilence(floatSpan);
		}
		else if (isFloat)
		{
			const float* in = reinterpret_cast<const float*>(data);
			AudioFormatConverter::CopyFloat(in, sampleCount, floatSpan);
		}
		else if (isPcm)
		{
			const UINT16 bits = m_mixFormat->wBitsPerSample;

			if (bits == 8)
			{
				const uint8_t* in = reinterpret_cast<const uint8_t*>(data);
				AudioFormatConverter::ConvertUnsigned8(in, sampleCount, floatSpan);
			}
			else if (bits == 16)
			{
				const int16_t* in = reinterpret_cast<const int16_t*>(data);
				AudioFormatConverter::ConvertSigned(in, sampleCount, 1.0f / 32768.0f, floatSpan);
			}
			else if (bits == 24)
			{
				const uint8_t* in = reinterpret_cast<const uint8_t*>(data);
				AudioFormatConverter::ConvertPcm24(in, sampleCount, floatSpan);
			}
			else if (bits == 32)
			{
				const int32_t* in = reinterpret_cast<const int32_t*>(data);

				const int validBits = ResolveValidBits(m_mixFormat);
				const bool looksLeftAligned = DetectLeftAligned(in, sampleCount, validBits);
				AudioFormatConverter::ConvertPcm32(in, sampleCount, validBits, looksLeftAligned, floatSpan);
			}
			else
			{
				static std::atomic_bool s_logged{ false };
				if (!s_logged.exchange(true))
				{
					OutputDebugStringW(std::format(L"[Audio] Unsupported PCM bits={} -> treated as silence\r\n", bits).c_str());
				}
				AudioFormatConverter::FillSilence(floatSpan);
			}
		}
		else
		{
			static std::atomic_bool s_logged{ false };
			if (!s_logged.exchange(true))
			{
				OutputDebugStringW(std::format(L"[Audio] Unsupported format tag={} bits={} (not float/pcm) -> treated as silence\r\n", m_mixFormat->wFormatTag, m_mixFormat->wBitsPerSample).c_str());
			}
			AudioFormatConverter::FillSilence(floatSpan);
		}

		// 取り込めているかの統計（1秒に1回だけログ用のピーク）
		{
			const size_t checkN = std::min<size_t>(floatSpan.size(), 4096);
			for (size_t i = 0; i < checkN; ++i)
			{
				float a = std::abs(floatSpan[i]);
				if (a > maxAbs) maxAbs = a;
			}
		}

		auto now = std::chrono::steady_clock::now();
		double timeSeconds = std::chrono::duration<double>(now - m_captureStart).count();

		m_analyzer.Process(floatBuffer.data(), frames, sampleRate, channels, timeSeconds);
		if (m_analyzer.LastHadAudio())
		{
			anyNonSilent = true;
		}
		{
			std::scoped_lock lock(m_stateMutex);
			m_state = m_analyzer.State();
			m_state.active = true;
		}

		if (stopToken.stop_requested())
		{
			m_captureClient->ReleaseBuffer(frames);
			break;
		}

		m_captureClient->ReleaseBuffer(frames);

		hr = m_captureClient->GetNextPacketSize(&packetLength);
		if (FAILED(hr))
		{
			DebugHr(L"IAudioCaptureClient::GetNextPacketSize(loop)", hr);
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING)
			{
				StopCapture();
				needRestart = true;
			}
			break;
		}
	}

#ifdef _DEBUG
	{
		static auto s_lastLog = std::chrono::steady_clock::now();
		const auto now = std::chrono::steady_clock::now();
		if (now - s_lastLog >= std::chrono::seconds(1))
		{
			s_lastLog = now;
			OutputDebugStringW(std::format(L"[Audio] Capture anyPacket={} anyNonSilent={} maxAbs={:.6f}\r\n", anyPacket ? 1 : 0, anyNonSilent ? 1 : 0, maxAbs).c_str());
		}
	}
#endif

	const auto now = std::chrono::steady_clock::now();
	if (anyNonSilent)
	{
		m_lastNonSilentAudio = now;
	}
	else if (anyPacket && m_currentTarget.active && m_currentTarget.eligible)
	{
		const auto silentDuration = now - m_lastNonSilentAudio;
		if (silentDuration > std::chrono::seconds(3) && !m_drmWarningSent.load(std::memory_order_acquire))
		{
			m_drmWarningPending.store(true, std::memory_order_release);
			m_drmWarningSent.store(true, std::memory_order_release);
		}
	}

	if (needRestart)
	{
		return false;
	}
	// データが来ていないだけなら「成功」とする
	return !stopToken.stop_requested();
}

void MediaAudioAnalyzer::AudioAnalyzer::Reset(double sampleRate, int channels)
{
	m_energyAvg = 0.0;
	m_lastBeatTime = 0.0;
	m_bpm = 0.0;
	m_mouth = 0.0;
	m_beatStrength = 0.0;
	m_bassEnergy = 0.0;
	m_rmsAvg = 0.0;
	m_noiseRms = 1e-9;
	m_agcGain = 1.0;
	m_lastHadAudio = false;
	m_fftBuffer.assign(kFftSize, 0.0f);
	m_window.resize(kFftSize);
	for (int i = 0; i < kFftSize; ++i)
	{
		m_window[i] = 0.5f * (1.0f - std::cos(2.0 * 3.141592653589793 * i / (kFftSize - 1)));
	}
	m_fftWriteIndex = 0;
}

void MediaAudioAnalyzer::AudioAnalyzer::Process(const float* samples, size_t frames, double sampleRate, int channels, double timeSeconds)
{
	if (!samples || frames == 0 || channels == 0) return;

	double energy = 0.0;
	for (size_t i = 0; i < frames; ++i)
	{
		double mono = 0.0;
		for (int ch = 0; ch < channels; ++ch)
		{
			mono += samples[i * channels + ch];
		}
		mono /= static_cast<double>(channels);
		energy += mono * mono;

		m_fftBuffer[m_fftWriteIndex] = static_cast<float>(mono);
		m_fftWriteIndex = (m_fftWriteIndex + 1) % kFftSize;
	}

	energy /= static_cast<double>(frames);
	const double rms = std::sqrt(std::max(0.0, energy));

	// 無音判定（固定閾値ではなく、環境ノイズの移動平均に基づいて判定する）
	const double gate = std::max(m_noiseRms * 4.0, kMinSilenceGateRms);
	const bool hasAudio = (rms > gate);
	m_lastHadAudio = hasAudio;

	// ノイズ推定（音が無いときのみ追従させる）
	if (!hasAudio)
	{
		m_noiseRms = (m_noiseRms * 0.995) + (rms * 0.005);
		m_noiseRms = std::clamp(m_noiseRms, 0.0, 1.0);
	}

	// エネルギー平均（BPM/ビート検知用）。音量が小さくても追従できるように、
	// 値そのものが小さいことを理由に打ち切らない。
	m_energyAvg = (m_energyAvg * 0.98) + (energy * 0.02);

	// 口パクの自動ゲイン制御（AGC）。
	// 小音量でも同程度の反応になるように、平均 RMS に対してゲインを調整する。
	if (hasAudio)
	{
		m_rmsAvg = (m_rmsAvg * 0.995) + (rms * 0.005);
		const double denom = std::max(m_rmsAvg, 1e-12);
		double targetGain = kAgcTargetRms / denom;
		targetGain = std::clamp(targetGain, 1.0, 200.0);
		m_agcGain = (m_agcGain * 0.90) + (targetGain * 0.10);
		m_agcGain = std::clamp(m_agcGain, 1.0, 200.0);
	}

	UpdateBeat(energy, timeSeconds);
	UpdateSpectral();

	// AGC 後の RMS で口パクを駆動（音量に依存しにくい）
	double mouthTarget = hasAudio ? (rms * m_agcGain * 4.0) : 0.0;
	m_mouth = (m_mouth * 0.85) + (mouthTarget * 0.15);
	m_mouth = std::clamp(m_mouth, 0.0, 1.0);
}

void MediaAudioAnalyzer::AudioAnalyzer::UpdateBeat(double energy, double timeSeconds)
{
	if (m_energyAvg <= 1e-12) return;

	double ratio = energy / m_energyAvg;
	double spectralBoost = std::clamp(m_bassEnergy * 0.15, 0.0, 0.5);
	double strength = std::clamp((ratio - 1.0 + spectralBoost) * 1.25, 0.0, 1.0);
	m_beatStrength = (m_beatStrength * 0.7) + (strength * 0.3);

	if (ratio > kBeatEnergyThreshold && (timeSeconds - m_lastBeatTime) > kBeatMinIntervalSeconds)
	{
		if (m_lastBeatTime > 0.0)
		{
			double interval = timeSeconds - m_lastBeatTime;
			double bpm = 60.0 / interval;
			if (bpm > 60.0 && bpm < 200.0)
			{
				m_bpm = (m_bpm * 0.7) + (bpm * 0.3);
			}
		}
		m_lastBeatTime = timeSeconds;
	}
}

void MediaAudioAnalyzer::AudioAnalyzer::UpdateSpectral()
{
	std::vector<std::complex<double>> fft(kFftSize);
	for (int i = 0; i < kFftSize; ++i)
	{
		int idx = static_cast<int>((m_fftWriteIndex + i) % kFftSize);
		double windowed = static_cast<double>(m_fftBuffer[idx]) * m_window[i];
		fft[i] = std::complex<double>(windowed, 0.0);
	}

	for (int i = 1, j = 0; i < kFftSize; ++i)
	{
		int bit = kFftSize >> 1;
		for (; j & bit; bit >>= 1)
		{
			j ^= bit;
		}
		j ^= bit;
		if (i < j)
		{
			std::swap(fft[i], fft[j]);
		}
	}

	for (int len = 2; len <= kFftSize; len <<= 1)
	{
		double angle = -2.0 * 3.141592653589793 / static_cast<double>(len);
		std::complex<double> wlen(std::cos(angle), std::sin(angle));
		for (int i = 0; i < kFftSize; i += len)
		{
			std::complex<double> w(1.0, 0.0);
			for (int j = 0; j < len / 2; ++j)
			{
				auto u = fft[i + j];
				auto v = fft[i + j + len / 2] * w;
				fft[i + j] = u + v;
				fft[i + j + len / 2] = u - v;
				w *= wlen;
			}
		}
	}

	// 音量に依存しないよう、低域成分を「全体に対する比率」で追跡する。
	double bassSum = 0.0;
	double totalSum = 0.0;
	for (int i = 1; i < 128; ++i)
	{
		double mag = std::abs(fft[i]);
		totalSum += mag;
		if (i < 6)
		{
			bassSum += mag;
		}
	}
	if (totalSum > 0.0)
	{
		double bassRatio = bassSum / totalSum;
		m_bassEnergy = (m_bassEnergy * 0.8) + (bassRatio * 0.2);
	}
}

AudioReactiveState MediaAudioAnalyzer::AudioAnalyzer::State() const
{
	AudioReactiveState state{};
	state.active = true;
	state.mouthOpen = Clamp01(static_cast<float>(m_mouth));
	state.beatStrength = Clamp01(static_cast<float>(m_beatStrength));
	state.bpm = static_cast<float>(m_bpm);
	return state;
}
