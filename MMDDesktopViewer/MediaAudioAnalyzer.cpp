#include "MediaAudioAnalyzer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <appmodel.h>
#include <winrt/base.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Foundation.h>
#include <roapi.h>
#include <cmath>
#include <complex>
#include <algorithm>

#pragma comment(lib, "Mmdevapi.lib")
#pragma comment(lib, "mincore.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr int kFftSize = 1024;
	constexpr double kBeatMinIntervalSeconds = 0.25;
	constexpr double kBeatEnergyThreshold = 1.35;
	constexpr DWORD kCaptureWaitMs = 150;
	constexpr auto kSessionPollInterval = std::chrono::milliseconds(500);

	bool IsFloatFormat(const WAVEFORMATEX* format)
	{
		if (!format) return false;
		if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
			return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		}
		return false;
	}

	bool IsPcmFormat(const WAVEFORMATEX* format)
	{
		if (!format) return false;
		if (format->wFormatTag == WAVE_FORMAT_PCM) return true;
		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
			return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
		}
		return false;
	}

	float Clamp01(float v)
	{
		return std::clamp(v, 0.0f, 1.0f);
	}
}

MediaAudioAnalyzer::MediaAudioAnalyzer()
{
	m_worker = std::jthread([this](std::stop_token token) { WorkerLoop(token); });
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

void MediaAudioAnalyzer::WorkerLoop(std::stop_token stopToken)
{
	winrt::init_apartment(winrt::apartment_type::single_threaded);
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
		auto mediaType = mediaProps.PlaybackType();
		const bool eligible = (static_cast<winrt::Windows::Media::MediaPlaybackType>(mediaType.GetInt32()) == winrt::Windows::Media::MediaPlaybackType::Music ||
							   static_cast<winrt::Windows::Media::MediaPlaybackType>(mediaType.GetInt32()) == winrt::Windows::Media::MediaPlaybackType::Video);

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

	ComPtr<IMMDeviceEnumerator> enumerator;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
								IID_PPV_ARGS(&enumerator))))
	{
		return std::nullopt;
	}

	ComPtr<IMMDevice> device;
	if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
	{
		return std::nullopt;
	}

	ComPtr<IAudioSessionManager2> sessionManager;
	if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
								reinterpret_cast<void**>(sessionManager.GetAddressOf()))))
	{
		return std::nullopt;
	}

	ComPtr<IAudioSessionEnumerator> sessionEnum;
	if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnum)))
	{
		return std::nullopt;
	}

	int count = 0;
	sessionEnum->GetCount(&count);
	for (int i = 0; i < count; ++i)
	{
		ComPtr<IAudioSessionControl> control;
		if (FAILED(sessionEnum->GetSession(i, &control))) continue;

		ComPtr<IAudioSessionControl2> control2;
		if (FAILED(control.As(&control2))) continue;

		DWORD pid = 0;
		if (FAILED(control2->GetProcessId(&pid))) continue;
		if (pid == 0) continue;

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

	return std::nullopt;
}

bool MediaAudioAnalyzer::EnsureCaptureForTarget(const CaptureTarget& target)
{
	if (m_audioClient && target.aumid == m_currentTarget.aumid && target.pid == m_currentTarget.pid)
	{
		return true;
	}

	StopCapture();
	m_currentTarget = target;

	if (target.pid.has_value())
	{
		if (StartProcessLoopback(target.pid.value()))
		{
			return true;
		}
	}

	return StartSystemLoopback();
}

bool MediaAudioAnalyzer::StartProcessLoopback(uint32_t pid)
{
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
		ComPtr<IAudioClient> client;

		HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) noexcept override
		{
			hr = E_FAIL;
			ComPtr<IAudioClient> audioClient;
			if (operation)
			{
				HRESULT result = E_FAIL;
				operation->GetActivateResult(&result, reinterpret_cast<IUnknown**>(audioClient.GetAddressOf()));
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

	ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
	HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &var, handler.get(), &asyncOp);
	if (FAILED(hr))
	{
		CloseHandle(handler->eventHandle);
		return false;
	}

	WaitForSingleObject(handler->eventHandle, 5000);
	CloseHandle(handler->eventHandle);

	if (FAILED(handler->hr) || !handler->client)
	{
		return false;
	}

	m_audioClient = handler->client;
	return StartSystemLoopback();
}

bool MediaAudioAnalyzer::StartSystemLoopback()
{
	if (!m_audioClient)
	{
		ComPtr<IMMDeviceEnumerator> enumerator;
		if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
									IID_PPV_ARGS(&enumerator))))
		{
			return false;
		}

		ComPtr<IMMDevice> device;
		if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
		{
			return false;
		}
		m_captureDevice = device;

		if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
									reinterpret_cast<void**>(m_audioClient.ReleaseAndGetAddressOf()))))
		{
			return false;
		}
	}

	if (m_mixFormat)
	{
		CoTaskMemFree(m_mixFormat);
		m_mixFormat = nullptr;
	}

	if (FAILED(m_audioClient->GetMixFormat(&m_mixFormat)) || !m_mixFormat)
	{
		return false;
	}

	DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
	HRESULT hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
										   streamFlags,
										   0,
										   0,
										   m_mixFormat,
										   nullptr);
	if (FAILED(hr))
	{
		return false;
	}

	if (FAILED(m_audioClient->GetService(IID_PPV_ARGS(&m_captureClient))))
	{
		return false;
	}

	if (!m_captureEvent)
	{
		m_captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_captureEvent) return false;
	}

	if (FAILED(m_audioClient->SetEventHandle(m_captureEvent)))
	{
		return false;
	}

	m_analyzer.Reset(m_mixFormat->nSamplesPerSec, m_mixFormat->nChannels);
	m_captureStart = std::chrono::steady_clock::now();

	return SUCCEEDED(m_audioClient->Start());
}

void MediaAudioAnalyzer::StopCapture()
{
	if (m_audioClient)
	{
		m_audioClient->Stop();
	}
	m_captureClient.Reset();
	m_audioClient.Reset();
	m_captureDevice.Reset();
	if (m_mixFormat)
	{
		CoTaskMemFree(m_mixFormat);
		m_mixFormat = nullptr;
	}
}

bool MediaAudioAnalyzer::CaptureAudioOnce(std::stop_token stopToken)
{
	if (!m_audioClient || !m_captureClient || !m_mixFormat || !m_captureEvent)
	{
		return false;
	}

	DWORD wait = WaitForSingleObject(m_captureEvent, kCaptureWaitMs);
	if (wait != WAIT_OBJECT_0)
	{
		return false;
	}

	UINT32 packetLength = 0;
	HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
	if (FAILED(hr))
	{
		return false;
	}

	const bool isFloat = IsFloatFormat(m_mixFormat);
	const bool isPcm = IsPcmFormat(m_mixFormat);
	const int channels = static_cast<int>(m_mixFormat->nChannels);
	const double sampleRate = static_cast<double>(m_mixFormat->nSamplesPerSec);

	while (packetLength > 0 && !stopToken.stop_requested())
	{
		BYTE* data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;
		hr = m_captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
		if (FAILED(hr) || frames == 0)
		{
			break;
		}

		std::vector<float> floatBuffer;
		floatBuffer.resize(static_cast<size_t>(frames) * channels);

		if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
		{
			std::fill(floatBuffer.begin(), floatBuffer.end(), 0.0f);
		}
		else if (isFloat)
		{
			const float* in = reinterpret_cast<const float*>(data);
			std::copy(in, in + floatBuffer.size(), floatBuffer.begin());
		}
		else if (isPcm && m_mixFormat->wBitsPerSample == 16)
		{
			const int16_t* in = reinterpret_cast<const int16_t*>(data);
			for (size_t i = 0; i < floatBuffer.size(); ++i)
			{
				floatBuffer[i] = static_cast<float>(in[i]) / 32768.0f;
			}
		}
		else
		{
			std::fill(floatBuffer.begin(), floatBuffer.end(), 0.0f);
		}

		auto now = std::chrono::steady_clock::now();
		double timeSeconds = std::chrono::duration<double>(now - m_captureStart).count();

		m_analyzer.Process(floatBuffer.data(), frames, sampleRate, channels, timeSeconds);
		{
			std::scoped_lock lock(m_stateMutex);
			m_state = m_analyzer.State();
			m_state.active = true;
		}

		m_captureClient->ReleaseBuffer(frames);
		hr = m_captureClient->GetNextPacketSize(&packetLength);
		if (FAILED(hr)) break;
	}

	return true;
}

void MediaAudioAnalyzer::AudioAnalyzer::Reset(double sampleRate, int channels)
{
	m_energyAvg = 0.0;
	m_lastBeatTime = 0.0;
	m_bpm = 0.0;
	m_mouth = 0.0;
	m_beatStrength = 0.0;
	m_bassEnergy = 0.0;
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
	m_energyAvg = (m_energyAvg * 0.98) + (energy * 0.02);

	UpdateBeat(energy, timeSeconds);
	UpdateSpectral();

	double mouthTarget = std::sqrt(energy) * 3.5;
	m_mouth = (m_mouth * 0.85) + (mouthTarget * 0.15);
	m_mouth = std::clamp(m_mouth, 0.0, 1.0);
}

void MediaAudioAnalyzer::AudioAnalyzer::UpdateBeat(double energy, double timeSeconds)
{
	if (m_energyAvg <= 1e-6) return;

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

	double bassSum = 0.0;
	int bassBins = 0;
	for (int i = 1; i < 6; ++i)
	{
		double mag = std::abs(fft[i]);
		bassSum += mag;
		bassBins++;
	}
	if (bassBins > 0)
	{
		double bass = bassSum / static_cast<double>(bassBins);
		m_bassEnergy = (m_bassEnergy * 0.8) + (bass * 0.2);
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