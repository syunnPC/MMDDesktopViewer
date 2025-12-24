#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <wrl/client.h>
#include <mmeapi.h>
#include "AudioReactiveState.hpp"

struct IAudioClient;
struct IAudioCaptureClient;
struct IMMDevice;

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

	class AudioAnalyzer
	{
	public:
		void Reset(double sampleRate, int channels);
		void Process(const float* samples, size_t frames, double sampleRate, int channels, double timeSeconds);
		AudioReactiveState State() const;

	private:
		void UpdateBeat(double energy, double timeSeconds);
		void UpdateSpectral();

		double m_energyAvg{ 0.0 };
		double m_lastBeatTime{ 0.0 };
		double m_bpm{ 0.0 };
		double m_mouth{ 0.0 };
		double m_beatStrength{ 0.0 };
		double m_bassEnergy{ 0.0 };

		std::vector<float> m_fftBuffer;
		std::vector<float> m_window;
		size_t m_fftWriteIndex{ 0 };
	};

	void WorkerLoop(std::stop_token stopToken);
	MediaSessionInfo QuerySession();
	std::optional<uint32_t> ResolveProcessId(const std::wstring& aumid);
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

	Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
	Microsoft::WRL::ComPtr<IAudioCaptureClient> m_captureClient;
	Microsoft::WRL::ComPtr<IMMDevice> m_captureDevice;
	WAVEFORMATEX* m_mixFormat{ nullptr };
	HANDLE m_captureEvent{ nullptr };
	CaptureTarget m_currentTarget{};
	std::chrono::steady_clock::time_point m_captureStart{};
	std::chrono::steady_clock::time_point m_lastNonSilentAudio{};
};