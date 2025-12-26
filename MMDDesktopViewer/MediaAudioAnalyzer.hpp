#pragma once

#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <deque>
#include <Windows.h>
#include <mmeapi.h>
#include <winrt/base.h>
#include "AudioReactiveState.hpp"

struct IAudioClient;
struct IAudioCaptureClient;
struct IMMDevice;
struct IMMDeviceEnumerator;

class MediaAudioAnalyzer
{
public:
	MediaAudioAnalyzer();
	~MediaAudioAnalyzer();

	MediaAudioAnalyzer(const MediaAudioAnalyzer&) = delete;
	MediaAudioAnalyzer& operator=(const MediaAudioAnalyzer&) = delete;

	void SetEnabled(bool enabled);
	bool Enabled() const;

	AudioReactiveState GetState() const;
	bool ConsumeDrmWarning();

private:
	struct MediaSessionInfo
	{
		bool active{ false };
		bool eligible{ false };
		std::wstring aumid;
	};

	struct CaptureTarget
	{
		bool active{ false };
		bool eligible{ false };
		std::wstring aumid;
		std::optional<uint32_t> pid;
	};

	struct GsmtcCache;

	class AudioAnalyzer
	{
	public:
		void Reset(double sampleRate, int channels);
		void Process(const float* samples, size_t frames, double sampleRate, int channels, double timeSeconds);
		AudioReactiveState State() const;
		bool LastHadAudio() const
		{
			return m_lastHadAudio;
		}

	private:
		void UpdateBeat(double energy, double normalizedFlux, double timeSeconds, double frameDuration);
		double UpdateSpectral();

		double m_energyAvg{ 0.0 };
		double m_lastBeatTime{ 0.0 };
		double m_bpm{ 0.0 };
		double m_mouth{ 0.0 };
		double m_beatStrength{ 0.0 };
		double m_bassEnergy{ 0.0 };
		double m_rmsAvg{ 0.0 };
		double m_noiseRms{ 1e-9 };
		double m_agcGain{ 1.0 };
		bool m_lastHadAudio{ false };

		double m_envelopeFast{ 0.0 };
		double m_envelopeSlow{ 0.0 };
		double m_fluxAdaptive{ 0.0 };
		double m_fluxPeak{ 0.0 };
		std::deque<double> m_recentBeatIntervals;

		std::vector<float> m_fftBuffer;
		std::vector<float> m_window;
		std::vector<double> m_prevMagnitudes;
		size_t m_fftWriteIndex{ 0 };
	};

	void WorkerLoop(std::stop_token stopToken);
	MediaSessionInfo QuerySession();
	std::optional<uint32_t> ResolveProcessId(const std::wstring& aumid);
	bool EnsureDeviceEnumerator();
	bool EnsureCaptureForTarget(const CaptureTarget& target);
	bool StartProcessLoopback(uint32_t pid);
	bool StartSystemLoopback();
	void StopCapture();
	bool CaptureAudioOnce(std::stop_token stopToken);

	std::atomic<bool> m_enabled{ true };
	std::atomic<bool> m_drmWarningPending{ false };
	std::atomic<bool> m_drmWarningSent{ false };

	mutable std::mutex m_stateMutex;
	AudioReactiveState m_state{};
	AudioAnalyzer m_analyzer{};

	std::jthread m_worker;

	winrt::com_ptr<IMMDeviceEnumerator> m_deviceEnumerator;
	std::unique_ptr<GsmtcCache> m_gsmtc;

	winrt::com_ptr<IAudioClient> m_audioClient;
	winrt::com_ptr<IAudioCaptureClient> m_captureClient;
	winrt::com_ptr<IMMDevice> m_captureDevice;
	WAVEFORMATEX* m_mixFormat{ nullptr };
	HANDLE m_captureEvent{ nullptr };
	CaptureTarget m_currentTarget{};
	std::chrono::steady_clock::time_point m_captureStart{};
	std::chrono::steady_clock::time_point m_lastNonSilentAudio{};
};
