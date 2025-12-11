#pragma once
#include <windows.h>
#include <memory>
#include <filesystem>
#include <vector>
#include "ProgressWindow.hpp"
#include "TrayIcon.hpp"
#include "DcompRenderer.hpp"
#include "MmdAnimator.hpp"
#include "Settings.hpp"

class SettingsWindow;

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
	void ApplyLightSettings();
	void SaveSettings() const;

	const std::filesystem::path& ModelsDir() const
	{
		return m_modelsDir;
	}
	const std::filesystem::path& BaseDir() const
	{
		return m_baseDir;
	}

private:
	static LRESULT CALLBACK WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void CreateHiddenMessageWindow();
	void CreateRenderWindow();
	void InitRenderer();
	void InitTray();
	void InitAnimator();
	void BuildTrayMenu();
	void RefreshMotionList();
	void ApplyTopmost() const;
	void UpdateTimerInterval();
	UINT ComputeTimerIntervalMs() const;
	void LoadModelFromSettings();

	void OnTrayCommand(UINT id);
	void OnTimer();
	void OnMouseWheel(HWND hWnd, int delta, WPARAM wParam);

	HINSTANCE m_hInst{};
	HWND m_msgWnd{};
	HWND m_renderWnd{};

	std::unique_ptr<TrayIcon> m_tray;
	std::unique_ptr<SettingsWindow> m_settings;

	std::unique_ptr<DcompRenderer> m_renderer;
	std::unique_ptr<MmdAnimator> m_animator;

	AppSettings m_settingsData;

	std::filesystem::path m_baseDir;
	std::filesystem::path m_modelsDir;
	std::filesystem::path m_motionsDir;

	std::vector<std::filesystem::path> m_motionFiles;

	HMENU m_trayMenu{};

	bool m_comInitialized{ false };

	bool m_draggingWindow{ false };
	POINT m_dragStartCursor{};
	POINT m_dragStartWindowPos{};

	bool m_rotatingModel{ false };
	POINT m_rotateLastCursor{};

	UINT m_timerIntervalMs{ 16 };

	// ローディング関連
	std::unique_ptr<ProgressWindow> m_progress;
	std::atomic<bool> m_isLoading{ false };

	// スレッドからの完了通知用メッセージ
	static constexpr UINT WM_APP_LOAD_COMPLETE = WM_APP + 200;

	void StartLoadingModel(const std::filesystem::path& path);
	void OnLoadComplete(WPARAM wParam, LPARAM lParam);
};