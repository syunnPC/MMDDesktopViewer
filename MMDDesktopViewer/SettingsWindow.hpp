#pragma once
#include <windows.h>
#include <string>

#include "Settings.hpp"

class App;

class SettingsWindow
{
public:
	SettingsWindow(App& app, HINSTANCE hInst);
	~SettingsWindow();

	SettingsWindow(const SettingsWindow&) = delete;
	SettingsWindow& operator=(const SettingsWindow&) = delete;

	void Show();
	void Hide();
	void Refresh()
	{
		LoadCurrentSettings();
	}

private:
	static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
	LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void CreateControls();
	void LoadCurrentSettings();
	void ApplyAndSave();
	void UpdateLightPreview();
	void PickColor(float& r, float& g, float& b, HWND btnHwnd);
	void UpdateFpsControlState();

	void SetModernFont(HWND hChild);
	void SetDarkTheme(HWND hChild);

	void UpdateScrollInfo();
	void OnVScroll(WPARAM wParam);
	void OnMouseWheel(int delta);

	App& m_app;
	HINSTANCE m_hInst;
	HWND m_hwnd{};

	HFONT m_hFont{ nullptr };
	HBRUSH m_darkBrush{ nullptr };

	int m_totalContentHeight{ 0 };
	int m_scrollY{ 0 };

	// 基本設定
	HWND m_modelPathEdit{};
	HWND m_browseBtn{};
	HWND m_topmostCheck{};
	HWND m_fpsEdit{};
	HWND m_fpsSpin{};
	HWND m_unlimitedFpsCheck{};

	// プリセット設定
	HWND m_presetModeCombo{};

	// スケール
	HWND m_scaleSlider{};
	HWND m_scaleLabel{};

	// ライト設定
	HWND m_brightnessSlider{};
	HWND m_brightnessLabel{};
	HWND m_ambientSlider{};
	HWND m_ambientLabel{};
	HWND m_globalSatSlider{};
	HWND m_globalSatLabel{};
	HWND m_keyIntensitySlider{};
	HWND m_keyIntensityLabel{};
	HWND m_keyColorBtn{};
	HWND m_keyColorPreview{};
	HWND m_fillIntensitySlider{};
	HWND m_fillIntensityLabel{};
	HWND m_fillColorBtn{};
	HWND m_fillColorPreview{};

	// Toon Control
	HWND m_toonEnableCheck{};
	HWND m_toonContrastSlider{};
	HWND m_toonContrastLabel{};
	HWND m_shadowHueSlider{};
	HWND m_shadowHueLabel{};
	HWND m_shadowSatSlider{};
	HWND m_shadowSatLabel{};
	HWND m_shadowRampSlider{};
	HWND m_shadowRampLabel{};

	// Deep Shadow
	HWND m_shadowDeepThresholdSlider{};
	HWND m_shadowDeepThresholdLabel{};
	HWND m_shadowDeepSoftSlider{};
	HWND m_shadowDeepSoftLabel{};
	HWND m_shadowDeepMulSlider{};
	HWND m_shadowDeepMulLabel{};
	HWND m_rimWidthSlider{};
	HWND m_rimWidthLabel{};
	HWND m_rimIntensitySlider{};
	HWND m_rimIntensityLabel{};
	HWND m_specularStepSlider{};
	HWND m_specularStepLabel{};

	// Face Control
	HWND m_faceShadowMulSlider{};
	HWND m_faceShadowMulLabel{};
	HWND m_faceContrastMulSlider{};
	HWND m_faceContrastMulLabel{};

	// Direction
	HWND m_keyDirXSlider{};
	HWND m_keyDirYSlider{};
	HWND m_keyDirZSlider{};
	HWND m_fillDirXSlider{};
	HWND m_fillDirYSlider{};
	HWND m_fillDirZSlider{};

	HWND m_resetLightBtn{};
	HWND m_savePresetBtn{};
	HWND m_loadPresetBtn{};

	HWND m_okBtn{};
	HWND m_cancelBtn{};
	HWND m_applyBtn{};

	bool m_created{ false };
	COLORREF m_customColors[16]{};
	AppSettings m_backupSettings{};
};