#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "App.hpp"
#include "FileUtil.hpp"
#include "SettingsWindow.hpp"
#include "Settings.hpp"
#include "ProgressWindow.hpp"
#include "MediaAudioAnalyzer.hpp"
#include <algorithm>
#include <format>
#include <thread>
#include <stdexcept>
#include <string>
#include <cmath>
#include <shellapi.h>
#include <ShObjIdl_core.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

namespace
{
	constexpr UINT kDefaultTimerMs = 16;

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
		CMD_TOGGLE_AUTOBLINK = 107,
		CMD_TOGGLE_BREATH = 108,
		CMD_TOGGLE_MEDIA_REACTIVE = 109,
		CMD_MOTION_BASE = 1000
	};
}

App::App(HINSTANCE hInst)
	: m_hInst(hInst)
	, m_input(*this)
	, m_windowManager(
		hInst,
		m_input,
		m_settingsData,
		WindowManager::Callbacks{
			[this](const POINT& pt) { ShowTrayMenu(pt); },
			[this](UINT id) { OnTrayCommand(id); },
			[this]() { OnTimer(); },
			[this](WPARAM wParam, LPARAM lParam) { OnLoadComplete(wParam, lParam); },
			[this]() { SaveSettings(); }
		})
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

	SetCurrentProcessExplicitAppUserModelID(L"MMDDesk");

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

	m_windowManager.Initialize();
	m_input.SetWindows(m_windowManager.RenderWindow(), m_windowManager.GizmoWindow());
	m_input.RegisterHotkeys(m_windowManager.RenderWindow());

	m_windowManager.ApplyTopmost(m_settingsData.alwaysOnTop);

	InitRenderer();
	InitAnimator();
	m_mediaAudio = std::make_unique<MediaAudioAnalyzer>();
	m_mediaAudio->SetEnabled(m_settingsData.mediaReactiveEnabled);
	m_trayMenu = std::make_unique<TrayMenuWindow>(m_hInst, [this](UINT id) { OnTrayCommand(id); });
	m_trayMenu->SetTheme(TrayMenuThemeId::Light);

	BuildTrayMenu();
	InitTray();

	UpdateTimerInterval();
}

App::~App()
{
	SaveSettings();

	m_input.UnregisterHotkeys(m_windowManager.RenderWindow());

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
			config.hwndParent = m_windowManager.RenderWindow();
			config.hInstance = m_hInst;
			config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS_NO_ICON;
			config.pszWindowTitle = L"設定の読み込み";
			config.pszMainInstruction = L"このモデル用の設定プリセットが見つかりました。";
			config.pszContent = L"保存された表示・ライト・物理設定を適用しますか？";

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
			if (SettingsManager::LoadPreset(m_baseDir, path, m_settingsData.light, m_settingsData.physics))
			{
				ApplyLightSettings();
				if (m_animator)
				{
					m_animator->SetPhysicsSettings(m_settingsData.physics);
				}
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
		m_progress = std::make_unique<ProgressWindow>(m_hInst, m_windowManager.RenderWindow());
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
		PostMessage(m_windowManager.MessageWindow(), WindowManager::kLoadCompleteMessage, 0, reinterpret_cast<LPARAM>(loadedModelPtr));

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
		// 頭ボーンの行列を取得
		auto headM = m_animator->GetBoneGlobalMatrix(L"頭");

		using namespace DirectX;
		// ボーン行列から位置と上方向ベクトルを抽出
		XMVECTOR pos = XMVectorSet(headM._41, headM._42, headM._43, 1.0f);
		XMVECTOR up = XMVector3Normalize(XMVectorSet(headM._21, headM._22, headM._23, 0.0f));

		// スクリーン座標へ投影
		auto sPos = m_renderer->ProjectToScreen(XMFLOAT3(XMVectorGetX(pos), XMVectorGetY(pos), XMVectorGetZ(pos)));

		// 軸の方向をスクリーン上で計算するための基準点
		// ベクトルを長くして計算精度を確保
		float axisLen = 5.0f;
		XMVECTOR pUp = XMVectorAdd(pos, XMVectorScale(up, axisLen));
		auto sUpPos = m_renderer->ProjectToScreen(XMFLOAT3(XMVectorGetX(pUp), XMVectorGetY(pUp), XMVectorGetZ(pUp)));

		// スクリーン上での基底ベクトル
		// 顔の上方向 (v_u)
		float vx_u = sUpPos.x - sPos.x;
		float vy_u = sUpPos.y - sPos.y;

		// 正規化と右ベクトルの算出
		float lenU = std::sqrt(vx_u * vx_u + vy_u * vy_u);
		float vx_r = 1.0f, vy_r = 0.0f; // デフォルトは画面右

		if (lenU > 1e-4f)
		{
			vx_u /= lenU;
			vy_u /= lenU;

			// 顔の「右方向」は、画面上の「上方向」を時計回りに90度回転させて生成
			// これにより、顔が傾いていても直感的な操作感が維持されます
			vx_r = -vy_u;
			vy_r = vx_u;
		}

		// マウス位置との偏差
		POINT pt{};
		GetCursorPos(&pt);
		ScreenToClient(m_windowManager.RenderWindow(), &pt);

		float dx = (float)pt.x - sPos.x;
		float dy = (float)pt.y - sPos.y;

		float localX = dx * vx_r + dy * vy_r; // 右成分
		float localY = dx * vx_u + dy * vy_u; // 上成分

		// 距離係数を計算 (lenU は 5unit 分の長さ)
		// 3.0f 程度が標準的な画角感に近いです
		float dist = std::max(lenU * 3.0f, 150.0f);

		float targetYaw = -std::atan2(localX, dist);
		float targetPitch = std::atan2(localY, dist);

		// 現在の角度を取得
		bool currentEnabled = false;
		float currentYaw = 0.0f;
		float currentPitch = 0.0f;
		m_animator->GetLookAtState(currentEnabled, currentYaw, currentPitch);

		// スムージング係数 (0.0=動かない ～ 1.0=瞬時に追従)
		// 0.2 程度で適度な滑らかさになります
		const float alpha = 0.2f;

		// 線形補間 (Lerp) で目標角度へ近づける
		float nextYaw = currentYaw + (targetYaw - currentYaw) * alpha;
		float nextPitch = currentPitch + (targetPitch - currentPitch) * alpha;

		m_animator->SetLookAtState(true, nextYaw, nextPitch);
	}

	if (m_animator)
	{
		if (m_mediaAudio)
		{
			const auto st = m_mediaAudio->GetState();
			if (m_mediaAudio->ConsumeDrmWarning())
			{
				ShowNotification(L"音声を取得できません", L"DRM保護されたアプリからは音声を取得できません。別のアプリや再生方法をご利用ください。");
			}

#if _DEBUG

			static auto last = std::chrono::steady_clock::now();
			const auto now = std::chrono::steady_clock::now();
			if (now - last > std::chrono::seconds(1))
			{
				last = now;
				OutputDebugStringW(std::format(
					L"[Audio] active={} bpm={:.1f} beat={:.3f} mouth={:.3f}\r\n",
					st.active ? 1 : 0, st.bpm, st.beatStrength, st.mouthOpen).c_str());
			}
#endif

			m_animator->SetAudioReactiveState(st);
		}
		m_animator->Update();
	}

	if (m_renderer && m_animator)
	{
		m_renderer->Render(*m_animator);
	}

	if (m_windowManager.IsGizmoVisible() && m_windowManager.GizmoWindow())
	{
		m_windowManager.PositionGizmoWindow();
		InvalidateRect(m_windowManager.GizmoWindow(), nullptr, FALSE);
	}
}

void App::ToggleGizmoWindow()
{
	m_windowManager.ToggleGizmoWindow();
}

void App::TogglePhysics()
{
	if (m_animator)
	{
		m_animator->TogglePhysics();
		BuildTrayMenu();
	}
}

void App::ToggleWindowManipulation()
{
	m_windowManager.ToggleWindowManipulationMode();
	BuildTrayMenu();
}

void App::MoveRenderWindowBy(int dx, int dy)
{
	HWND renderWnd = m_windowManager.RenderWindow();
	if (!renderWnd) return;

	RECT rc{};
	GetWindowRect(renderWnd, &rc);
	const int newX = rc.left + dx;
	const int newY = rc.top + dy;
	SetWindowPos(renderWnd, nullptr, newX, newY, 0, 0,
				 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

	if (m_windowManager.IsGizmoVisible() && m_windowManager.GizmoWindow())
	{
		m_windowManager.PositionGizmoWindow();
	}
}

void App::AddCameraRotation(float dx, float dy)
{
	if (m_renderer)
	{
		m_renderer->AddCameraRotation(dx, dy);
	}
}

void App::AdjustScale(float delta)
{
	if (m_renderer)
	{
		m_renderer->AdjustScale(delta);
		m_settingsData.light = m_renderer->GetLightSettings();
		SaveSettings();
	}
}

void App::AdjustBrightness(float delta)
{
	if (m_renderer)
	{
		m_renderer->AdjustBrightness(delta);
		m_settingsData.light = m_renderer->GetLightSettings();
		SaveSettings();
	}
}

void App::RenderGizmo()
{
	m_windowManager.RenderGizmo();
}

void App::ShowNotification(const std::wstring& title, const std::wstring& message) const
{
	if (m_tray)
	{
		m_tray->ShowBalloon(title.c_str(), message.c_str(), NIIF_WARNING);
		return;
	}

	HWND owner = m_windowManager.RenderWindow();
	MessageBoxW(owner ? owner : nullptr, message.c_str(), title.c_str(), MB_ICONWARNING | MB_OK);
}

void App::InitRenderer()
{
	if (!m_progress)
	{
		m_progress = std::make_unique<ProgressWindow>(m_hInst, m_windowManager.RenderWindow());
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
	m_renderer->Initialize(m_windowManager.RenderWindow(), onProgress);
	m_windowManager.SetRenderer(m_renderer.get());
	m_windowManager.InstallRenderClickThrough();
	m_windowManager.ForceRenderTreeClickThrough();

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
	m_animator->SetPhysicsSettings(m_settingsData.physics);
	m_animator->SetAudioReactiveEnabled(m_settingsData.mediaReactiveEnabled);
	LoadModelFromSettings();
}

void App::InitTray()
{
	m_tray = std::make_unique<TrayIcon>(m_windowManager.MessageWindow(), 1);
	m_tray->Show(L"MMDDesk");
	m_windowManager.SetTray(m_tray.get());
}

void App::BuildTrayMenu()
{
	if (!m_trayMenu) return;

	RefreshMotionList();

	TrayMenuModel model{};
	model.title = L"MMD Desktop Viewer";
	if (!m_settingsData.modelPath.empty())
	{
		model.subtitle = m_settingsData.modelPath.filename().wstring();
	}
	else
	{
		model.subtitle = L"モデル未読み込み";
	}

	const bool paused = (m_animator && m_animator->IsPaused());
	const bool physicsEnabled = (m_animator && m_animator->PhysicsEnabled());
	const bool autoBlink = (m_animator && m_animator->AutoBlinkEnabled());
	const bool breathing = (m_animator && m_animator->BreathingEnabled());

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Action, CMD_OPEN_SETTINGS, L"設定", L"描画・ライティング・プリセットを編集"
						  });
	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle, CMD_TOGGLE_WINDOW_MANIP, L"ウィンドウ操作モード", L"Ctrl+Alt+R で切り替え", m_windowManager.IsWindowManipulationMode()
						  });

	model.items.push_back(TrayMenuItem{ TrayMenuItem::Kind::Separator, 0, L"", L"" });
	model.items.push_back(TrayMenuItem{ TrayMenuItem::Kind::Header, 0, L"再生コントロール", L"" });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_PAUSE,
		paused ? L"再生を再開" : L"一時停止",
		L"モーションを一時停止 / 再開",
		paused
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_PHYSICS,
		L"物理シミュレーション",
		physicsEnabled ? L"有効" : L"無効",
		physicsEnabled
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_LOOKAT,
		L"視線追従",
		L"視線を注視点へ向けます",
		m_lookAtEnabled
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_AUTOBLINK,
		L"自動まばたき",
		L"自然なまばたきを付与",
		autoBlink
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_BREATH,
		L"呼吸モーション (待機時)",
		L"待機中の呼吸モーションを制御",
		breathing
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Toggle,
		CMD_TOGGLE_MEDIA_REACTIVE,
		L"メディア連動 (SMTC/WASAPI)",
		L"音楽のビートに合わせて動作",
		m_settingsData.mediaReactiveEnabled
						  });

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Action,
		CMD_STOP_MOTION,
		L"停止 (リセット)",
		L"再生を止めてポーズをリセット",
		false,
		true
						  });

	model.items.push_back(TrayMenuItem{ TrayMenuItem::Kind::Separator, 0, L"", L"" });
	model.items.push_back(TrayMenuItem{ TrayMenuItem::Kind::Header, 0, L"モーション", L"" });

	if (m_motionFiles.empty())
	{
		model.items.push_back(TrayMenuItem{
			TrayMenuItem::Kind::Action,
			0,
			L"モーションファイルが見つかりません",
			L"\"Motions\" フォルダーに .vmd を追加してください"
							  });
	}
	else
	{
		for (size_t i = 0; i < m_motionFiles.size(); ++i)
		{
			const auto& path = m_motionFiles[i];
			std::wstring name = path.stem().wstring();
			model.items.push_back(TrayMenuItem{
				TrayMenuItem::Kind::Action,
				CMD_MOTION_BASE + static_cast<UINT>(i),
				name,
				L"クリックして再生を開始"
								  });
		}
	}

	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Action,
		CMD_RELOAD_MOTIONS,
		L"モーション一覧を更新",
		L"フォルダーを再スキャンします"
						  });

	model.items.push_back(TrayMenuItem{ TrayMenuItem::Kind::Separator, 0, L"", L"" });
	model.items.push_back(TrayMenuItem{
		TrayMenuItem::Kind::Action,
		CMD_EXIT,
		L"終了",
		L"アプリケーションを終了します",
		false,
		true
						  });

	m_trayMenu->SetModel(model);
}

void App::ShowTrayMenu(const POINT& anchor)
{
	if (!m_trayMenu) return;
	BuildTrayMenu();
	m_trayMenu->ShowAt(anchor);
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
	m_windowManager.UpdateTimerInterval(m_timerIntervalMs);
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
		m_windowManager.ApplyTopmost(m_settingsData.alwaysOnTop);
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

	if (m_animator)
	{
		m_animator->SetPhysicsSettings(m_settingsData.physics);
	}

	if (m_mediaAudio)
	{
		m_mediaAudio->SetEnabled(m_settingsData.mediaReactiveEnabled);
	}
	if (m_animator)
	{
		m_animator->SetAudioReactiveEnabled(m_settingsData.mediaReactiveEnabled);
	}

	if (persist)
	{
		if (!m_settingsData.modelPath.empty())
		{
			bool shouldSavePreset = SettingsManager::HasPreset(m_baseDir, m_settingsData.modelPath);
			const std::wstring filename = m_settingsData.modelPath.filename().wstring();
			auto it = m_settingsData.perModelPresetSettings.find(filename);
			const PresetMode mode = (it != m_settingsData.perModelPresetSettings.end())
				? it->second
				: m_settingsData.globalPresetMode;
			if (mode == PresetMode::AlwaysLoad)
			{
				shouldSavePreset = true;
			}

			if (shouldSavePreset)
			{
				SettingsManager::SavePreset(m_baseDir, m_settingsData.modelPath, m_settingsData.light, m_settingsData.physics);
			}
		}
	}
}

void App::ApplyLightSettings()
{
	if (m_renderer)
	{
		m_renderer->SetLightSettings(m_settingsData.light);
	}
}

void App::ApplyPhysicsSettings()
{
	if (m_animator)
	{
		m_animator->SetPhysicsSettings(m_settingsData.physics);
	}
}

void App::SaveSettings()
{
	m_windowManager.UpdateSettingsForRenderSize();

	if (m_renderer)
	{
		m_settingsData.light = m_renderer->GetLightSettings();
	}

	SettingsManager::Save(m_baseDir, m_settingsData);

	if (!m_settingsData.modelPath.empty())
	{
		bool shouldSavePreset = SettingsManager::HasPreset(m_baseDir, m_settingsData.modelPath);
		const std::wstring filename = m_settingsData.modelPath.filename().wstring();
		auto it = m_settingsData.perModelPresetSettings.find(filename);
		const PresetMode mode = (it != m_settingsData.perModelPresetSettings.end())
			? it->second
			: m_settingsData.globalPresetMode;
		if (mode == PresetMode::AlwaysLoad)
		{
			shouldSavePreset = true;
		}

		if (shouldSavePreset)
		{
			SettingsManager::SavePreset(m_baseDir, m_settingsData.modelPath, m_settingsData.light, m_settingsData.physics);
		}
	}
}

void App::OnTrayCommand(UINT id)
{
	if (m_trayMenu)
	{
		m_trayMenu->Hide();
	}

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

		case CMD_TOGGLE_AUTOBLINK:
			if (m_animator)
			{
				bool current = m_animator->AutoBlinkEnabled();
				m_animator->SetAutoBlinkEnabled(!current);
				BuildTrayMenu();
			}
			break;

		case CMD_TOGGLE_BREATH:
			if (m_animator)
			{
				bool current = m_animator->BreathingEnabled();
				m_animator->SetBreathingEnabled(!current);
				BuildTrayMenu();
			}
			break;

		case CMD_TOGGLE_MEDIA_REACTIVE:
			m_settingsData.mediaReactiveEnabled = !m_settingsData.mediaReactiveEnabled;
			if (m_mediaAudio)
			{
				m_mediaAudio->SetEnabled(m_settingsData.mediaReactiveEnabled);
			}
			if (m_animator)
			{
				m_animator->SetAudioReactiveEnabled(m_settingsData.mediaReactiveEnabled);
			}
			BuildTrayMenu();
			break;

		case CMD_TOGGLE_WINDOW_MANIP:
			m_windowManager.ToggleWindowManipulationMode();
			BuildTrayMenu();
			break;

		case CMD_EXIT:
			if (m_windowManager.RenderWindow())
			{
				PostMessageW(m_windowManager.RenderWindow(), WM_CLOSE, 0, 0);
			}
			else
			{
				PostQuitMessage(0);
			}
			break;

		default:
			if (id >= CMD_MOTION_BASE)
			{
				size_t idx = id - CMD_MOTION_BASE;
				if (idx < m_motionFiles.size() && m_animator)
				{
					m_animator->LoadMotion(m_motionFiles[idx]);
					BuildTrayMenu();
				}
			}
			break;
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
		MessageBoxW(m_windowManager.RenderWindow(), L"モデルの読み込みに失敗しました。", L"エラー", MB_ICONERROR);
	}

	if (m_progress)
	{
		m_progress->Hide();
	}
	m_isLoading = false; // 描画再開

	InvalidateRect(m_windowManager.RenderWindow(), nullptr, FALSE);
}