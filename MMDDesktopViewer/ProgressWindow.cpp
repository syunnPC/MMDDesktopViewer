#include "ProgressWindow.hpp"
#include <dwmapi.h>
#include <uxtheme.h> // テーマAPI

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kClassName[] = L"MMDDesk.ProgressWindow";
	constexpr int ID_PROGRESS = 1001;

	constexpr COLORREF kDarkBkColor = RGB(32, 32, 32);      // 背景色（濃いグレー）
	constexpr COLORREF kTextColor = RGB(240, 240, 240);   // 文字色（白）
}

ProgressWindow::ProgressWindow(HINSTANCE hInst, HWND parent)
	: m_hInst(hInst), m_parent(parent)
{

	m_darkBrush = CreateSolidBrush(kDarkBkColor);

	m_hFont = CreateFontW(
		-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kClassName;
	wc.hbrBackground = m_darkBrush;
	wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
	RegisterClassExW(&wc);
}

ProgressWindow::~ProgressWindow()
{
	Hide();
	if (m_darkBrush) DeleteObject(m_darkBrush);
	if (m_hFont) DeleteObject(m_hFont);
}

void ProgressWindow::SetModernFont(HWND hChild)
{
	if (m_hFont && hChild)
	{
		SendMessageW(hChild, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
	}
}

void ProgressWindow::SetDarkTheme(HWND hChild)
{
	if (hChild)
	{
		SetWindowTheme(hChild, L"DarkMode_Explorer", nullptr);
	}
}

void ProgressWindow::Show()
{
	if (m_hwnd) return;

	RECT rcParent;
	GetWindowRect(m_parent, &rcParent);
	int w = 400;
	int h = 120;
	int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
	int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

	m_hwnd = CreateWindowExW(
		WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
		kClassName, L"読み込み中...",
		WS_POPUP | WS_CAPTION | WS_BORDER,
		x, y, w, h,
		m_parent, nullptr, m_hInst, this
	);

	BOOL useDarkMode = TRUE;
	DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

	RECT rcClient;
	GetClientRect(m_hwnd, &rcClient);
	int clientW = rcClient.right - rcClient.left;
	int clientH = rcClient.bottom - rcClient.top;

	// コントロールのサイズ定義
	int barW = 360;  // プログレスバーの幅
	int barH = 20;   // プログレスバーの高さ
	int labelW = 380; // ラベルの幅
	int labelH = 20;  // ラベルの高さ

	int labelX = (clientW - labelW) / 2;
	int barX = (clientW - barW) / 2;

	// ラベル
	m_statusLabel = CreateWindowExW(0, L"STATIC", L"準備中...",
									WS_CHILD | WS_VISIBLE | SS_CENTER,
									labelX, 20, labelW, labelH,
									m_hwnd, nullptr, m_hInst, nullptr);
	SetModernFont(m_statusLabel);

	// プログレスバー
	m_progressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
									WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
									barX, 50, barW, barH,
									m_hwnd, (HMENU)ID_PROGRESS, m_hInst, nullptr);

	SendMessage(m_progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
	EnableWindow(m_parent, FALSE);
}

void ProgressWindow::Hide()
{
	if (m_hwnd)
	{
		EnableWindow(m_parent, TRUE);
		SetForegroundWindow(m_parent);
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}
}

void ProgressWindow::SetProgress(float percentage)
{
	if (m_progressBar)
	{
		int pos = static_cast<int>(percentage * 100.0f);
		SendMessage(m_progressBar, PBM_SETPOS, pos, 0);
	}
}

void ProgressWindow::SetMessage(const std::wstring& msg)
{
	if (m_statusLabel)
	{
		SetWindowTextW(m_statusLabel, msg.c_str());
	}
}

LRESULT CALLBACK ProgressWindow::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ProgressWindow* self = nullptr;
	if (msg == WM_NCCREATE)
	{
		auto cs = (CREATESTRUCTW*)lParam;
		self = (ProgressWindow*)cs->lpCreateParams;
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
	}
	else
	{
		self = (ProgressWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	}
	if (self) return self->WndProc(hWnd, msg, wParam, lParam);
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ProgressWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_CTLCOLORSTATIC: {
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, kTextColor); // 文字色を白に
			SetBkMode(hdc, TRANSPARENT);   // 背景透明
			return (LRESULT)m_darkBrush;   // 背景色（濃いグレー）のブラシを返す
		}
		default:
			return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
}