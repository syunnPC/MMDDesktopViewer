#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "App.hpp"
#include "FileUtil.hpp"
#include "SettingsWindow.hpp"
#include "Settings.hpp"
#include "ProgressWindow.hpp"
#include <algorithm>
#include <format>
#include <thread>
#include <stdexcept>
#include <string>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kMsgClassName[] = L"MMDDesk.MsgWindow";
	constexpr wchar_t kRenderClassName[] = L"MMDDesk.RenderWindow";
	constexpr wchar_t kGizmoClassName[] = L"MMDDesk.GizmoWindow";

	constexpr UINT_PTR kTimerId = 1;
	constexpr UINT kDefaultTimerMs = 16;

	constexpr int kGizmoSizePx = 140;
	constexpr int kGizmoMarginPx = 16;

	constexpr int kHotKeyToggleGizmoId = 1;
	constexpr int kHotKeyTogglePhysicsId = 2;
	constexpr int kHotKeyToggleWindowManipId = 3;

	constexpr UINT kHotKeyToggleGizmoMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyToggleGizmoVk = 'G';
	constexpr UINT kHotKeyTogglePhysicsMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyTogglePhysicsVk = 'P';
	constexpr UINT kHotKeyToggleWindowManipMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyToggleWindowManipVk = 'R';

	enum TrayCmd : UINT
	{
		CMD_OPEN_SETTINGS = 100,
		CMD_RELOAD_MOTIONS = 101,
		CMD_STOP_MOTION = 102,
		CMD_TOGGLE_PAUSE = 103,
		CMD_TOGGLE_PHYSICS = 104,
		CMD_TOGGLE_WINDOW_MANIP = 105,
		CMD_EXIT = 199,
		CMD_TOGGLE_LOOKAT = 106,
		CMD_MOTION_BASE = 1000
	};

	DWORD GetWindowStyleExForRender()
	{
		return WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
	}

	DWORD GetWindowStyleForRender()
	{
		return WS_POPUP;
	}

	constexpr wchar_t kPropWindowManipulationMode[] = L"MMDDesk.WindowManipulationMode";

	bool IsWindowManipulationMode(HWND hWnd)
	{
		return hWnd && GetPropW(hWnd, kPropWindowManipulationMode) != nullptr;
	}

	void SetWindowManipulationModeProp(HWND hWnd, bool enabled)
	{
		if (!hWnd) return;
		if (enabled)
		{
			SetPropW(hWnd, kPropWindowManipulationMode, reinterpret_cast<HANDLE>(1));
		}
		else
		{
			RemovePropW(hWnd, kPropWindowManipulationMode);
		}
	}

	void ApplyWindowManipulationMode(HWND hWnd, DcompRenderer* renderer, bool enabled)
	{
		if (!hWnd) return;

		SetWindowManipulationModeProp(hWnd, enabled);

		if (renderer)
		{
			renderer->SetResizeOverlayEnabled(enabled);
		}

		// 子ウィンドウも含めて透過/非アクティブ化を切り替える
		auto applyTo = [enabled](HWND w)
			{
				if (!w) return;

				EnableWindow(w, enabled ? TRUE : FALSE);

				LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
				if (enabled)
				{
					ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
				}
				else
				{
					ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
				}
				SetWindowLongPtrW(w, GWL_EXSTYLE, ex);
			};

		applyTo(hWnd);
		EnumChildWindows(
			hWnd,
			[](HWND child, LPARAM lp) -> BOOL
			{
				const bool en = (lp != 0);
				EnableWindow(child, en ? TRUE : FALSE);

				LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
				if (en)
				{
					ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
				}
				else
				{
					ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
				}
				SetWindowLongPtrW(child, GWL_EXSTYLE, ex);

				SetWindowPos(child, nullptr, 0, 0, 0, 0,
							 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
				return TRUE;
			},
			enabled ? 1 : 0);

		// システムのリサイズを使うためのスタイル（見た目は WM_NCCALCSIZE 等で消す）
		LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
		if (enabled)
		{
			style |= WS_THICKFRAME;
		}
		else
		{
			style &= ~WS_THICKFRAME;
		}
		SetWindowLongPtrW(hWnd, GWL_STYLE, style);

		SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

App::App(HINSTANCE hInst) : m_hInst(hInst)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(hr))
	{
		m_comInitialized = true;
	}
	else if (hr == RPC_E_CHANGED_MODE)
	{
		m_comInitialized = false;
	}
	else
	{
		throw std::runtime_error("CoInitializeEx failed.");
	}

	m_baseDir = FileUtil::GetExecutableDir();
	m_modelsDir = m_baseDir / L"Models";
	m_motionsDir = m_baseDir / L"Motions";

	const auto defaultModel = m_modelsDir / L"default.pmx";
	m_settingsData = SettingsManager::Load(m_baseDir, defaultModel);

	if (m_settingsData.modelPath.empty())
	{
		m_settingsData.modelPath = defaultModel;
	}
	else if (!m_settingsData.modelPath.is_absolute())
	{
		m_settingsData.modelPath = m_baseDir / m_settingsData.modelPath;
	}

	CreateHiddenMessageWindow();
	CreateRenderWindow();
	if (!RegisterHotKey(m_renderWnd, kHotKeyToggleGizmoId, kHotKeyToggleGizmoMods, kHotKeyToggleGizmoVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+G).\n");
	}
	if (!RegisterHotKey(m_renderWnd, kHotKeyTogglePhysicsId, kHotKeyTogglePhysicsMods, kHotKeyTogglePhysicsVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+P).\n");
	}
	if (!RegisterHotKey(m_renderWnd, kHotKeyToggleWindowManipId, kHotKeyToggleWindowManipMods, kHotKeyToggleWindowManipVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+R).\n");
	}
	CreateGizmoWindow();

	ApplyTopmost();

	InitRenderer();
	InitAnimator();

	BuildTrayMenu();
	InitTray();

	UpdateTimerInterval();
}

App::~App()
{
	if (m_trayMenu) DestroyMenu(m_trayMenu);
	if (m_msgWnd) KillTimer(m_msgWnd, kTimerId);
	if (m_renderWnd) UnregisterHotKey(m_renderWnd, kHotKeyToggleGizmoId);
	if (m_renderWnd) UnregisterHotKey(m_renderWnd, kHotKeyTogglePhysicsId);
	if (m_renderWnd) UnregisterHotKey(m_renderWnd, kHotKeyToggleWindowManipId);
	if (m_gizmoWnd) DestroyWindow(m_gizmoWnd);

	// GDIリソースの解放
	if (m_gizmoOldBmp && m_gizmoDc) SelectObject(m_gizmoDc, m_gizmoOldBmp);
	if (m_gizmoBmp) DeleteObject(m_gizmoBmp);
	if (m_gizmoDc) DeleteDC(m_gizmoDc);

	if (m_comInitialized)
	{
		CoUninitialize();
	}
}

int App::Run()
{
	MSG msg{};
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return static_cast<int>(msg.wParam);
}

void App::LoadModelFromSettings()
{
	if (!m_animator) return;
	if (m_settingsData.modelPath.empty()) return;

	// 既に読み込み中なら無視
	if (m_isLoading) return;

	StartLoadingModel(m_settingsData.modelPath);
}

void App::StartLoadingModel(const std::filesystem::path& path)
{
	if (!std::filesystem::exists(path)) return;

	if (SettingsManager::HasPreset(m_baseDir, path))
	{
		bool doLoad = false;
		bool showDialog = true;

		std::wstring filename = path.filename().wstring();

		// 1. モデルごとの設定を確認
		auto it = m_settingsData.perModelPresetSettings.find(filename);
		if (it != m_settingsData.perModelPresetSettings.end())
		{
			if (it->second == PresetMode::AlwaysLoad)
			{
				doLoad = true; showDialog = false;
			}
			else if (it->second == PresetMode::NeverLoad)
			{
				doLoad = false; showDialog = false;
			}
		}
		// 2. なければ全体設定を確認
		else
		{
			if (m_settingsData.globalPresetMode == PresetMode::AlwaysLoad)
			{
				doLoad = true; showDialog = false;
			}
			else if (m_settingsData.globalPresetMode == PresetMode::NeverLoad)
			{
				doLoad = false; showDialog = false;
			}
		}

		// 3. 必要ならダイアログ表示
		if (showDialog)
		{
			// TaskDialogの設定
			TASKDIALOGCONFIG config = { 0 };
			config.cbSize = sizeof(config);
			config.hwndParent = m_renderWnd;
			config.hInstance = m_hInst;
			config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS_NO_ICON;
			config.pszWindowTitle = L"設定の読み込み";
			config.pszMainInstruction = L"このモデル用の設定プリセットが見つかりました。";
			config.pszContent = L"保存された照明・表示設定を適用しますか？";

			TASKDIALOG_BUTTON buttons[] = {
				{ IDYES, L"読み込む" },
				{ IDNO, L"読み込まない" }
			};
			config.pButtons = buttons;
			config.cButtons = _countof(buttons);
			config.nDefaultButton = IDYES;

			// ラジオボタンで「今後どうするか」を選択させる
			// ID 100: 次回も確認 (デフォルト)
			// ID 101: このモデルは次回から確認しない (個別設定)
			// ID 102: すべてのモデルで次回から確認しない (全体設定)
			TASKDIALOG_BUTTON radios[] = {
				{ 100, L"次回も確認する" },
				{ 101, L"このモデルは次回から同じ選択をする" },
				{ 102, L"すべてのモデルで次回から同じ選択をする" }
			};
			config.pRadioButtons = radios;
			config.cRadioButtons = _countof(radios);
			config.nDefaultRadioButton = 100;

			int nButton = 0;
			int nRadio = 0;

			HRESULT hr = TaskDialogIndirect(&config, &nButton, &nRadio, nullptr);

			if (SUCCEEDED(hr))
			{
				bool yes = (nButton == IDYES);
				doLoad = yes;

				// 設定更新
				if (nRadio == 101) // このモデルのみ
				{
					m_settingsData.perModelPresetSettings[filename] = yes ? PresetMode::AlwaysLoad : PresetMode::NeverLoad;
					SettingsManager::Save(m_baseDir, m_settingsData);
				}
				else if (nRadio == 102) // 全体
				{
					m_settingsData.globalPresetMode = yes ? PresetMode::AlwaysLoad : PresetMode::NeverLoad;
					SettingsManager::Save(m_baseDir, m_settingsData);
				}
			}
		}

		if (doLoad)
		{
			if (SettingsManager::LoadPreset(m_baseDir, path, m_settingsData.light))
			{
				ApplyLightSettings();
				if (m_settings)
				{
					m_settings->Refresh();
				}
			}
		}
	}

	m_isLoading = true;

	if (!m_progress)
	{
		m_progress = std::make_unique<ProgressWindow>(m_hInst, m_renderWnd);
	}
	m_progress->Show();
	m_progress->SetMessage(L"読み込み開始...");
	m_progress->SetProgress(0.0f);

	// ワーカースレッド起動
	std::thread([this, path]() {
		HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		PmxModel* loadedModelPtr = nullptr;

		try
		{
			auto newModel = std::make_unique<PmxModel>();

			// 1. PMXファイル解析
			bool res = newModel->Load(path, [this](float p, const wchar_t* msg) {
				if (m_progress)
				{
					m_progress->SetMessage(msg);
					m_progress->SetProgress(p * 0.6f);
				}
									  });

			if (res)
			{
				// 2. テクスチャの先行読み込み
				if (m_renderer)
				{
					m_renderer->LoadTexturesForModel(newModel.get(), [this](float p, const wchar_t* msg) {
						if (m_progress)
						{
							m_progress->SetMessage(msg);
							m_progress->SetProgress(p);
						}
													 }, 0.6f, 1.0f);
				}

				// 所有権を移動するためにrelease()する
				loadedModelPtr = newModel.release();
			}
		}
		catch (const std::exception& e)
		{
			auto buf = std::format("Model Load Error: {}\n", e.what());
			OutputDebugStringA(buf.c_str());
			loadedModelPtr = nullptr; // 失敗扱い
		}
		catch (...)
		{
			OutputDebugStringA("Model Load Error: Unknown exception\n");
			loadedModelPtr = nullptr;
		}

		// 完了通知 (成功時はポインタ、失敗時はnullptr)
		PostMessage(m_msgWnd, WM_APP_LOAD_COMPLETE, 0, reinterpret_cast<LPARAM>(loadedModelPtr));

		if (SUCCEEDED(hr))
		{
			CoUninitialize();
		}

				}).detach();
}

void App::OnTimer()
{
	if (m_isLoading) return;

	// LookAt 計算
	if (m_lookAtEnabled && m_animator && m_renderer)
	{
		POINT pt{};
		GetCursorPos(&pt);
		ScreenToClient(m_renderWnd, &pt);

		// 頭の現在位置（3D）を取得
		auto headPos3D = m_animator->GetBoneGlobalPosition(L"頭");

		// 頭のスクリーン位置を取得
		auto headPosScreen = m_renderer->ProjectToScreen(headPos3D);

		// スクリーン上での偏差
		float dx = (float)pt.x - headPosScreen.x;
		float dy = (float)pt.y - headPosScreen.y;

		RECT rc;
		GetClientRect(m_renderWnd, &rc);
		float h = (float)(rc.bottom - rc.top);
		if (h < 1.0f) h = 1.0f;

		float dist = h * 1.5f; // 係数で感度調整

		// dx(右) -> 右を向くにはY軸負回転
		float yaw = -std::atan2(dx, dist);

		float pitch = -std::atan2(dy, dist);

		m_animator->SetLookAtState(true, yaw, pitch);
	}

	if (m_animator)
	{
		m_animator->Update();
	}

	if (m_renderer && m_animator)
	{
		m_renderer->Render(*m_animator);
	}

	if (m_gizmoVisible && m_gizmoWnd)
	{
		PositionGizmoWindow();
		InvalidateRect(m_gizmoWnd, nullptr, FALSE);
	}
}

void App::CreateHiddenMessageWindow()
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kMsgClassName;

	if (!RegisterClassExW(&wc))
	{
		throw std::runtime_error("RegisterClassExW (MsgWindow) failed.");
	}

	m_msgWnd = CreateWindowExW(
		0, kMsgClassName, L"MMDDesk Message Window",
		0, 0, 0, 0, 0,
		HWND_MESSAGE, nullptr, m_hInst, this);

	if (!m_msgWnd)
	{
		throw std::runtime_error("CreateWindowExW (MsgWindow) failed.");
	}
}

void App::CreateRenderWindow()
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kRenderClassName;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;

	if (!RegisterClassExW(&wc))
	{
		throw std::runtime_error("RegisterClassExW (RenderWindow) failed.");
	}

	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);

	int w = 400;
	int h = 600;
#if !DCOMP_AUTOFIT_WINDOW
	// 自動フィット無効時は、初期サイズが小さすぎないようにする
	w = std::clamp(screenW / 3, 480, 720);
	h = std::clamp((screenH * 2) / 3, 720, 1200);
#endif
	const int x = screenW - w - 50;
	const int y = screenH - h - 100;

	m_renderWnd = CreateWindowExW(
		GetWindowStyleExForRender(),
		kRenderClassName, L"MMDDesk",
		GetWindowStyleForRender(),
		x, y, w, h,
		nullptr, nullptr, m_hInst, this);

	if (!m_renderWnd)
	{
		throw std::runtime_error("CreateWindowExW (RenderWindow) failed.");
	}

	BOOL useDarkMode = TRUE;
	DwmSetWindowAttribute(m_renderWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

	ShowWindow(m_renderWnd, SW_SHOWNOACTIVATE);
}

void App::CreateGizmoWindow()
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kGizmoClassName;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;

	// 既に登録済みなら成功扱い
	if (!RegisterClassExW(&wc))
	{
		const DWORD err = GetLastError();
		if (err != ERROR_CLASS_ALREADY_EXISTS)
		{
			throw std::runtime_error("RegisterClassExW (GizmoWindow) failed.");
		}
	}

	// 入力を受ける UI ウィンドウ
	// WS_EX_LAYERED は UpdateLayeredWindow を使うために必須
	const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED;
	const DWORD style = WS_POPUP;

	m_gizmoWnd = CreateWindowExW(
		exStyle,
		kGizmoClassName,
		L"MMDDesk Gizmo",
		style,
		0, 0, kGizmoSizePx, kGizmoSizePx,
		nullptr, nullptr, m_hInst, this);

	if (!m_gizmoWnd)
	{
		throw std::runtime_error("CreateWindowExW (GizmoWindow) failed.");
	}

	// 以前の CreateEllipticRgn と SetLayeredWindowAttributes を削除
	// これらはジャギーの原因となるため、UpdateLayeredWindow によるピクセル単位のアルファブレンドに移行します。

	ShowWindow(m_gizmoWnd, SW_HIDE);
}

void App::ToggleGizmoWindow()
{
	if (!m_gizmoWnd) return;

	if (m_gizmoVisible)
	{
		m_gizmoVisible = false;
		m_gizmoLeftDrag = false;
		m_gizmoRightDrag = false;
		ReleaseCapture();
		ShowWindow(m_gizmoWnd, SW_HIDE);
		return;
	}

	m_gizmoVisible = true;
	PositionGizmoWindow();
	ShowWindow(m_gizmoWnd, SW_SHOWNOACTIVATE);
	InvalidateRect(m_gizmoWnd, nullptr, FALSE);
}

void App::PositionGizmoWindow()
{
	if (!m_gizmoWnd || !m_renderWnd) return;

	RECT rc{};
	GetWindowRect(m_renderWnd, &rc);

	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;

	const int x = rc.left + (w - kGizmoSizePx) / 2;
	const int y = rc.top + (h - kGizmoSizePx) / 2;

	HWND insertAfter = m_settingsData.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
	SetWindowPos(
		m_gizmoWnd,
		insertAfter,
		x,
		y,
		kGizmoSizePx,
		kGizmoSizePx,
		SWP_NOACTIVATE);
}

void App::EnsureGizmoD2D()
{
	if (!m_gizmoWnd) return;

	if (!m_d2dFactory)
	{
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("D2D1CreateFactory failed.");
		}
	}

	// GDIリソース（ビットマップ、メモリDC）の作成
	if (!m_gizmoDc)
	{
		HDC screenDc = GetDC(nullptr);
		m_gizmoDc = CreateCompatibleDC(screenDc);
		ReleaseDC(nullptr, screenDc);
	}

	if (!m_gizmoBmp)
	{
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = kGizmoSizePx;
		bmi.bmiHeader.biHeight = -kGizmoSizePx; // Top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32; // Alpha channelあり
		bmi.bmiHeader.biCompression = BI_RGB;

		// 32ビットビットマップを作成（初期値は0クリア＝透明）
		m_gizmoBmp = CreateDIBSection(m_gizmoDc, &bmi, DIB_RGB_COLORS, &m_gizmoBits, nullptr, 0);
		if (m_gizmoBmp)
		{
			m_gizmoOldBmp = SelectObject(m_gizmoDc, m_gizmoBmp);
		}
	}

	// DCレンダーターゲットの作成
	if (!m_gizmoRt)
	{
		D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_SOFTWARE,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			0.0f, 0.0f,
			D2D1_RENDER_TARGET_USAGE_NONE,
			D2D1_FEATURE_LEVEL_DEFAULT
		);

		HRESULT hr = m_d2dFactory->CreateDCRenderTarget(&props, &m_gizmoRt);
		if (FAILED(hr))
		{
			throw std::runtime_error("CreateDCRenderTarget failed.");
		}

		m_gizmoRt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

		// ブラシの作成 (アルファ値を調整して質感を向上)
		// 塗りつぶし: 暗めのグレー、透過度60%程度
		m_gizmoRt->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.6f), m_gizmoBrushFill.GetAddressOf());
		// 枠線: 明るい白、透過度90%程度（くっきりさせる）
		m_gizmoRt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f, 0.9f), m_gizmoBrushStroke.GetAddressOf());
	}
}

void App::DiscardGizmoD2D()
{
	m_gizmoRt.Reset();
	m_gizmoBrushFill.Reset();
	m_gizmoBrushStroke.Reset();
}

void App::RenderGizmo()
{
	if (!m_gizmoVisible || !m_gizmoWnd) return;
	EnsureGizmoD2D();
	if (!m_gizmoRt || !m_gizmoDc) return;

	const float width = static_cast<float>(kGizmoSizePx);
	const float height = static_cast<float>(kGizmoSizePx);
	const float cx = width * 0.5f;
	const float cy = height * 0.5f;
	const float radius = (std::min)(width, height) * 0.5f - 2.0f; // 少し余白を持たせる

	// DCをレンダーターゲットにバインド
	RECT rc = { 0, 0, kGizmoSizePx, kGizmoSizePx };
	HRESULT hr = m_gizmoRt->BindDC(m_gizmoDc, &rc);

	if (FAILED(hr))
	{
		if (hr == D2DERR_RECREATE_TARGET) DiscardGizmoD2D();
		return;
	}

	m_gizmoRt->BeginDraw();
	m_gizmoRt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

	D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
	m_gizmoRt->FillEllipse(el, m_gizmoBrushFill.Get());
	m_gizmoRt->DrawEllipse(el, m_gizmoBrushStroke.Get(), 2.0f); // 枠線を少し細くして上品に

	// 目印（十字）
	const float tick = radius * 0.55f;
	m_gizmoRt->DrawLine(D2D1::Point2F(cx - tick, cy), D2D1::Point2F(cx + tick, cy), m_gizmoBrushStroke.Get(), 1.5f);
	m_gizmoRt->DrawLine(D2D1::Point2F(cx, cy - tick), D2D1::Point2F(cx, cy + tick), m_gizmoBrushStroke.Get(), 1.5f);

	hr = m_gizmoRt->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		if (hr != D2DERR_RECREATE_TARGET)
		{
			OutputDebugStringW(std::format(L"EndDraw hr=0x{:08X}\n", (unsigned)hr).c_str());
		}
		DiscardGizmoD2D();
		return;
	}

	// UpdateLayeredWindow で画面に反映 (ピクセル単位のアルファブレンド)
	BLENDFUNCTION bf = {};
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA; // ピクセルのアルファ値を使用

	POINT ptSrc = { 0, 0 };
	SIZE wndSize = { kGizmoSizePx, kGizmoSizePx };

	RECT wndRect;
	GetWindowRect(m_gizmoWnd, &wndRect);
	POINT ptDst = { wndRect.left, wndRect.top };

	UpdateLayeredWindow(m_gizmoWnd, nullptr, &ptDst, &wndSize, m_gizmoDc, &ptSrc, 0, &bf, ULW_ALPHA);
}

void App::InitRenderer()
{
	if (!m_progress)
	{
		m_progress = std::make_unique<ProgressWindow>(m_hInst, m_renderWnd);
	}

	m_progress->Show();
	m_progress->SetProgress(0.0f);
	m_progress->SetMessage(L"レンダラーを初期化しています...");

	auto onProgress = [this](float p, const wchar_t* msg) {
		if (!m_progress) return;
		m_progress->SetProgress(p);
		if (msg && msg[0] != L'\0')
		{
			m_progress->SetMessage(msg);
		}
		};

	m_renderer = std::make_unique<DcompRenderer>();
	m_renderer->Initialize(m_renderWnd, onProgress);

	InstallRenderClickThrough();
	ForceRenderTreeClickThrough();

	// 保存されたライト設定を適用
	m_renderer->SetLightSettings(m_settingsData.light);

	if (m_progress)
	{
		m_progress->Hide();
	}
}

void App::MakeClickThrough(HWND hWnd)
{
	if (!hWnd) return;

	EnableWindow(hWnd, FALSE);

	LONG_PTR ex = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
	ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
	SetWindowLongPtrW(hWnd, GWL_EXSTYLE, ex);

	SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
				 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

BOOL CALLBACK App::EnumChildForClickThrough(HWND hWnd, LPARAM lParam)
{
	MakeClickThrough(hWnd);
	return TRUE;
}

void App::ForceRenderTreeClickThrough()
{
	// render 本体
	MakeClickThrough(m_renderWnd);
	EnumChildWindows(m_renderWnd, &App::EnumChildForClickThrough, 0);
}


void App::InstallRenderClickThrough()
{
	if (!m_renderWnd) return;

	LONG_PTR ex = GetWindowLongPtrW(m_renderWnd, GWL_EXSTYLE);
	ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
	SetWindowLongPtrW(m_renderWnd, GWL_EXSTYLE, ex);

	SetWindowPos(
		m_renderWnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	if (!m_prevRenderWndProc)
	{
		m_prevRenderWndProc = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrW(
				m_renderWnd,
				GWLP_WNDPROC,
				reinterpret_cast<LONG_PTR>(&App::RenderClickThroughProc)));
	}
}

LRESULT CALLBACK App::RenderClickThroughProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// GWLP_USERDATA は WndProcThunk が設定済み :contentReference[oaicite:3]{index=3}
	App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

	switch (msg)
	{
		case WM_NCHITTEST:
		{
			if (!IsWindowManipulationMode(hWnd))
			{
				return HTTRANSPARENT;
			}

			POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT rc{};
			GetWindowRect(hWnd, &rc);

			const UINT dpi = GetDpiForWindow(hWnd);
			const float s = (dpi > 0) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
			int border = static_cast<int>(14.0f * s + 0.5f);
			border = std::clamp(border, 10, 32);

			const bool left = (pt.x >= rc.left && pt.x < rc.left + border);
			const bool right = (pt.x <= rc.right && pt.x > rc.right - border);
			const bool top = (pt.y >= rc.top && pt.y < rc.top + border);
			const bool bottom = (pt.y <= rc.bottom && pt.y > rc.bottom - border);

			if (top && left) return HTTOPLEFT;
			if (top && right) return HTTOPRIGHT;
			if (bottom && left) return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;
			if (left) return HTLEFT;
			if (right) return HTRIGHT;
			if (top) return HTTOP;
			if (bottom) return HTBOTTOM;

			// 内側はドラッグ移動（操作モード中はモデル操作よりウィンドウ操作を優先）
			return HTCAPTION;
		}
		case WM_MOUSEACTIVATE:
			return IsWindowManipulationMode(hWnd) ? MA_ACTIVATE : MA_NOACTIVATE;

		case WM_NCCALCSIZE:
			// 枠は自前オーバーレイで表現するので、非クライアントを消す（モダンな見た目）
			if (IsWindowManipulationMode(hWnd))
			{
				return 0;
			}
			break;

		case WM_NCPAINT:
		case WM_NCACTIVATE:
			if (IsWindowManipulationMode(hWnd))
			{
				return 0;
			}
			break;
		default:
			break;
	}

	if (self && self->m_prevRenderWndProc)
	{
		return CallWindowProcW(self->m_prevRenderWndProc, hWnd, msg, wParam, lParam);
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void App::InitAnimator()
{
	m_animator = std::make_unique<MmdAnimator>();
	LoadModelFromSettings();
}

void App::InitTray()
{
	m_tray = std::make_unique<TrayIcon>(m_msgWnd, 1);
	m_tray->Show(L"MMDDesk");
	m_tray->SetContextMenu(m_trayMenu);
}

void App::BuildTrayMenu()
{
	if (m_trayMenu)
	{
		DestroyMenu(m_trayMenu);
	}
	m_trayMenu = CreatePopupMenu();

	AppendMenuW(m_trayMenu, MF_STRING, CMD_OPEN_SETTINGS, L"設定...");

	UINT manipFlags = MF_STRING;
	manipFlags |= IsWindowManipulationMode(m_renderWnd) ? MF_CHECKED : MF_UNCHECKED;
	AppendMenuW(m_trayMenu, manipFlags, CMD_TOGGLE_WINDOW_MANIP, L"ウィンドウ操作モード (Ctrl+Alt+R)");

	AppendMenuW(m_trayMenu, MF_SEPARATOR, 0, nullptr);

	RefreshMotionList();

	HMENU motionMenu = CreatePopupMenu();

	std::wstring pauseText = (m_animator && m_animator->IsPaused()) ? L"再開" : L"一時停止";
	AppendMenuW(motionMenu, MF_STRING, CMD_TOGGLE_PAUSE, pauseText.c_str());

	std::wstring physText = (m_animator && m_animator->PhysicsEnabled()) ? L"物理: ON" : L"物理: OFF";
	AppendMenuW(motionMenu, MF_STRING, CMD_TOGGLE_PHYSICS, physText.c_str());

	// 追加: LookAt メニュー
	UINT lookAtFlags = MF_STRING;
	lookAtFlags |= m_lookAtEnabled ? MF_CHECKED : MF_UNCHECKED;
	AppendMenuW(motionMenu, lookAtFlags, CMD_TOGGLE_LOOKAT, L"視線追従 (LookAt)");

	AppendMenuW(motionMenu, MF_STRING, CMD_STOP_MOTION, L"停止 (リセット)");
	AppendMenuW(motionMenu, MF_SEPARATOR, 0, nullptr);

	for (size_t i = 0; i < m_motionFiles.size(); ++i)
	{
		const auto& path = m_motionFiles[i];
		std::wstring name = path.stem().wstring();
		AppendMenuW(motionMenu, MF_STRING, CMD_MOTION_BASE + static_cast<UINT>(i), name.c_str());
	}

	AppendMenuW(m_trayMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(motionMenu), L"モーション");
	AppendMenuW(m_trayMenu, MF_STRING, CMD_RELOAD_MOTIONS, L"モーション一覧を更新");

	AppendMenuW(m_trayMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m_trayMenu, MF_STRING, CMD_EXIT, L"終了");

	if (m_tray)
	{
		m_tray->SetContextMenu(m_trayMenu);
	}
}

void App::RefreshMotionList()
{
	m_motionFiles.clear();

	if (!std::filesystem::exists(m_motionsDir))
	{
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(m_motionsDir))
	{
		if (!entry.is_regular_file()) continue;

		auto ext = entry.path().extension().wstring();
		for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

		if (ext == L".vmd")
		{
			m_motionFiles.push_back(entry.path());
		}
	}

	std::sort(m_motionFiles.begin(), m_motionFiles.end());
}

void App::ApplyTopmost() const
{
	HWND insertAfter = m_settingsData.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
	SetWindowPos(m_renderWnd, insertAfter, 0, 0, 0, 0,
				 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	if (m_gizmoWnd)
	{
		SetWindowPos(m_gizmoWnd, insertAfter, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

UINT App::ComputeTimerIntervalMs() const
{
	if (m_settingsData.unlimitedFps)
	{
		return 1;
	}

	if (m_settingsData.targetFps <= 0)
	{
		return kDefaultTimerMs;
	}

	const int clampedFps = std::max(1, m_settingsData.targetFps);
	const int interval = static_cast<int>(1000.0 / static_cast<double>(clampedFps) + 0.5);
	return static_cast<UINT>(std::max(1, interval));
}

void App::UpdateTimerInterval()
{
	m_timerIntervalMs = ComputeTimerIntervalMs();
	SetTimer(m_msgWnd, kTimerId, m_timerIntervalMs, nullptr);
}

void App::ApplySettings(const AppSettings& settings, bool persist)
{
	const bool modelChanged = (m_settingsData.modelPath != settings.modelPath);
	const bool topmostChanged = (m_settingsData.alwaysOnTop != settings.alwaysOnTop);
	const bool fpsChanged =
		(m_settingsData.targetFps != settings.targetFps) ||
		(m_settingsData.unlimitedFps != settings.unlimitedFps);

	m_settingsData = settings;

	if (modelChanged)
	{
		LoadModelFromSettings();
	}

	if (topmostChanged)
	{
		ApplyTopmost();
	}

	if (fpsChanged)
	{
		UpdateTimerInterval();
	}

	// ライト設定を適用
	if (m_renderer)
	{
		m_renderer->SetLightSettings(m_settingsData.light);
	}

	if (persist)
	{
		SettingsManager::Save(m_baseDir, m_settingsData);
	}
}

void App::ApplyLightSettings()
{
	if (m_renderer)
	{
		m_renderer->SetLightSettings(m_settingsData.light);
	}
}

void App::SaveSettings() const
{
	SettingsManager::Save(m_baseDir, m_settingsData);
}

void App::OnTrayCommand(UINT id)
{
	switch (id)
	{
		case CMD_OPEN_SETTINGS:
			if (!m_settings)
			{
				m_settings = std::make_unique<SettingsWindow>(*this, m_hInst);
			}
			m_settings->Show();
			break;

		case CMD_RELOAD_MOTIONS:
			BuildTrayMenu();
			break;

		case CMD_STOP_MOTION:
			if (m_animator)
			{
				m_animator->StopMotion();
			}
			break;

		case CMD_TOGGLE_PAUSE:
			if (m_animator)
			{
				m_animator->TogglePause();
				BuildTrayMenu();
			}
			break;

		case CMD_TOGGLE_PHYSICS:
			if (m_animator)
			{
				m_animator->TogglePhysics();
				BuildTrayMenu();
			}
			break;

		case CMD_TOGGLE_LOOKAT:
			m_lookAtEnabled = !m_lookAtEnabled;
			// 無効化時はリセットする
			if (m_animator)
			{
				m_animator->SetLookAtState(m_lookAtEnabled, 0.0f, 0.0f);
			}
			BuildTrayMenu();
			break;

		case CMD_TOGGLE_WINDOW_MANIP:
			ApplyWindowManipulationMode(m_renderWnd, m_renderer.get(), !IsWindowManipulationMode(m_renderWnd));
			BuildTrayMenu();
			break;

		case CMD_EXIT:
			PostQuitMessage(0);
			break;

		default:
			if (id >= CMD_MOTION_BASE)
			{
				size_t idx = id - CMD_MOTION_BASE;
				if (idx < m_motionFiles.size() && m_animator)
				{
					m_animator->LoadMotion(m_motionFiles[idx]);
				}
			}
			break;
	}
}

void App::OnMouseWheel(HWND hWnd, int delta, WPARAM)
{
	if (hWnd != m_gizmoWnd) return;

	bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

	if (ctrl && shift)
	{
		// Ctrl + Shift + ホイール: スケール調整
		float adjustment = (delta > 0) ? 0.1f : -0.1f;
		if (m_renderer)
		{
			m_renderer->AdjustScale(adjustment);
			m_settingsData.light = m_renderer->GetLightSettings();
		}
	}
	else if (ctrl)
	{
		// Ctrl + ホイール: 明るさ調整
		float adjustment = (delta > 0) ? 0.1f : -0.1f;
		if (m_renderer)
		{
			m_renderer->AdjustBrightness(adjustment);
			m_settingsData.light = m_renderer->GetLightSettings();
		}
	}
	else
	{
		// 通常ホイール: 回転（ヨー）
		const float steps = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
		const float pseudoDx = steps * 12.0f; // AddCameraRotation は「ピクセル差分」相当で受け取る
		if (m_renderer)
		{
			m_renderer->AddCameraRotation(pseudoDx, 0.0f);
		}
	}
}

void App::OnLoadComplete(WPARAM, LPARAM lParam)
{
	PmxModel* rawPtr = reinterpret_cast<PmxModel*>(lParam);

	if (rawPtr)
	{
		std::unique_ptr<PmxModel> model(rawPtr);
		// アニメーターにセット
		if (m_animator)
		{
			m_animator->SetModel(std::move(model));
			m_animator->Update();
		}
	}
	else
	{
		MessageBoxW(m_renderWnd, L"モデルの読み込みに失敗しました。", L"エラー", MB_ICONERROR);
	}

	if (m_progress)
	{
		m_progress->Hide();
	}
	m_isLoading = false; // 描画再開

	// 描画ウィンドウを強制更新
	InvalidateRect(m_renderWnd, nullptr, FALSE);
}

LRESULT CALLBACK App::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	App* self = nullptr;

	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<App*>(cs->lpCreateParams);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}

	if (self)
	{
		return self->WndProc(hWnd, msg, wParam, lParam);
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT App::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// トレイアイコンのメッセージ処理
	if (m_tray && msg == m_tray->CallbackMessage())
	{
		if (LOWORD(lParam) == WM_RBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			TrackPopupMenu(m_trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
			PostMessageW(hWnd, WM_NULL, 0, 0);
		}
		return 0;
	}

	if (msg == WM_APP_LOAD_COMPLETE)
	{
		OnLoadComplete(wParam, lParam);
		return 0;
	}

	switch (msg)
	{
		case WM_COMMAND:
			OnTrayCommand(LOWORD(wParam));
			return 0;

		case WM_HOTKEY:
			if (wParam == kHotKeyToggleGizmoId)
			{
				ToggleGizmoWindow();
				return 0;
			}
			if (wParam == kHotKeyTogglePhysicsId)
			{
				if (m_animator)
				{
					m_animator->TogglePhysics();
					BuildTrayMenu();
				}
				return 0;
			}
			if (wParam == kHotKeyToggleWindowManipId)
			{
				ApplyWindowManipulationMode(m_renderWnd, m_renderer.get(), !IsWindowManipulationMode(m_renderWnd));
				BuildTrayMenu();
				return 0;
			}
			break;

		case WM_NCHITTEST:
			break;

		case WM_LBUTTONDOWN:
			if (hWnd == m_gizmoWnd)
			{
				m_gizmoLeftDrag = true;
				m_gizmoRightDrag = false;
				SetCapture(hWnd);
				GetCursorPos(&m_gizmoLastCursor);
				return 0;
			}
			break;

		case WM_RBUTTONDOWN:
			if (hWnd == m_gizmoWnd)
			{
				m_gizmoRightDrag = true;
				m_gizmoLeftDrag = false;
				SetCapture(hWnd);
				GetCursorPos(&m_gizmoLastCursor);
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
		{
			if (hWnd == m_gizmoWnd && (m_gizmoLeftDrag || m_gizmoRightDrag))
			{
				POINT cursorNow{};
				GetCursorPos(&cursorNow);
				const int dx = cursorNow.x - m_gizmoLastCursor.x;
				const int dy = cursorNow.y - m_gizmoLastCursor.y;
				m_gizmoLastCursor = cursorNow;

				if (m_renderer)
				{
					if (m_gizmoLeftDrag)
					{
						if (m_renderWnd)
						{
							RECT rc{};
							GetWindowRect(m_renderWnd, &rc);
							const int newX = rc.left + dx;
							const int newY = rc.top + dy;
							SetWindowPos(m_renderWnd, nullptr, newX, newY,
										 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

							if (m_gizmoVisible && m_gizmoWnd)
							{
								PositionGizmoWindow();
							}
						}
					}
					else if (m_gizmoRightDrag)
					{
						// 右ドラッグ: 回転
						m_renderer->AddCameraRotation((float)dx, (float)dy);
					}
				}

				// RenderGizmo内でUpdateLayeredWindowを呼ぶので、InvalidateRectでWM_PAINTを飛ばさなくても
				// 直接RenderGizmoを呼べば更新できますが、一貫性のためWM_PAINT経由か
				// 負荷を考えてRenderGizmoを直接呼ぶことも可能です。
				// ここではシンプルに再描画要求を出します。
				RenderGizmo();
				return 0;
			}
			break;
		}

		case WM_RBUTTONUP:
			if (hWnd == m_gizmoWnd && m_gizmoRightDrag)
			{
				m_gizmoRightDrag = false;
				ReleaseCapture();
				return 0;
			}
			break;

		case WM_LBUTTONUP:
			if (hWnd == m_gizmoWnd && m_gizmoLeftDrag)
			{
				m_gizmoLeftDrag = false;
				ReleaseCapture();
				return 0;
			}
			break;

		case WM_CAPTURECHANGED:
			if (hWnd == m_gizmoWnd)
			{
				m_gizmoLeftDrag = false;
				m_gizmoRightDrag = false;
				return 0;
			}
			break;

		case WM_ERASEBKGND:
			if (hWnd == m_gizmoWnd)
			{
				return 1;
			}
			break;

		case WM_PAINT:
			if (hWnd == m_gizmoWnd)
			{
				PAINTSTRUCT ps{};
				BeginPaint(hWnd, &ps);
				RenderGizmo();
				EndPaint(hWnd, &ps);
				return 0;
			}
			break;

		case WM_MOUSEWHEEL:
			OnMouseWheel(hWnd, GET_WHEEL_DELTA_WPARAM(wParam), wParam);
			return 0;

		case WM_TIMER:
			if (wParam == kTimerId) OnTimer();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_CANCELMODE:
		case WM_KILLFOCUS:
		case WM_ACTIVATEAPP:
			if (hWnd == m_gizmoWnd)
			{
				m_gizmoLeftDrag = false;
				m_gizmoRightDrag = false;
				ReleaseCapture();
			}
			break;

		default:
			break;
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}