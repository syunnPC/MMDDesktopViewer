#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "SettingsWindow.hpp"
#include "App.hpp"
#include "Settings.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kClassName[] = L"MMDDesk.SettingsWindow";
	constexpr COLORREF kDarkBkColor = RGB(32, 32, 32);
	constexpr COLORREF kTextColor = RGB(240, 240, 240);

	constexpr int ID_MODEL_PATH = 101;
	constexpr int ID_BROWSE = 102;
	constexpr int ID_TOPMOST = 103;
	constexpr int ID_FPS_EDIT = 104;
	constexpr int ID_FPS_SPIN = 1041;
	constexpr int ID_FPS_UNLIMITED = 1042;
	constexpr int ID_PRESET_MODE_COMBO = 1043; // 新規

	constexpr int ID_SCALE_SLIDER = 105;
	constexpr int ID_BRIGHTNESS_SLIDER = 110;
	constexpr int ID_AMBIENT_SLIDER = 111;
	constexpr int ID_GLOBAL_SAT_SLIDER = 1111;
	constexpr int ID_KEY_INTENSITY_SLIDER = 112;
	constexpr int ID_FILL_INTENSITY_SLIDER = 113;
	constexpr int ID_KEY_DIR_X_SLIDER = 114;
	constexpr int ID_KEY_DIR_Y_SLIDER = 115;
	constexpr int ID_KEY_DIR_Z_SLIDER = 116;
	constexpr int ID_FILL_DIR_X_SLIDER = 117;
	constexpr int ID_FILL_DIR_Y_SLIDER = 118;
	constexpr int ID_FILL_DIR_Z_SLIDER = 119;

	constexpr int ID_RESET_LIGHT = 120;
	constexpr int ID_SAVE_PRESET = 121;
	constexpr int ID_LOAD_PRESET = 122;

	constexpr int ID_KEY_COLOR_BTN = 130;
	constexpr int ID_FILL_COLOR_BTN = 131;

	constexpr int ID_TOON_ENABLE = 140;
	constexpr int ID_TOON_CONTRAST_SLIDER = 141;
	constexpr int ID_SHADOW_HUE_SLIDER = 142;
	constexpr int ID_SHADOW_SAT_SLIDER = 143;
	constexpr int ID_RIM_WIDTH_SLIDER = 144;
	constexpr int ID_RIM_INTENSITY_SLIDER = 145;
	constexpr int ID_SPECULAR_STEP_SLIDER = 146;
	constexpr int ID_SHADOW_RAMP_SLIDER = 147;

	constexpr int ID_SHADOW_DEEP_THRESH_SLIDER = 148;
	constexpr int ID_SHADOW_DEEP_SOFT_SLIDER = 149;
	constexpr int ID_SHADOW_DEEP_MUL_SLIDER = 150;
	constexpr int ID_FACE_SHADOW_MUL_SLIDER = 151;
	constexpr int ID_FACE_CONTRAST_MUL_SLIDER = 152;

	constexpr int ID_OK = 200;
	constexpr int ID_CANCEL = 201;
	constexpr int ID_APPLY = 202;

	std::wstring FormatFloat(float val)
	{
		std::wostringstream oss;
		oss << std::fixed << std::setprecision(2) << val;
		return oss.str();
	}

	COLORREF FloatToColorRef(float r, float g, float b)
	{
		return RGB(static_cast<BYTE>(r * 255.0f), static_cast<BYTE>(g * 255.0f), static_cast<BYTE>(b * 255.0f));
	}

	int GetEditBoxInt(HWND hEdit, int defaultVal)
	{
		wchar_t buf[32]{};
		GetWindowTextW(hEdit, buf, static_cast<int>(std::size(buf)));
		try
		{
			return std::stoi(buf);
		}
		catch (...)
		{
			return defaultVal;
		}
	}
}

SettingsWindow::SettingsWindow(App& app, HINSTANCE hInst) : m_app(app), m_hInst(hInst)
{
	INITCOMMONCONTROLSEX icex{};
	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	m_darkBrush = CreateSolidBrush(kDarkBkColor);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kClassName;
	wc.hbrBackground = m_darkBrush;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	for (int i = 0; i < 16; ++i) m_customColors[i] = RGB(255, 255, 255);

	m_hFont = CreateFontW(
		-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

SettingsWindow::~SettingsWindow()
{
	if (m_hwnd) DestroyWindow(m_hwnd);
	UnregisterClassW(kClassName, m_hInst);
	if (m_hFont) DeleteObject(m_hFont);
	if (m_darkBrush) DeleteObject(m_darkBrush);
}

void SettingsWindow::Show()
{
	if (!m_hwnd)	
	{
		m_hwnd = CreateWindowExW(
			WS_EX_DLGMODALFRAME,
			kClassName, L"設定",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL | WS_SIZEBOX | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 560, 900,
			nullptr, nullptr, m_hInst, this);

		BOOL useDarkMode = TRUE;
		DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
	}

	if (!m_created)
	{
		CreateControls();
		m_created = true;
	}

	if (m_scrollY != 0)
	{
		ScrollWindowEx(m_hwnd, 0, m_scrollY, nullptr, nullptr, nullptr, nullptr,
					   SW_INVALIDATE | SW_SCROLLCHILDREN | SW_ERASE);
	}

	m_scrollY = 0;
	UpdateScrollInfo();
	ScrollWindow(m_hwnd, 0, 0, nullptr, nullptr);

	m_backupSettings = m_app.Settings();
	LoadCurrentSettings();
	ShowWindow(m_hwnd, SW_SHOW);
	SetForegroundWindow(m_hwnd);
}

void SettingsWindow::Hide()
{
	if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

void SettingsWindow::SetModernFont(HWND hChild)
{
	if (m_hFont && hChild) SendMessageW(hChild, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
}

void SettingsWindow::SetDarkTheme(HWND hChild)
{
	if (hChild) SetWindowTheme(hChild, L"DarkMode_Explorer", nullptr);
}

void SettingsWindow::CreateControls()
{
	int y = 15;
	const int labelW = 140;
	const int editW = 270;
	const int btnW = 70;
	const int sliderW = 190;
	const int rowH = 32;
	const int xPadding = 20;

	auto CreateLabel = [&](const wchar_t* text, int x, int y, int w) {
		HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, y, w, 24, m_hwnd, nullptr, m_hInst, nullptr);
		SetModernFont(h);
		return h;
		};

	auto CreateSlider = [&](int id, int x, int y, int w) {
		HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, 24, m_hwnd, reinterpret_cast<HMENU>(id), m_hInst, nullptr);
		SetModernFont(h);
		SetDarkTheme(h);
		return h;
		};

	// モデルパス
	CreateLabel(L"モデルパス:", xPadding, y, labelW);
	m_modelPathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, xPadding + labelW + 5, y, editW, 24, m_hwnd, reinterpret_cast<HMENU>(ID_MODEL_PATH), m_hInst, nullptr);
	SetModernFont(m_modelPathEdit); SetDarkTheme(m_modelPathEdit);
	m_browseBtn = CreateWindowExW(0, L"BUTTON", L"参照...", WS_CHILD | WS_VISIBLE, xPadding + labelW + 5 + editW + 5, y, btnW, 24, m_hwnd, reinterpret_cast<HMENU>(ID_BROWSE), m_hInst, nullptr);
	SetModernFont(m_browseBtn); SetDarkTheme(m_browseBtn);
	y += rowH + 8;

	m_topmostCheck = CreateWindowExW(0, L"BUTTON", L"常に最前面に表示", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, xPadding, y, 200, 24, m_hwnd, reinterpret_cast<HMENU>(ID_TOPMOST), m_hInst, nullptr);
	SetModernFont(m_topmostCheck); SetDarkTheme(m_topmostCheck);
	y += rowH + 10;

	CreateLabel(L"最大FPS:", xPadding, y, labelW);
	m_fpsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER, xPadding + labelW + 5, y, 70, 24, m_hwnd, reinterpret_cast<HMENU>(ID_FPS_EDIT), m_hInst, nullptr);
	SetModernFont(m_fpsEdit); SetDarkTheme(m_fpsEdit);
	m_fpsSpin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_ALIGNRIGHT, xPadding + labelW + 5 + 70, y, 20, 24, m_hwnd, reinterpret_cast<HMENU>(ID_FPS_SPIN), m_hInst, nullptr);
	SendMessageW(m_fpsSpin, UDM_SETRANGE32, 10, 240);
	SendMessageW(m_fpsSpin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(m_fpsEdit), 0);
	m_unlimitedFpsCheck = CreateWindowExW(0, L"BUTTON", L"無制限", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, xPadding + labelW + 5 + 100, y, 120, 24, m_hwnd, reinterpret_cast<HMENU>(ID_FPS_UNLIMITED), m_hInst, nullptr);
	SetModernFont(m_unlimitedFpsCheck); SetDarkTheme(m_unlimitedFpsCheck);
	y += rowH;

	CreateLabel(L"プリセット読み込み:", xPadding, y, labelW);
	m_presetModeCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, xPadding + labelW + 5, y, 200, 200, m_hwnd, reinterpret_cast<HMENU>(ID_PRESET_MODE_COMBO), m_hInst, nullptr);
	SetModernFont(m_presetModeCombo); SetDarkTheme(m_presetModeCombo);
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"モデル読み込み時に確認"));
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"常に読み込む"));
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"読み込まない"));
	y += rowH + 15;


	// 表示設定
	CreateLabel(L"【表示・ライト設定】", xPadding, y, 200); y += 24;
	CreateLabel(L"モデルサイズ:", xPadding, y, labelW);
	m_scaleSlider = CreateSlider(ID_SCALE_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_scaleSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 500));
	m_scaleLabel = CreateLabel(L"1.00", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"明るさ:", xPadding, y, labelW);
	m_brightnessSlider = CreateSlider(ID_BRIGHTNESS_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_brightnessSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 300));
	m_brightnessLabel = CreateLabel(L"1.30", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"環境光:", xPadding, y, labelW);
	m_ambientSlider = CreateSlider(ID_AMBIENT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_ambientSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_ambientLabel = CreateLabel(L"0.45", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"全体の彩度:", xPadding, y, labelW);
	m_globalSatSlider = CreateSlider(ID_GLOBAL_SAT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_globalSatSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_globalSatLabel = CreateLabel(L"1.10", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"主光源強度/色:", xPadding, y, labelW);
	m_keyIntensitySlider = CreateSlider(ID_KEY_INTENSITY_SLIDER, xPadding + labelW, y, sliderW - 80);
	SendMessageW(m_keyIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 300));
	m_keyIntensityLabel = CreateLabel(L"1.40", xPadding + labelW + (sliderW - 80) + 5, y, 40);
	m_keyColorBtn = CreateWindowExW(0, L"BUTTON", L"色", WS_CHILD | WS_VISIBLE, xPadding + labelW + (sliderW - 80) + 50, y, 40, 24, m_hwnd, reinterpret_cast<HMENU>(ID_KEY_COLOR_BTN), m_hInst, nullptr);
	SetModernFont(m_keyColorBtn); SetDarkTheme(m_keyColorBtn);
	m_keyColorPreview = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, xPadding + labelW + (sliderW - 80) + 95, y + 2, 20, 20, m_hwnd, nullptr, m_hInst, nullptr);
	y += rowH;

	CreateLabel(L"補助光源強度/色:", xPadding, y, labelW);
	m_fillIntensitySlider = CreateSlider(ID_FILL_INTENSITY_SLIDER, xPadding + labelW, y, sliderW - 80);
	SendMessageW(m_fillIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillIntensityLabel = CreateLabel(L"0.50", xPadding + labelW + (sliderW - 80) + 5, y, 40);
	m_fillColorBtn = CreateWindowExW(0, L"BUTTON", L"色", WS_CHILD | WS_VISIBLE, xPadding + labelW + (sliderW - 80) + 50, y, 40, 24, m_hwnd, reinterpret_cast<HMENU>(ID_FILL_COLOR_BTN), m_hInst, nullptr);
	SetModernFont(m_fillColorBtn); SetDarkTheme(m_fillColorBtn);
	m_fillColorPreview = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, xPadding + labelW + (sliderW - 80) + 95, y + 2, 20, 20, m_hwnd, nullptr, m_hInst, nullptr);
	y += rowH + 10;

	// Toon
	CreateLabel(L"【トゥーンシェーディング】", xPadding, y, 200); y += 24;
	m_toonEnableCheck = CreateWindowExW(0, L"BUTTON", L"トゥーンシェーディング有効", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, xPadding, y, 220, 24, m_hwnd, reinterpret_cast<HMENU>(ID_TOON_ENABLE), m_hInst, nullptr);
	SetModernFont(m_toonEnableCheck); SetDarkTheme(m_toonEnableCheck);
	y += rowH;

	CreateLabel(L"コントラスト:", xPadding, y, labelW);
	m_toonContrastSlider = CreateSlider(ID_TOON_CONTRAST_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_toonContrastSlider, TBM_SETRANGE, TRUE, MAKELONG(50, 250));
	m_toonContrastLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の色味(度):", xPadding, y, labelW);
	m_shadowHueSlider = CreateSlider(ID_SHADOW_HUE_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowHueSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 90));
	m_shadowHueLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の彩度:", xPadding, y, labelW);
	m_shadowSatSlider = CreateSlider(ID_SHADOW_SAT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowSatSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowSatLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の範囲:", xPadding, y, labelW);
	m_shadowRampSlider = CreateSlider(ID_SHADOW_RAMP_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowRampSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowRampLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	// Deep Shadow
	CreateLabel(L"濃い影の閾値:", xPadding, y, labelW);
	m_shadowDeepThresholdSlider = CreateSlider(ID_SHADOW_DEEP_THRESH_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepThresholdSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowDeepThresholdLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"濃い影の境界:", xPadding, y, labelW);
	m_shadowDeepSoftSlider = CreateSlider(ID_SHADOW_DEEP_SOFT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepSoftSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 50));
	m_shadowDeepSoftLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"濃い影の強度:", xPadding, y, labelW);
	m_shadowDeepMulSlider = CreateSlider(ID_SHADOW_DEEP_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowDeepMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"リム幅/強度:", xPadding, y, labelW);
	m_rimWidthSlider = CreateSlider(ID_RIM_WIDTH_SLIDER, xPadding + labelW, y, sliderW / 2 - 5);
	SendMessageW(m_rimWidthSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
	m_rimIntensitySlider = CreateSlider(ID_RIM_INTENSITY_SLIDER, xPadding + labelW + sliderW / 2 + 5, y, sliderW / 2 - 5);
	SendMessageW(m_rimIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_rimWidthLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"スペキュラ:", xPadding, y, labelW);
	m_specularStepSlider = CreateSlider(ID_SPECULAR_STEP_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_specularStepSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_specularStepLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH + 10;

	// Face Control
	CreateLabel(L"【顔マテリアル設定】", xPadding, y, 200); y += 24;
	CreateLabel(L"顔の影の濃さ:", xPadding, y, labelW);
	m_faceShadowMulSlider = CreateSlider(ID_FACE_SHADOW_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_faceShadowMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_faceShadowMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"顔のコントラスト:", xPadding, y, labelW);
	m_faceContrastMulSlider = CreateSlider(ID_FACE_CONTRAST_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_faceContrastMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_faceContrastMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH + 10;

	CreateLabel(L"主光源方向 (X/Y/Z):", xPadding, y, 200); y += 24;
	int slider3W = 145;
	m_keyDirXSlider = CreateSlider(ID_KEY_DIR_X_SLIDER, xPadding, y, slider3W);
	SendMessageW(m_keyDirXSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_keyDirYSlider = CreateSlider(ID_KEY_DIR_Y_SLIDER, xPadding + 150, y, slider3W);
	SendMessageW(m_keyDirYSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_keyDirZSlider = CreateSlider(ID_KEY_DIR_Z_SLIDER, xPadding + 300, y, slider3W);
	SendMessageW(m_keyDirZSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	y += rowH + 5;

	CreateLabel(L"補助光源方向 (X/Y/Z):", xPadding, y, 200); y += 24;
	m_fillDirXSlider = CreateSlider(ID_FILL_DIR_X_SLIDER, xPadding, y, slider3W);
	SendMessageW(m_fillDirXSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillDirYSlider = CreateSlider(ID_FILL_DIR_Y_SLIDER, xPadding + 150, y, slider3W);
	SendMessageW(m_fillDirYSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillDirZSlider = CreateSlider(ID_FILL_DIR_Z_SLIDER, xPadding + 300, y, slider3W);
	SendMessageW(m_fillDirZSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	y += rowH + 15;

	m_resetLightBtn = CreateWindowExW(0, L"BUTTON", L"ライト設定をリセット", WS_CHILD | WS_VISIBLE, xPadding, y, 160, 30, m_hwnd, reinterpret_cast<HMENU>(ID_RESET_LIGHT), m_hInst, nullptr);
	SetModernFont(m_resetLightBtn); SetDarkTheme(m_resetLightBtn);

	m_loadPresetBtn = CreateWindowExW(0, L"BUTTON", L"プリセットを読み込む", WS_CHILD | WS_VISIBLE, xPadding + 170, y, 160, 30, m_hwnd, reinterpret_cast<HMENU>(ID_LOAD_PRESET), m_hInst, nullptr);
	SetModernFont(m_loadPresetBtn); SetDarkTheme(m_loadPresetBtn);

	y += rowH + 25;

	const int btnOkW = 90;
	const int btnH = 30;

	// 左端: 保存ボタン
	m_savePresetBtn = CreateWindowExW(0, L"BUTTON", L"このモデルの設定を保存", WS_CHILD | WS_VISIBLE, xPadding, y, 180, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_SAVE_PRESET), m_hInst, nullptr);
	SetModernFont(m_savePresetBtn); SetDarkTheme(m_savePresetBtn);

	// 右寄せ: OK/Cancel/Apply
	const int footerX = 520 - xPadding - (btnOkW * 3 + 20);
	m_okBtn = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, footerX, y, btnOkW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_OK), m_hInst, nullptr);
	SetModernFont(m_okBtn); SetDarkTheme(m_okBtn);
	m_cancelBtn = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE, footerX + btnOkW + 10, y, btnOkW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_CANCEL), m_hInst, nullptr);
	SetModernFont(m_cancelBtn); SetDarkTheme(m_cancelBtn);
	m_applyBtn = CreateWindowExW(0, L"BUTTON", L"適用", WS_CHILD | WS_VISIBLE, footerX + btnOkW * 2 + 20, y, btnOkW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_APPLY), m_hInst, nullptr);
	SetModernFont(m_applyBtn); SetDarkTheme(m_applyBtn);

	m_totalContentHeight = y + btnH + 30;

	UpdateScrollInfo();
}

void SettingsWindow::UpdateScrollInfo()
{
	if (!m_hwnd) return;

	RECT rc;
	GetClientRect(m_hwnd, &rc);
	int clientHeight = rc.bottom - rc.top;

	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	si.nMin = 0;
	si.nMax = m_totalContentHeight;
	si.nPage = static_cast<UINT>(clientHeight);
	si.nPos = m_scrollY;

	SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void SettingsWindow::OnVScroll(WPARAM wParam)
{
	if (!m_hwnd) return;

	int action = LOWORD(wParam);
	int newY = m_scrollY;

	RECT rc;
	GetClientRect(m_hwnd, &rc);
	int page = rc.bottom - rc.top;

	switch (action)
	{
		case SB_TOP:        newY = 0; break;
		case SB_BOTTOM:     newY = m_totalContentHeight; break;
		case SB_LINEUP:     newY -= 20; break;
		case SB_LINEDOWN:   newY += 20; break;
		case SB_PAGEUP:     newY -= page; break;
		case SB_PAGEDOWN:   newY += page; break;
		case SB_THUMBTRACK:
		{
			SCROLLINFO si{};
			si.cbSize = sizeof(si);
			si.fMask = SIF_TRACKPOS;
			GetScrollInfo(m_hwnd, SB_VERT, &si);
			newY = si.nTrackPos;
			break;
		}
		default: return;
	}

	newY = std::max(0, std::min(newY, m_totalContentHeight - page));
	if (newY < 0) newY = 0;

	if (newY != m_scrollY)
	{
		ScrollWindowEx(m_hwnd, 0, m_scrollY - newY, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN | SW_ERASE);
		m_scrollY = newY;
		UpdateScrollInfo();
		UpdateWindow(m_hwnd);
	}
}

void SettingsWindow::OnMouseWheel(int delta)
{
	int scrollAmount = -(delta / WHEEL_DELTA) * 60;
	int newY = m_scrollY + scrollAmount;

	RECT rc;
	GetClientRect(m_hwnd, &rc);
	int page = rc.bottom - rc.top;

	newY = std::max(0, std::min(newY, m_totalContentHeight - page));
	if (newY < 0) newY = 0;

	if (newY != m_scrollY)
	{
		ScrollWindowEx(m_hwnd, 0, m_scrollY - newY, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN | SW_ERASE);
		m_scrollY = newY;
		UpdateScrollInfo();
		UpdateWindow(m_hwnd);
	}
}

void SettingsWindow::LoadCurrentSettings()
{
	const auto& settings = m_app.Settings();
	const auto& light = settings.light;

	SetWindowTextW(m_modelPathEdit, settings.modelPath.wstring().c_str());
	SendMessageW(m_topmostCheck, BM_SETCHECK, settings.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_unlimitedFpsCheck, BM_SETCHECK, settings.unlimitedFps ? BST_CHECKED : BST_UNCHECKED, 0);
	std::wostringstream fpsStr; fpsStr << settings.targetFps;
	SetWindowTextW(m_fpsEdit, fpsStr.str().c_str());
	SendMessageW(m_fpsSpin, UDM_SETPOS32, 0, settings.targetFps);
	UpdateFpsControlState();

	SendMessageW(m_presetModeCombo, CB_SETCURSEL, static_cast<WPARAM>(settings.globalPresetMode), 0);

	SendMessageW(m_scaleSlider, TBM_SETPOS, TRUE, static_cast<int>(light.modelScale * 100));
	SendMessageW(m_brightnessSlider, TBM_SETPOS, TRUE, static_cast<int>(light.brightness * 100));
	SendMessageW(m_ambientSlider, TBM_SETPOS, TRUE, static_cast<int>(light.ambientStrength * 100));
	SendMessageW(m_globalSatSlider, TBM_SETPOS, TRUE, static_cast<int>(light.globalSaturation * 100));
	SendMessageW(m_keyIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.keyLightIntensity * 100));
	SendMessageW(m_fillIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.fillLightIntensity * 100));

	SendMessageW(m_toonEnableCheck, BM_SETCHECK, light.toonEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_toonContrastSlider, TBM_SETPOS, TRUE, static_cast<int>(light.toonContrast * 100));
	SendMessageW(m_shadowHueSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowHueShiftDeg + 45.0f));
	SendMessageW(m_shadowSatSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowSaturationBoost * 100));
	int rampPos = static_cast<int>((light.shadowRampShift + 0.5f) * 100.0f);
	SendMessageW(m_shadowRampSlider, TBM_SETPOS, TRUE, rampPos);

	SendMessageW(m_shadowDeepThresholdSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepThreshold * 100.0f));
	SendMessageW(m_shadowDeepSoftSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepSoftness * 100.0f));
	SendMessageW(m_shadowDeepMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepMul * 100.0f));
	SendMessageW(m_faceShadowMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.faceShadowMul * 100.0f));
	SendMessageW(m_faceContrastMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.faceToonContrastMul * 100.0f));

	SendMessageW(m_rimWidthSlider, TBM_SETPOS, TRUE, static_cast<int>(light.rimWidth * 100));
	SendMessageW(m_rimIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.rimIntensity * 100));
	SendMessageW(m_specularStepSlider, TBM_SETPOS, TRUE, static_cast<int>(light.specularStep * 100));

	SendMessageW(m_keyDirXSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirX + 1.0f) * 100));
	SendMessageW(m_keyDirYSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirY + 1.0f) * 100));
	SendMessageW(m_keyDirZSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirZ + 1.0f) * 100));
	SendMessageW(m_fillDirXSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirX + 1.0f) * 100));
	SendMessageW(m_fillDirYSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirY + 1.0f) * 100));
	SendMessageW(m_fillDirZSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirZ + 1.0f) * 100));

	UpdateLightPreview();
	InvalidateRect(m_hwnd, nullptr, TRUE);
}

void SettingsWindow::UpdateLightPreview()
{
	auto& light = m_app.LightSettingsRef();

	auto GetVal = [&](HWND s) { return static_cast<float>(SendMessageW(s, TBM_GETPOS, 0, 0)); };

	light.modelScale = GetVal(m_scaleSlider) / 100.0f;
	light.brightness = GetVal(m_brightnessSlider) / 100.0f;
	light.ambientStrength = GetVal(m_ambientSlider) / 100.0f;
	light.globalSaturation = GetVal(m_globalSatSlider) / 100.0f;
	light.keyLightIntensity = GetVal(m_keyIntensitySlider) / 100.0f;
	light.fillLightIntensity = GetVal(m_fillIntensitySlider) / 100.0f;

	light.toonEnabled = (SendMessageW(m_toonEnableCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	light.toonContrast = GetVal(m_toonContrastSlider) / 100.0f;
	light.shadowHueShiftDeg = GetVal(m_shadowHueSlider) - 45.0f;
	light.shadowSaturationBoost = GetVal(m_shadowSatSlider) / 100.0f;
	light.shadowRampShift = (GetVal(m_shadowRampSlider) / 100.0f) - 0.5f;

	light.shadowDeepThreshold = GetVal(m_shadowDeepThresholdSlider) / 100.0f;
	light.shadowDeepSoftness = GetVal(m_shadowDeepSoftSlider) / 100.0f;
	light.shadowDeepMul = GetVal(m_shadowDeepMulSlider) / 100.0f;
	light.faceShadowMul = GetVal(m_faceShadowMulSlider) / 100.0f;
	light.faceToonContrastMul = GetVal(m_faceContrastMulSlider) / 100.0f;

	light.rimWidth = GetVal(m_rimWidthSlider) / 100.0f;
	light.rimIntensity = GetVal(m_rimIntensitySlider) / 100.0f;
	light.specularStep = GetVal(m_specularStepSlider) / 100.0f;

	light.keyLightDirX = GetVal(m_keyDirXSlider) / 100.0f - 1.0f;
	light.keyLightDirY = GetVal(m_keyDirYSlider) / 100.0f - 1.0f;
	light.keyLightDirZ = GetVal(m_keyDirZSlider) / 100.0f - 1.0f;
	light.fillLightDirX = GetVal(m_fillDirXSlider) / 100.0f - 1.0f;
	light.fillLightDirY = GetVal(m_fillDirYSlider) / 100.0f - 1.0f;
	light.fillLightDirZ = GetVal(m_fillDirZSlider) / 100.0f - 1.0f;

	SetWindowTextW(m_scaleLabel, FormatFloat(light.modelScale).c_str());
	SetWindowTextW(m_brightnessLabel, FormatFloat(light.brightness).c_str());
	SetWindowTextW(m_ambientLabel, FormatFloat(light.ambientStrength).c_str());
	SetWindowTextW(m_globalSatLabel, FormatFloat(light.globalSaturation).c_str());
	SetWindowTextW(m_keyIntensityLabel, FormatFloat(light.keyLightIntensity).c_str());
	SetWindowTextW(m_fillIntensityLabel, FormatFloat(light.fillLightIntensity).c_str());
	SetWindowTextW(m_toonContrastLabel, FormatFloat(light.toonContrast).c_str());
	SetWindowTextW(m_shadowHueLabel, FormatFloat(light.shadowHueShiftDeg).c_str());
	SetWindowTextW(m_shadowSatLabel, FormatFloat(light.shadowSaturationBoost).c_str());
	SetWindowTextW(m_shadowRampLabel, FormatFloat(light.shadowRampShift).c_str());
	SetWindowTextW(m_rimWidthLabel, FormatFloat(light.rimWidth).c_str());
	SetWindowTextW(m_rimIntensityLabel, FormatFloat(light.rimIntensity).c_str());
	SetWindowTextW(m_specularStepLabel, FormatFloat(light.specularStep).c_str());

	SetWindowTextW(m_shadowDeepThresholdLabel, FormatFloat(light.shadowDeepThreshold).c_str());
	SetWindowTextW(m_shadowDeepSoftLabel, FormatFloat(light.shadowDeepSoftness).c_str());
	SetWindowTextW(m_shadowDeepMulLabel, FormatFloat(light.shadowDeepMul).c_str());
	SetWindowTextW(m_faceShadowMulLabel, FormatFloat(light.faceShadowMul).c_str());
	SetWindowTextW(m_faceContrastMulLabel, FormatFloat(light.faceToonContrastMul).c_str());

	m_app.ApplyLightSettings();
}

void SettingsWindow::PickColor(float& r, float& g, float& b, HWND)
{
	CHOOSECOLORW cc{};
	cc.lStructSize = sizeof(cc);
	cc.hwndOwner = m_hwnd;
	cc.lpCustColors = m_customColors;
	cc.rgbResult = FloatToColorRef(r, g, b);
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	if (ChooseColorW(&cc))
	{
		r = GetRValue(cc.rgbResult) / 255.0f;
		g = GetGValue(cc.rgbResult) / 255.0f;
		b = GetBValue(cc.rgbResult) / 255.0f;

		m_app.ApplyLightSettings();
		InvalidateRect(m_hwnd, nullptr, TRUE);
	}
}

void SettingsWindow::ApplyAndSave()
{
	AppSettings newSettings = m_app.Settings();

	wchar_t buf[MAX_PATH]{};
	GetWindowTextW(m_modelPathEdit, buf, MAX_PATH);
	newSettings.modelPath = buf;

	newSettings.alwaysOnTop = (SendMessageW(m_topmostCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	newSettings.unlimitedFps = (SendMessageW(m_unlimitedFpsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

	int fps = GetEditBoxInt(m_fpsEdit, newSettings.targetFps);
	if (fps < 10) fps = 10;
	if (fps > 240) fps = 240;
	newSettings.targetFps = fps;

	// PresetMode
	int sel = (int)SendMessageW(m_presetModeCombo, CB_GETCURSEL, 0, 0);
	if (sel >= 0) newSettings.globalPresetMode = static_cast<PresetMode>(sel);

	newSettings.light = m_app.LightSettingsRef();

	m_app.ApplySettings(newSettings, true);
	m_backupSettings = m_app.Settings();
}

void SettingsWindow::UpdateFpsControlState()
{
	const bool unlimited = (SendMessageW(m_unlimitedFpsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	EnableWindow(m_fpsEdit, !unlimited);
	EnableWindow(m_fpsSpin, !unlimited);
}

LRESULT CALLBACK SettingsWindow::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SettingsWindow* self = nullptr;
	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<SettingsWindow*>(cs->lpCreateParams);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}
	if (self) return self->WndProc(hWnd, msg, wParam, lParam);
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, kTextColor);
			SetBkMode(hdc, TRANSPARENT);
			return (LRESULT)m_darkBrush;
		}
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, kTextColor);
			SetBkColor(hdc, kDarkBkColor);
			return (LRESULT)m_darkBrush;
		}

		case WM_VSCROLL:
			OnVScroll(wParam);
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
			return 0;

		case WM_SIZE:
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			int clientH = rc.bottom - rc.top;

			int maxPos = std::max(0, m_totalContentHeight - clientH);

			int newY = std::min(m_scrollY, maxPos);
			if (newY < 0) newY = 0;

			if (newY != m_scrollY)
			{
				int dy = m_scrollY - newY;
				m_scrollY = newY;
				ScrollWindowEx(hWnd, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN | SW_ERASE);
			}

			UpdateScrollInfo();
			return 0;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case ID_BROWSE: {
					wchar_t path[MAX_PATH]{};
					OPENFILENAMEW ofn{};
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = hWnd;
					ofn.lpstrFilter = L"PMXモデル (*.pmx)\0*.pmx\0すべてのファイル\0*.*\0";
					ofn.lpstrFile = path;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_FILEMUSTEXIST;
					if (GetOpenFileNameW(&ofn)) SetWindowTextW(m_modelPathEdit, path);
					return 0;
				}
				case ID_KEY_COLOR_BTN: {
					auto& light = m_app.LightSettingsRef();
					PickColor(light.keyLightColorR, light.keyLightColorG, light.keyLightColorB, m_keyColorBtn);
					return 0;
				}
				case ID_FILL_COLOR_BTN: {
					auto& light = m_app.LightSettingsRef();
					PickColor(light.fillLightColorR, light.fillLightColorG, light.fillLightColorB, m_fillColorBtn);
					return 0;
				}
				case ID_TOON_ENABLE: UpdateLightPreview(); return 0;
				case ID_FPS_UNLIMITED: UpdateFpsControlState(); return 0;
				case ID_RESET_LIGHT: {
					LightSettings defaultLight;
					m_app.LightSettingsRef() = defaultLight;
					LoadCurrentSettings();
					m_app.ApplyLightSettings();
					return 0;
				}
				case ID_SAVE_PRESET: {
					const auto& settings = m_app.Settings();
					if (settings.modelPath.empty())
					{
						MessageBoxW(m_hwnd, L"モデルが読み込まれていません。", L"エラー", MB_ICONWARNING);
					}
					else
					{
						SettingsManager::SavePreset(m_app.BaseDir(), settings.modelPath, settings.light);
						MessageBoxW(m_hwnd, L"現在のライト設定をモデル用プリセットとして保存しました。", L"保存完了", MB_ICONINFORMATION);
					}
					return 0;
				}
				case ID_LOAD_PRESET: {
					const auto& settings = m_app.Settings();
					if (settings.modelPath.empty())
					{
						MessageBoxW(m_hwnd, L"モデルが読み込まれていません。", L"エラー", MB_ICONWARNING);
					}
					else
					{
						if (SettingsManager::LoadPreset(m_app.BaseDir(), settings.modelPath, m_app.LightSettingsRef()))
						{
							LoadCurrentSettings(); // UI反映
							m_app.ApplyLightSettings();
							MessageBoxW(m_hwnd, L"プリセットを読み込みました。", L"完了", MB_ICONINFORMATION);
						}
						else
						{
							MessageBoxW(m_hwnd, L"このモデル用のプリセットが見つかりません。", L"エラー", MB_ICONWARNING);
						}
					}
					return 0;
				}
				case ID_OK: ApplyAndSave(); Hide(); return 0;
				case ID_CANCEL: m_app.ApplySettings(m_backupSettings, false); LoadCurrentSettings(); Hide(); return 0;
				case ID_APPLY: ApplyAndSave(); return 0;
			}
			break;

		case WM_HSCROLL:
			UpdateLightPreview();
			return 0;

		case WM_DRAWITEM: {
			auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
			if (dis->hwndItem == m_keyColorPreview || dis->hwndItem == m_fillColorPreview)
			{
				FillRect(dis->hDC, &dis->rcItem, m_darkBrush);
				RECT rc = dis->rcItem; InflateRect(&rc, -2, -2);
				HBRUSH brush = nullptr;
				auto& light = m_app.LightSettingsRef();
				if (dis->hwndItem == m_keyColorPreview) brush = CreateSolidBrush(FloatToColorRef(light.keyLightColorR, light.keyLightColorG, light.keyLightColorB));
				else brush = CreateSolidBrush(FloatToColorRef(light.fillLightColorR, light.fillLightColorG, light.fillLightColorB));
				FillRect(dis->hDC, &rc, brush);
				DeleteObject(brush);
				HBRUSH frameBrush = CreateSolidBrush(RGB(100, 100, 100));
				FrameRect(dis->hDC, &rc, frameBrush);
				DeleteObject(frameBrush);
				return TRUE;
			}
			break;
		}

		case WM_CLOSE:
			m_app.ApplySettings(m_backupSettings, false);
			Hide();
			return 0;

		default: break;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}