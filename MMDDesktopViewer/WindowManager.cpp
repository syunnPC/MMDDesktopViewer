#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WindowManager.hpp"
#include "InputManager.hpp"
#include "DcompRenderer.hpp"
#include "Settings.hpp"
#include "TrayIcon.hpp"
#include <algorithm>
#include <format>
#include <stdexcept>
#include <windowsx.h>
#include <dwmapi.h>

#include <d2d1.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kMsgClassName[] = L"MMDDesk.MsgWindow";
	constexpr wchar_t kRenderClassName[] = L"MMDDesk.RenderWindow";
	constexpr wchar_t kGizmoClassName[] = L"MMDDesk.GizmoWindow";

	constexpr int kGizmoSizePx = 140;

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
}

WindowManager::WindowManager(HINSTANCE hInst, InputManager& input, AppSettings& settings, Callbacks callbacks)
	: m_hInst(hInst)
	, m_input(input)
	, m_settings(settings)
	, m_callbacks(callbacks)
{
}

WindowManager::~WindowManager()
{
	if (m_msgWnd)
	{
		KillTimer(m_msgWnd, kTimerId);
	}
	if (m_gizmoWnd)
	{
		DestroyWindow(m_gizmoWnd);
	}

	if (m_gizmoOldBmp && m_gizmoDc) SelectObject(m_gizmoDc, m_gizmoOldBmp);
	if (m_gizmoBmp) DeleteObject(m_gizmoBmp);
	if (m_gizmoDc) DeleteDC(m_gizmoDc);
}

void WindowManager::Initialize()
{
	CreateHiddenMessageWindow();
	CreateRenderWindow();
	CreateGizmoWindow();
}

void WindowManager::SetRenderer(DcompRenderer* renderer)
{
	m_renderer = renderer;
}

void WindowManager::SetTray(TrayIcon* tray)
{
	m_tray = tray;
}

void WindowManager::ApplyTopmost(bool alwaysOnTop) const
{
	if (!m_renderWnd) return;

	HWND insertAfter = alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
	SetWindowPos(m_renderWnd, insertAfter, 0, 0, 0, 0,
				 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	if (m_gizmoWnd)
	{
		SetWindowPos(m_gizmoWnd, insertAfter, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

void WindowManager::UpdateTimerInterval(UINT intervalMs)
{
	if (!m_msgWnd) return;
	KillTimer(m_msgWnd, kTimerId);
	(void)intervalMs;
}

void WindowManager::ToggleGizmoWindow()
{
	if (!m_gizmoWnd) return;

	if (m_gizmoVisible)
	{
		m_gizmoVisible = false;
		m_input.ResetGizmoDrag();
		ReleaseCapture();
		ShowWindow(m_gizmoWnd, SW_HIDE);
		return;
	}

	m_gizmoVisible = true;
	PositionGizmoWindow();
	ShowWindow(m_gizmoWnd, SW_SHOWNOACTIVATE);
	InvalidateRect(m_gizmoWnd, nullptr, FALSE);
}

void WindowManager::PositionGizmoWindow()
{
	if (!m_gizmoWnd || !m_renderWnd) return;

	RECT rc{};
	GetWindowRect(m_renderWnd, &rc);

	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;

	const int x = rc.left + (w - kGizmoSizePx) / 2;
	const int y = rc.top + (h - kGizmoSizePx) / 2;

	HWND insertAfter = m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
	SetWindowPos(
		m_gizmoWnd,
		insertAfter,
		x,
		y,
		kGizmoSizePx,
		kGizmoSizePx,
		SWP_NOACTIVATE);
}

void WindowManager::EnsureGizmoD2D()
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
		bmi.bmiHeader.biHeight = -kGizmoSizePx;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		m_gizmoBmp = CreateDIBSection(m_gizmoDc, &bmi, DIB_RGB_COLORS, &m_gizmoBits, nullptr, 0);
		if (m_gizmoBmp)
		{
			m_gizmoOldBmp = SelectObject(m_gizmoDc, m_gizmoBmp);
		}
	}

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

		m_gizmoRt->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.6f), m_gizmoBrushFill.GetAddressOf());
		m_gizmoRt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f, 0.9f), m_gizmoBrushStroke.GetAddressOf());
	}
}

void WindowManager::DiscardGizmoD2D()
{
	m_gizmoRt.Reset();
	m_gizmoBrushFill.Reset();
	m_gizmoBrushStroke.Reset();
}

void WindowManager::RenderGizmo()
{
	if (!m_gizmoVisible || !m_gizmoWnd) return;
	EnsureGizmoD2D();
	if (!m_gizmoRt || !m_gizmoDc) return;

	const float width = static_cast<float>(kGizmoSizePx);
	const float height = static_cast<float>(kGizmoSizePx);
	const float cx = width * 0.5f;
	const float cy = height * 0.5f;
	const float radius = (std::min)(width, height) * 0.5f - 2.0f;

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
	m_gizmoRt->DrawEllipse(el, m_gizmoBrushStroke.Get(), 2.0f);

	const float tick = radius * 0.55f;
	m_gizmoRt->DrawLine(D2D1::Point2F(cx - tick, cy), D2D1::Point2F(cx + tick, cy), m_gizmoBrushStroke.Get(), 1.5f);
	m_gizmoRt->DrawLine(D2D1::Point2F(cx, cy - tick), D2D1::Point2F(cx, cy + tick), m_gizmoBrushStroke.Get(), 1.5f);

	hr = m_gizmoRt->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		if (hr != D2DERR_RECREATE_TARGET)
		{
			OutputDebugStringW(std::format(L"EndDraw hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
		}
		DiscardGizmoD2D();
		return;
	}

	BLENDFUNCTION bf = {};
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;

	POINT ptSrc = { 0, 0 };
	SIZE wndSize = { kGizmoSizePx, kGizmoSizePx };

	RECT wndRect;
	GetWindowRect(m_gizmoWnd, &wndRect);
	POINT ptDst = { wndRect.left, wndRect.top };

	UpdateLayeredWindow(m_gizmoWnd, nullptr, &ptDst, &wndSize, m_gizmoDc, &ptSrc, 0, &bf, ULW_ALPHA);
}

void WindowManager::InstallRenderClickThrough()
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
				reinterpret_cast<LONG_PTR>(
					reinterpret_cast<WNDPROC>(WindowManager::RenderClickThroughProc)
					)
			)
			);
	}
}

void WindowManager::MakeClickThrough(HWND hWnd)
{
	if (!hWnd) return;

	EnableWindow(hWnd, FALSE);

	LONG_PTR ex = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
	ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
	SetWindowLongPtrW(hWnd, GWL_EXSTYLE, ex);

	SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
				 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

BOOL CALLBACK WindowManager::EnumChildForClickThrough(HWND hWnd, LPARAM)
{
	MakeClickThrough(hWnd);
	return TRUE;
}

void WindowManager::ForceRenderTreeClickThroughFor(HWND hWnd)
{
	MakeClickThrough(hWnd);
	EnumChildWindows(hWnd, &WindowManager::EnumChildForClickThrough, 0);
}

void WindowManager::ForceRenderTreeClickThrough()
{
	if (!m_renderWnd) return;
	ForceRenderTreeClickThroughFor(m_renderWnd);
}

void WindowManager::ToggleWindowManipulationMode()
{
	ApplyWindowManipulationMode(!IsWindowManipulationMode());
}

bool WindowManager::IsWindowManipulationMode() const
{
	return ::IsWindowManipulationMode(m_renderWnd);
}

void WindowManager::ApplyWindowManipulationMode(bool enabled)
{
	if (!m_renderWnd) return;

	SetWindowManipulationModeProp(m_renderWnd, enabled);

	if (m_renderer)
	{
		m_renderer->SetResizeOverlayEnabled(enabled);
	}

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

	applyTo(m_renderWnd);
	EnumChildWindows(
		m_renderWnd,
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

	LONG_PTR style = GetWindowLongPtrW(m_renderWnd, GWL_STYLE);
	if (enabled)
	{
		style |= WS_THICKFRAME;
	}
	else
	{
		style &= ~WS_THICKFRAME;
	}
	SetWindowLongPtrW(m_renderWnd, GWL_STYLE, style);

	SetWindowPos(m_renderWnd, nullptr, 0, 0, 0, 0,
				 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void WindowManager::UpdateSettingsForRenderSize()
{
	if (!m_renderWnd || !IsWindow(m_renderWnd)) return;
	RECT rc{};
	if (GetClientRect(m_renderWnd, &rc))
	{
		const int cw = rc.right - rc.left;
		const int ch = rc.bottom - rc.top;
		if (cw > 0 && ch > 0)
		{
			m_settings.windowWidth = cw;
			m_settings.windowHeight = ch;
		}
	}
}

void WindowManager::CreateHiddenMessageWindow()
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

void WindowManager::CreateRenderWindow()
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

	if (m_settings.windowWidth > 0 && m_settings.windowHeight > 0)
	{
		w = m_settings.windowWidth;
		h = m_settings.windowHeight;
	}
	else
	{
#if !DCOMP_AUTOFIT_WINDOW
		w = std::clamp(screenW / 3, 480, 720);
		h = std::clamp((screenH * 2) / 3, 720, 1200);
#endif
	}

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

void WindowManager::CreateGizmoWindow()
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kGizmoClassName;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;

	if (!RegisterClassExW(&wc))
	{
		const DWORD err = GetLastError();
		if (err != ERROR_CLASS_ALREADY_EXISTS)
		{
			throw std::runtime_error("RegisterClassExW (GizmoWindow) failed.");
		}
	}

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

	ShowWindow(m_gizmoWnd, SW_HIDE);
}

LRESULT CALLBACK WindowManager::RenderClickThroughProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WindowManager* self = reinterpret_cast<WindowManager*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

	switch (msg)
	{
		case WM_NCHITTEST:
		{
			if (!::IsWindowManipulationMode(hWnd))
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

			return HTCAPTION;
		}
		case WM_MOUSEACTIVATE:
			return ::IsWindowManipulationMode(hWnd) ? MA_ACTIVATE : MA_NOACTIVATE;

		case WM_NCCALCSIZE:
			if (::IsWindowManipulationMode(hWnd))
			{
				return 0;
			}
			break;

		case WM_NCPAINT:
		case WM_NCACTIVATE:
			if (::IsWindowManipulationMode(hWnd))
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

LRESULT CALLBACK WindowManager::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WindowManager* self = nullptr;

	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<WindowManager*>(cs->lpCreateParams);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<WindowManager*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}

	if (self)
	{
		return self->WndProc(hWnd, msg, wParam, lParam);
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WindowManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (m_tray && msg == m_tray->CallbackMessage())
	{
		const UINT evRaw = static_cast<UINT>(lParam);
		const UINT ev = LOWORD(lParam);
		auto isEv = [&](UINT x) { return (ev == x) || (evRaw == x); };

		const bool requestMenu =
			isEv(WM_CONTEXTMENU) ||
			isEv(WM_RBUTTONUP) ||
			isEv(WM_RBUTTONDOWN) ||
			isEv(NIN_SELECT) ||
			isEv(NIN_KEYSELECT);

		if (requestMenu && m_callbacks.onTrayMenuRequested)
		{
			POINT pt{};
			GetCursorPos(&pt);
			m_callbacks.onTrayMenuRequested(pt);
		}
		return 0;
	}

	if (msg == kLoadCompleteMessage)
	{
		if (m_callbacks.onLoadComplete)
		{
			m_callbacks.onLoadComplete(wParam, lParam);
		}
		return 0;
	}

	switch (msg)
	{
		case WM_COMMAND:
			if (m_callbacks.onTrayCommand)
			{
				m_callbacks.onTrayCommand(LOWORD(wParam));
				return 0;
			}
			break;

		case WM_HOTKEY:
			if (m_input.HandleHotkey(wParam))
			{
				return 0;
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			if (m_input.HandleMouseDown(hWnd, msg))
			{
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
			if (m_input.HandleMouseMove(hWnd))
			{
				return 0;
			}
			break;

		case WM_RBUTTONUP:
		case WM_LBUTTONUP:
			if (m_input.HandleMouseUp(hWnd, msg))
			{
				return 0;
			}
			break;

		case WM_MOUSEWHEEL:
			if (m_input.HandleMouseWheel(hWnd, GET_WHEEL_DELTA_WPARAM(wParam), wParam))
			{
				return 0;
			}
			break;

		case WM_TIMER:
			if (wParam == kTimerId && m_callbacks.onTimer)
			{
				m_callbacks.onTimer();
				return 0;
			}
			break;

		case WM_SIZE:
			if (hWnd == m_renderWnd)
			{
				if (wParam != SIZE_MINIMIZED)
				{
					const int cw = LOWORD(lParam);
					const int ch = HIWORD(lParam);
					if (cw > 0 && ch > 0)
					{
						m_settings.windowWidth = cw;
						m_settings.windowHeight = ch;
					}
					if (m_gizmoVisible && m_gizmoWnd)
					{
						PositionGizmoWindow();
					}
				}
			}
			break;

		case WM_EXITSIZEMOVE:
			if (hWnd == m_renderWnd)
			{
				if (m_callbacks.onSaveSettings)
				{
					m_callbacks.onSaveSettings();
				}
				return 0;
			}
			break;

		case WM_CLOSE:
			if (hWnd == m_renderWnd)
			{
				if (m_gizmoWnd && IsWindow(m_gizmoWnd))
				{
					DestroyWindow(m_gizmoWnd);
					m_gizmoWnd = nullptr;
					m_gizmoVisible = false;
				}
				DestroyWindow(hWnd);
				return 0;
			}
			break;

		case WM_DESTROY:
			if (hWnd == m_renderWnd)
			{
				if (m_callbacks.onSaveSettings)
				{
					m_callbacks.onSaveSettings();
				}
				m_renderWnd = nullptr;
				PostQuitMessage(0);
				return 0;
			}
			return 0;

		case WM_CANCELMODE:
		case WM_KILLFOCUS:
		case WM_ACTIVATEAPP:
			if (hWnd == m_gizmoWnd)
			{
				m_input.CancelGizmoDrag(hWnd);
			}
			break;

		case WM_CAPTURECHANGED:
			if (m_input.HandleCaptureChanged(hWnd))
			{
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

		case WM_QUERYENDSESSION:
			return TRUE;

		case WM_ENDSESSION:
			if (wParam && hWnd == m_renderWnd)
			{
				if (m_callbacks.onSaveSettings)
				{
					m_callbacks.onSaveSettings();
				}
			}
			return 0;

		default:
			break;
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}