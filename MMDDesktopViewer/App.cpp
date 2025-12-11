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

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kMsgClassName[] = L"MMDDesk.MsgWindow";
	constexpr wchar_t kRenderClassName[] = L"MMDDesk.RenderWindow";

	constexpr UINT_PTR kTimerId = 1;
	constexpr UINT kDefaultTimerMs = 16;

	enum TrayCmd : UINT
	{
		CMD_OPEN_SETTINGS = 100,
		CMD_RELOAD_MOTIONS = 101,
		CMD_STOP_MOTION = 102,
		CMD_TOGGLE_PAUSE = 103,
		CMD_EXIT = 199,
		CMD_MOTION_BASE = 1000
	};

	DWORD GetWindowStyleExForRender()
	{
		return WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
	}

	DWORD GetWindowStyleForRender()
	{
		return WS_POPUP;
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

	if (m_animator)
	{
		m_animator->Update();
	}

	if (m_renderer && m_animator)
	{
		m_renderer->Render(*m_animator);
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

	const int w = 400;
	const int h = 600;
	const int x = GetSystemMetrics(SM_CXSCREEN) - w - 50;
	const int y = GetSystemMetrics(SM_CYSCREEN) - h - 100;

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

	// 保存されたライト設定を適用
	m_renderer->SetLightSettings(m_settingsData.light);

	if (m_progress)
	{
		m_progress->Hide();
	}
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
	AppendMenuW(m_trayMenu, MF_SEPARATOR, 0, nullptr);

	RefreshMotionList();

	HMENU motionMenu = CreatePopupMenu();

	std::wstring pauseText = (m_animator && m_animator->IsPaused()) ? L"再開" : L"一時停止";
	AppendMenuW(motionMenu, MF_STRING, CMD_TOGGLE_PAUSE, pauseText.c_str());

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
	if (hWnd != m_renderWnd) return;

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

		case WM_NCHITTEST:
			if (hWnd == m_renderWnd && m_renderer)
			{
				// マウス座標（スクリーン座標）を取得
				POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

				// クライアント座標に変換して判定
				POINT ptClient = pt;
				ScreenToClient(hWnd, &ptClient);

				// モデル上なら「クライアント領域」として自分のイベントにする
				if (m_renderer->IsPointOnModel(ptClient))
				{
					return HTCLIENT;
				}

				// 透明部分なら「透過」として下のウィンドウへイベントを流す
				return HTTRANSPARENT;
			}
			break;

		case WM_LBUTTONDOWN:
			if (hWnd == m_renderWnd)
			{
				// ウィンドウ移動の開始
				m_draggingWindow = true;
				SetCapture(hWnd);
				GetCursorPos(&m_dragStartCursor);
				RECT rc; GetWindowRect(hWnd, &rc);
				m_dragStartWindowPos = { rc.left, rc.top };
				return 0;
			}
			break;

		case WM_RBUTTONDOWN:
			if (hWnd == m_renderWnd && m_renderer)
			{
				POINT pt; GetCursorPos(&pt);
				// モデル回転の開始
				m_rotatingModel = true;
				SetCapture(hWnd);
				m_rotateLastCursor = pt;
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
		{
			if (hWnd == m_renderWnd)
			{
				if (m_draggingWindow)
				{
					POINT cursorNow;
					GetCursorPos(&cursorNow);
					const int dx = cursorNow.x - m_dragStartCursor.x;
					const int dy = cursorNow.y - m_dragStartCursor.y;

					SetWindowPos(
						m_renderWnd, nullptr,
						m_dragStartWindowPos.x + dx,
						m_dragStartWindowPos.y + dy,
						0, 0,
						SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
					);
					return 0;
				}
				else if (m_rotatingModel)
				{
					POINT cursorNow;
					GetCursorPos(&cursorNow);
					const int dx = cursorNow.x - m_rotateLastCursor.x;
					const int dy = cursorNow.y - m_rotateLastCursor.y;
					if (m_renderer)
					{
						m_renderer->AddCameraRotation((float)dx, (float)dy);
					}
					m_rotateLastCursor = cursorNow;
					return 0;
				}
			}
			break;
		}

		case WM_RBUTTONUP:
		case WM_CAPTURECHANGED:
			if (hWnd == m_renderWnd && m_rotatingModel)
			{
				m_rotatingModel = false;
				ReleaseCapture();
				return 0;
			}
			if (hWnd == m_renderWnd && m_draggingWindow)
			{
				m_draggingWindow = false;
				ReleaseCapture();
				return 0;
			}
			break;

		case WM_LBUTTONUP:
			if (hWnd == m_renderWnd && m_draggingWindow)
			{
				m_draggingWindow = false;
				ReleaseCapture();
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
			if (hWnd == m_renderWnd)
			{
				m_draggingWindow = false;
				m_rotatingModel = false;
				ReleaseCapture();
			}
			break;

		default:
			break;
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}