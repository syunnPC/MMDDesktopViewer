#pragma once
#include <windows.h>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>
#include <atomic>
#include "ProgressWindow.hpp"
#include "TrayIcon.hpp"
#include "DcompRenderer.hpp"
#include "MmdAnimator.hpp"
#include "Settings.hpp"
#include "inputManager.hpp"
#include "WindowManager.hpp"
#include "AudioReactiveState.hpp"

class SettingsWindow;
class MediaAudioAnalyzer;

class App
{
public:
	explicit App(HINSTANCE hInst);
	~App();

	App(const App&) = delete;
	App& operator=(const App&) = delete;

	int Run();

	const AppSettings& Settings() const
	{
		return m_settingsData;
	}
	void ApplySettings(const AppSettings& settings, bool persist);

	// ライト設定を取得してUIで表示するため
	LightSettings& LightSettingsRef()
	{
		return m_settingsData.light;
	}
	PhysicsSettings& PhysicsSettingsRef()
	{
		return m_settingsData.physics;
	}
	void ApplyLightSettings();
	void ApplyPhysicsSettings();
	void SaveSettings();

	const std::filesystem::path& ModelsDir() const
	{
		return m_modelsDir;
	}
	const std::filesystem::path& BaseDir() const
	{
		return m_baseDir;
	}

	void ToggleGizmoWindow();
	void TogglePhysics();
	void ToggleWindowManipulation();
	void MoveRenderWindowBy(int dx, int dy);
	void AddCameraRotation(float dx, float dy);
	void AdjustScale(float delta);
	void AdjustBrightness(float delta);
	void RenderGizmo();

private:
	void InitRenderer();
	void InitTray();
	void InitAnimator();
	void BuildTrayMenu();
	void RefreshMotionList();
	void UpdateTimerInterval();
	UINT ComputeTimerIntervalMs() const;
	void LoadModelFromSettings();
	void ShowNotification(const std::wstring& title, const std::wstring& message) const;

	void OnTrayCommand(UINT id);
	void OnTimer();

	HINSTANCE m_hInst{};

	std::unique_ptr<TrayIcon> m_tray;
	std::unique_ptr<SettingsWindow> m_settings;

	std::unique_ptr<DcompRenderer> m_renderer;
	std::unique_ptr<MmdAnimator> m_animator;
	std::unique_ptr<MediaAudioAnalyzer> m_mediaAudio;

	InputManager m_input;
	WindowManager m_windowManager;

	AppSettings m_settingsData;

	std::filesystem::path m_baseDir;
	std::filesystem::path m_modelsDir;
	std::filesystem::path m_motionsDir;

	std::vector<std::filesystem::path> m_motionFiles;

	HMENU m_trayMenu{};

	bool m_comInitialized{ false };

	UINT m_timerIntervalMs{ 16 };

	// ローディング関連
	std::unique_ptr<ProgressWindow> m_progress;
	std::atomic<bool> m_isLoading{ false };

	void StartLoadingModel(const std::filesystem::path& path);
	void OnLoadComplete(WPARAM wParam, LPARAM lParam);

	bool m_lookAtEnabled{ false };
};