#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "TrayMenuWindow.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <stdexcept>
#include <uxtheme.h>
#include <windowsx.h>
#include <utility>
#include <string>
#include <format>
#include <set>
#include <unordered_map>

#pragma comment(lib, "dwmapi.lib")

namespace
{
	constexpr wchar_t kWindowClass[] = L"MMDDesk.TrayMenuWindow";

	COLORREF Rgb(BYTE r, BYTE g, BYTE b)
	{
		return RGB(r, g, b);
	}

	struct Theme
	{
		COLORREF background{ Rgb(20, 22, 27) };
		COLORREF headerBackground{ Rgb(28, 30, 36) };
		COLORREF cardHover{ Rgb(38, 43, 52) };
		COLORREF textPrimary{ Rgb(235, 238, 243) };
		COLORREF textMuted{ Rgb(165, 169, 179) };
		COLORREF accent{ Rgb(0, 120, 215) };
		COLORREF danger{ Rgb(203, 68, 80) };
		COLORREF outline{ Rgb(64, 68, 78) };
	};

	const Theme& GetTheme()
	{
		static Theme theme{};
		return theme;
	}

	int GetSystemDpi(HWND hWnd)
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32) return 96;

		using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
		using GetDpiForSystemFn = UINT(WINAPI*)();

		if (hWnd)
		{
			if (auto getDpiForWindow =
				reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")))
			{
				const UINT dpi = getDpiForWindow(hWnd);
				if (dpi != 0) return static_cast<int>(dpi);
			}
		}

		if (auto getDpiForSystem =
			reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem")))
		{
			const UINT dpi = getDpiForSystem();
			if (dpi != 0) return static_cast<int>(dpi);
		}

		HDC hdc = GetDC(nullptr);
		const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(nullptr, hdc);
		return (dpi > 0) ? dpi : 96;
	}
}

TrayMenuWindow::TrayMenuWindow(HINSTANCE hInst, std::function<void(UINT)> onCommand)
	: m_hInst(hInst)
	, m_onCommand(std::move(onCommand))
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = &TrayMenuWindow::WndProcThunk;
	wc.hInstance = m_hInst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = kWindowClass;
	RegisterClassExW(&wc);
}

TrayMenuWindow::~TrayMenuWindow()
{
	// 親ウィンドウとして振る舞っている場合、子ウィンドウを先に破棄
	m_subMenu.reset();

	if (m_bodyFont) DeleteObject(m_bodyFont);
	if (m_titleFont) DeleteObject(m_titleFont);
	if (m_headerFont) DeleteObject(m_headerFont);
	if (m_hWnd) DestroyWindow(m_hWnd);
}

void TrayMenuWindow::SetIsSubMenu(bool isSubMenu)
{
	m_isSubMenu = isSubMenu;
}

void TrayMenuWindow::SetModel(const TrayMenuModel& model)
{
	m_model = model;
	if (m_model.title.empty())
	{
		m_model.title = L"MMD Desktop Viewer";
	}

	// モデル変更時にレイアウト再構築
	RebuildLayout();
	if (m_hWnd && m_visible)
	{
		UpdateTopLevelPlacement();
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void TrayMenuWindow::EnsureWindow()
{
	if (m_hWnd) return;

	m_dpi = GetSystemDpi(GetDesktopWindow());
	if (m_dpi <= 0) m_dpi = 96;
	EnsureFonts();

	m_hWnd = CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kWindowClass,
		L"",
		WS_POPUP,
		0, 0,
		m_windowSize.cx,
		m_windowSize.cy,
		nullptr,
		nullptr,
		m_hInst,
		this);

	if (!m_hWnd)
	{
		OutputDebugStringW(L"TrayMenuWindow: CreateWindowExW failed.\r\n");
		return;
	}

	{
		const int newDpi = GetSystemDpi(m_hWnd);
		if (newDpi > 0 && newDpi != static_cast<int>(m_dpi))
		{
			m_dpi = static_cast<UINT>(newDpi);

			if (m_bodyFont) DeleteObject(m_bodyFont);
			if (m_titleFont) DeleteObject(m_titleFont);
			if (m_headerFont) DeleteObject(m_headerFont);
			m_bodyFont = m_titleFont = m_headerFont = nullptr;

			EnsureFonts();
		}
	}

	BOOL darkMode = TRUE;
	DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
}


void TrayMenuWindow::EnsureFonts()
{
	if (m_bodyFont && m_titleFont && m_headerFont) return;

	const int titleSize = -MulDiv(18, m_dpi, 72);
	const int bodySize = -MulDiv(14, m_dpi, 72);
	const int headerSize = -MulDiv(11, m_dpi, 72);

	if (!m_titleFont)
	{
		m_titleFont = CreateFontW(
			titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	}
	if (!m_bodyFont)
	{
		m_bodyFont = CreateFontW(
			bodySize, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	}
	if (!m_headerFont)
	{
		m_headerFont = CreateFontW(
			headerSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	}
}

void TrayMenuWindow::RebuildLayout()
{
	EnsureFonts();

	HDC hdc = GetDC(nullptr);
	HGDIOBJ oldFont = SelectObject(hdc, m_bodyFont);

	const int padding = Scale(16);
	const int rightPadding = Scale(18);
	const int spacing = Scale(8);
	const int headerGap = Scale(10);
	const int separatorHeight = Scale(1);
	const int toggleWidth = Scale(48);
	const int minWidth = Scale(360);
	int maxHeight = Scale(540);
	if (m_maxHeightOverride > 0) maxHeight = m_maxHeightOverride;

	// --- ヘッダーの高さ計算 ---
	if (m_isSubMenu)
	{
		// サブメニューの場合はヘッダーを表示しない
		m_headerHeight = 0;
	}
	else
	{
		SIZE titleSize{ 0,0 };
		SIZE subtitleSize{ 0,0 };
		SelectObject(hdc, m_titleFont);
		GetTextExtentPoint32W(hdc, m_model.title.c_str(), static_cast<int>(m_model.title.size()), &titleSize);

		SelectObject(hdc, m_bodyFont);
		if (!m_model.subtitle.empty())
		{
			GetTextExtentPoint32W(hdc, m_model.subtitle.c_str(), static_cast<int>(m_model.subtitle.size()), &subtitleSize);
		}

		int calculatedHeaderHeight = padding + titleSize.cy;
		if (!m_model.subtitle.empty())
		{
			calculatedHeaderHeight += Scale(4) + subtitleSize.cy;
		}
		calculatedHeaderHeight += padding;

		m_headerHeight = std::max(Scale(64), calculatedHeaderHeight);
	}

	m_layout.clear();

	// サブメニューはヘッダーがないので padding から開始
	int y = m_headerHeight + (m_isSubMenu ? padding : spacing);
	int width = minWidth;
	if (m_isSubMenu) width = Scale(200); // サブメニューの最小幅

	bool currentSectionCollapsed = false;

	// 「モーション」はホバーで横に展開したいので、ヘッダー配下のフラットな項目を仮想 children にまとめて
	// 親ウィンドウ内で縦方向に展開されないようにする（Windows 95 スタートメニュー風）
	auto isMotionHeaderTitle = [](const std::wstring& t) -> bool
		{
			if (t == L"モーション") return true;

			std::wstring s = t;
			for (auto& ch : s)
			{
				if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
			}
			return (s == L"motion" || s == L"motions");
		};

	std::unordered_map<int, std::vector<TrayMenuItem>> virtualChildren;
	std::vector<bool> skip(m_model.items.size(), false);

	for (size_t i = 0; i < m_model.items.size(); ++i)
	{
		const auto& it = m_model.items[i];
		if (it.kind != TrayMenuItem::Kind::Header && it.kind != TrayMenuItem::Kind::Action) continue;
		if (!it.children.empty()) continue;
		if (!isMotionHeaderTitle(it.title)) continue;

		std::vector<TrayMenuItem> kids;
		size_t j = i + 1;
		for (; j < m_model.items.size(); ++j)
		{
			if (m_model.items[j].kind == TrayMenuItem::Kind::Header) break;
			if (m_model.items[j].kind == TrayMenuItem::Kind::Separator) break;

			kids.push_back(m_model.items[j]);
			skip[j] = true;
		}

		if (!kids.empty())
		{
			virtualChildren[static_cast<int>(i)] = std::move(kids);
		}
	}

	for (size_t i = 0; i < m_model.items.size(); ++i)
	{
		if (skip[i]) continue;

		TrayMenuItem item = m_model.items[i];

		// Motion header -> サブメニュー用の通常行に変換（常に表示され、右/左に子を出す）
		bool forceVisible = false;
		if (auto it = virtualChildren.find(static_cast<int>(i)); it != virtualChildren.end())
		{
			// 元は Header なので、ここで「セクション境界」として collapse 状態をリセットする
			currentSectionCollapsed = false;

			TrayMenuItem converted = item;
			converted.kind = TrayMenuItem::Kind::Action;
			converted.commandId = 0;
			converted.toggled = false;
			converted.destructive = false;
			converted.children = it->second;

			item = std::move(converted);
			forceVisible = true;
		}

		LayoutItem layout{};
		layout.data = item;
		layout.modelIndex = static_cast<int>(i);

		if (item.kind == TrayMenuItem::Kind::Separator)
		{
			if (currentSectionCollapsed) continue;
			layout.bounds = { padding, y, width - rightPadding, y + separatorHeight };
			y += separatorHeight + spacing;
			m_layout.push_back(layout);
			continue;
		}

		if (item.kind == TrayMenuItem::Kind::Header)
		{
			currentSectionCollapsed = m_collapsedHeaders.count(item.title) > 0;
			SIZE headerSize{};
			HGDIOBJ old = SelectObject(hdc, m_headerFont);
			GetTextExtentPoint32W(hdc, item.title.c_str(), static_cast<int>(item.title.size()), &headerSize);
			SelectObject(hdc, old);

			layout.bounds = { padding, y, width - rightPadding, y + headerSize.cy + headerGap };
			y += headerSize.cy + headerGap;
			m_layout.push_back(layout);
			continue;
		}

		// --- 通常アイテム ---
		if (currentSectionCollapsed && !forceVisible) continue;

		int rowHeight = Scale(44);
		if (!item.subtitle.empty())
		{
			rowHeight = Scale(60);
		}

		SIZE textSize{};
		GetTextExtentPoint32W(hdc, item.title.c_str(), static_cast<int>(item.title.size()), &textSize);
		int rowWidth = textSize.cx + padding * 2;

		if (!item.subtitle.empty())
		{
			SIZE subSize{};
			GetTextExtentPoint32W(hdc, item.subtitle.c_str(), static_cast<int>(item.subtitle.size()), &subSize);
			rowWidth = std::max(static_cast<long>(rowWidth), subSize.cx + padding * 2);
		}

		// トグルスイッチまたはサブメニュー矢印のスペース
		if (item.kind == TrayMenuItem::Kind::Toggle)
		{
			rowWidth += toggleWidth + Scale(12);
		}
		else if (!item.children.empty())
		{
			rowWidth += Scale(20); // 矢印用
		}

		width = std::max(width, rowWidth);
		layout.bounds = { padding, y, width - rightPadding, y + rowHeight };
		y += rowHeight + spacing;
		m_layout.push_back(layout);
	}

	// サブメニューなら下部のパディングを追加
	if (m_isSubMenu) y += padding;

	m_totalHeight = y;

	// レイアウト計算後にすべての幅を最大幅に合わせる
	if (m_maxWidthOverride > 0) width = std::min(width, m_maxWidthOverride);
	for (auto& l : m_layout)
	{
		l.bounds.right = width - rightPadding;
	}

	m_windowSize.cx = width;
	m_windowSize.cy = std::min(m_totalHeight, maxHeight);

	const int contentHeight = std::max(0, m_totalHeight - m_headerHeight);
	const int viewportHeight = std::max(0l, m_windowSize.cy - m_headerHeight);
	m_maxScroll = std::max(0, contentHeight - viewportHeight);
	m_scrollOffset = std::min(m_scrollOffset, m_maxScroll);

	SelectObject(hdc, oldFont);
	ReleaseDC(nullptr, hdc);
}

void TrayMenuWindow::ShowAt(POINT anchor)
{
	m_lastAnchor = anchor;
	m_hasLastAnchor = true;
	EnsureWindow();
	if (!m_hWnd) return;

	RebuildLayout();
	m_scrollOffset = 0;
	m_hoveredIndex = -1;
	m_openSubMenuIndex = -1;

	// サブメニューの場合は親の位置に依存した配置ロジックが必要だが、
	// ShowAtはトップレベル呼び出しを想定。サブメニュー呼び出しは別処理で行う。

	const POINT pos = AdjustAnchorToWorkArea(m_windowSize, anchor);

	m_openTime = GetTickCount64();

	SetWindowPos(
		m_hWnd,
		HWND_TOPMOST,
		pos.x,
		pos.y,
		m_windowSize.cx,
		m_windowSize.cy,
		SWP_SHOWWINDOW);

	SetForegroundWindow(m_hWnd);
	SetFocus(m_hWnd);

	UpdateWindow(m_hWnd);
	m_visible = true;

	SetCapture(m_hWnd);
	m_hasCapture = (GetCapture() == m_hWnd);

	SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void TrayMenuWindow::UpdateTopLevelPlacement()
{
	if (!m_hWnd || !m_visible) return;
	if (m_isSubMenu) return;

	POINT anchor{};
	bool hasAnchor = m_hasLastAnchor;
	if (hasAnchor) anchor = m_lastAnchor;

	// アンカーが分からない場合は現在位置を基準にサイズだけ更新する
	RECT currentRc{};
	GetWindowRect(m_hWnd, &currentRc);
	POINT pos{ currentRc.left, currentRc.top };

	if (hasAnchor)
	{
		// 表示先モニターの work area に合わせてサイズ上限を更新（上下左右はみ出し対策）
		HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		if (GetMonitorInfoW(monitor, &mi))
		{
			const int margin = Scale(8);
			const int workW = static_cast<int>(mi.rcWork.right - mi.rcWork.left);
			const int workH = static_cast<int>(mi.rcWork.bottom - mi.rcWork.top);
			m_maxWidthOverride = std::max(1, workW - margin * 2);
			m_maxHeightOverride = std::max(1, workH - margin * 2);
		}

		pos = AdjustAnchorToWorkArea(m_windowSize, anchor);
	}

	SetWindowPos(
		m_hWnd,
		HWND_TOPMOST,
		pos.x,
		pos.y,
		m_windowSize.cx,
		m_windowSize.cy,
		SWP_NOACTIVATE);
}


void TrayMenuWindow::HideLocal()
{
	if (!m_hWnd || !m_visible) return;

	CancelPendingSubMenuClose();
	CloseSubMenu();

	// 実際にキャプチャを持っている場合だけ解放
	if (GetCapture() == m_hWnd)
	{
		ReleaseCapture();
	}
	m_hasCapture = false;

	ShowWindow(m_hWnd, SW_HIDE);
	m_visible = false;
	m_hoveredIndex = -1;
	m_scrollOffset = 0;
}


void TrayMenuWindow::Hide()
{
	if (!m_hWnd || !m_visible) return;

	HideLocal();

	// 親メニューも閉じる（チェーンを全て閉じる場合）
	if (m_parentWindow && m_parentWindow->m_visible)
	{
		m_parentWindow->Hide();
	}
}

// サブメニュー専用の「閉じる」処理（親は閉じない）
void TrayMenuWindow::CloseSubMenu()
{
	CancelPendingSubMenuClose();
	if (m_subMenu && m_subMenu->m_visible)
	{
		m_subMenu->HideLocal(); // 親は閉じない
	}
	m_openSubMenuIndex = -1;
}

// 特定のインデックスのサブメニューを開く
void TrayMenuWindow::OpenSubMenu(int index)
{
	CancelPendingSubMenuClose();
	if (index < 0 || index >= static_cast<int>(m_layout.size())) return;

	const auto& layoutItem = m_layout[index];
	if (layoutItem.data.children.empty())
		return;

	// 既存のサブメニューが開いている場合
	if (m_subMenu && m_subMenu->m_visible)
	{
		if (m_openSubMenuIndex == index) return;
		m_subMenu->HideLocal();
	}

	if (!m_subMenu)
	{
		m_subMenu = std::make_unique<TrayMenuWindow>(m_hInst, m_onCommand);
		m_subMenu->m_parentWindow = this;
		m_subMenu->SetIsSubMenu(true);
	}

	TrayMenuModel subModel;
	subModel.title = layoutItem.data.title;
	subModel.subtitle.clear();
	subModel.items = layoutItem.data.children;

	m_subMenu->SetModel(subModel);
	m_openSubMenuIndex = index;

	// サブメニュー側のサイズを先に確定させる（モニター境界に収める）
	RECT itemRect = layoutItem.bounds;
	OffsetRect(&itemRect, 0, -m_scrollOffset);

	POINT ptRight{ itemRect.right, itemRect.top };
	POINT ptLeft{ itemRect.left, itemRect.top };
	ClientToScreen(m_hWnd, &ptRight);
	ClientToScreen(m_hWnd, &ptLeft);

	HMONITOR monitor = MonitorFromPoint(ptRight, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi{ sizeof(MONITORINFO) };
	GetMonitorInfoW(monitor, &mi);

	const int margin = Scale(8);
	const int workW = mi.rcWork.right - mi.rcWork.left;
	const int workH = mi.rcWork.bottom - mi.rcWork.top;

	m_subMenu->m_maxHeightOverride = std::max(1, workH - margin * 2);
	m_subMenu->m_maxWidthOverride = 0;
	m_subMenu->EnsureWindow();
	m_subMenu->RebuildLayout();

	int desiredW = m_subMenu->m_windowSize.cx;
	int desiredH = m_subMenu->m_windowSize.cy;

	const int rightSpace = (mi.rcWork.right - margin) - ptRight.x;
	const int leftSpace = ptLeft.x - (mi.rcWork.left + margin);

	bool openRight = true;
	if (desiredW <= rightSpace) openRight = true;
	else if (desiredW <= leftSpace) openRight = false;
	else openRight = (rightSpace >= leftSpace);

	m_subMenu->m_maxWidthOverride = std::max(1, openRight ? rightSpace : leftSpace);
	m_subMenu->RebuildLayout();
	desiredW = m_subMenu->m_windowSize.cx;
	desiredH = m_subMenu->m_windowSize.cy;

	int subX = openRight ? ptRight.x : (ptLeft.x - desiredW);
	int subY = ptRight.y - Scale(4);

	// モニター範囲に収める
	if (subX < mi.rcWork.left + margin) subX = mi.rcWork.left + margin;
	if (subX + desiredW > mi.rcWork.right - margin) subX = mi.rcWork.right - margin - desiredW;

	if (subY < mi.rcWork.top + margin) subY = mi.rcWork.top + margin;
	if (subY + desiredH > mi.rcWork.bottom - margin) subY = mi.rcWork.bottom - margin - desiredH;

	SetWindowPos(m_subMenu->m_hWnd, HWND_TOPMOST, subX, subY, desiredW, desiredH,
				 SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_subMenu->m_visible = true;
	m_subMenu->m_openTime = GetTickCount64();
}



LRESULT CALLBACK TrayMenuWindow::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TrayMenuWindow* self = nullptr;
	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = reinterpret_cast<TrayMenuWindow*>(cs->lpCreateParams);
		self->m_hWnd = hWnd;
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<TrayMenuWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}

	if (!self)
	{
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	return self->WndProc(hWnd, msg, wParam, lParam);
}


LRESULT TrayMenuWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (!m_hWnd) m_hWnd = hWnd;

	const bool inGuardPeriod = (GetTickCount64() - m_openTime < 300);

	switch (msg)
	{
		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				SetCursor(LoadCursor(nullptr, IDC_ARROW));
				return TRUE;
			}
			break;

		case WM_NCDESTROY:
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
			if (GetCapture() == hWnd) ReleaseCapture();
			m_hasCapture = false;
			m_hWnd = nullptr;
			m_visible = false;
			return 0;

		case WM_CAPTURECHANGED:
		{
			// キャプチャが変更されたとき
			HWND hNewCapture = reinterpret_cast<HWND>(lParam);

			// 1. 自分がキャプチャを失ったが、新しいキャプチャ先が自分のサブメニューなら閉じない
			if (m_subMenu && m_subMenu->m_visible && hNewCapture == m_subMenu->m_hWnd)
			{
				return 0;
			}

			// 2. 自分がキャプチャを失ったが、新しいキャプチャ先が自分の親なら閉じない
			if (m_parentWindow && hNewCapture == m_parentWindow->m_hWnd)
			{
				return 0;
			}

			// それ以外（外部クリックなど）なら閉じる
			if (!inGuardPeriod && m_visible)
			{
				// 親がいる場合、ここでHideすると親のHideも連鎖して呼ばれる
				// サブメニューがキャプチャを失った場合、親に戻る動作が必要

				// マウスが親ウィンドウの領域内に戻った場合は閉じずに親にキャプチャを返す
				POINT pt;
				GetCursorPos(&pt);
				if (m_parentWindow && WindowFromPoint(pt) == m_parentWindow->m_hWnd)
				{
					// 親へ制御を戻す（親が再キャプチャする）
					// ReleaseCaptureは既に呼ばれている
					return 0;
				}

				Hide();
			}
			return 0;
		}

		        case WM_TIMER :
		        {
		            if(wParam == kTimerSubMenuClose)
		            {
		                KillTimer(hWnd, kTimerSubMenuClose);
		                m_subMenuCloseTimerArmed = false;

		                if(m_subMenu && m_subMenu->m_visible)
		                {
		                    POINT spt{};
		                    GetCursorPos(&spt);
		                    if(!m_subMenu->ContainsScreenPoint(spt))
		                    {
		                        CloseSubMenu();
		                        InvalidateRect(hWnd, nullptr, FALSE);
		                    }
		                }
		                return 0;
		            }
		            break;
		         }
		case WM_ACTIVATE:
			if (LOWORD(wParam) == WA_INACTIVE)
			{
				// 親ウィンドウがアクティブなら閉じない
				HWND hOther = reinterpret_cast<HWND>(lParam);
				if (m_parentWindow && hOther == m_parentWindow->m_hWnd) return 0;
				if (m_subMenu && hOther == m_subMenu->m_hWnd) return 0;

				if (!inGuardPeriod) Hide();
			}
			return 0;

			        case WM_LBUTTONDOWN :
			        case WM_RBUTTONDOWN :
			        case WM_MBUTTONDOWN :
			        case WM_LBUTTONUP :
			        case WM_RBUTTONUP :
			        case WM_MBUTTONUP :
			        case WM_LBUTTONDBLCLK :
			        case WM_RBUTTONDBLCLK :
			        case WM_MBUTTONDBLCLK :
			        {
			            // Root window holds capture and routes mouse to the deepest submenu under cursor.
			            if(!m_isSubMenu && GetCapture() == hWnd)
			            {
			                RouteCapturedMouseButton(msg, wParam, lParam);
			                return 0;
			            }

			            // Fallback: if we aren't capturing, process locally.
			            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			            RECT rc{};
			            GetClientRect(hWnd, &rc);
			            if(!PtInRect(&rc, pt))
			            {
			                Hide();
			                return 0;
			            }
			            if(msg == WM_LBUTTONUP)
			            {
			                HandleMouse(pt, true);
			            }
			            return 0;
			         }
				        case WM_MOUSEMOVE :
			        {
			            if(!m_isSubMenu && GetCapture() == hWnd)
			            {
			                RouteCapturedMouseMove();
			                return 0;
			            }

			            if(!m_trackingMouse)
			            {
			                TRACKMOUSEEVENT tme{ sizeof(tme) };
			                tme.dwFlags = TME_LEAVE;
			                tme.hwndTrack = hWnd;
			                TrackMouseEvent(&tme);
			                m_trackingMouse = true;
			            }

			            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			            HandleMouse(pt, false);
			            return 0;
			         }
		case WM_MOUSELEAVE:
			m_trackingMouse = false;
			// サブメニューが開いていない場合のみハイライト解除
			if (!m_subMenu || !m_subMenu->m_visible)
			{
				if (m_hoveredIndex != -1)
				{
					m_hoveredIndex = -1;
					InvalidateRect(hWnd, nullptr, FALSE);
				}
			}
			return 0;

			        case WM_MOUSEWHEEL :
			        {
			            if(!m_isSubMenu && GetCapture() == hWnd)
			            {
			                RouteCapturedMouseWheel(wParam, lParam);
			                return 0;
			            }

			            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			            OnMouseWheelDelta(delta);
			            return 0;
			         }
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE)
			{
				Hide();
				return 0;
			}
			// サブメニューがある場合、左キーで閉じる等の操作も可能だが省略
			break;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			HDC hdc = BeginPaint(hWnd, &ps);
			Paint(hdc);
			EndPaint(hWnd, &ps);
			return 0;
		}
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}


void TrayMenuWindow::Paint(HDC hdc)
{
	const auto& theme = GetTheme();
	RECT rc{};
	GetClientRect(m_hWnd, &rc);

	HBRUSH bg = CreateSolidBrush(theme.background);
	FillRect(hdc, &rc, bg);
	DeleteObject(bg);

	SetBkMode(hdc, TRANSPARENT);

	// 1. ヘッダーを描画 (サブメニューなら高さ0なので描画されない)
	if (m_headerHeight > 0)
	{
		DrawHeader(hdc, 0);
	}

	// クリップ領域設定
	RECT clipRc = rc;
	clipRc.top = m_headerHeight;
	HRGN hRgn = CreateRectRgnIndirect(&clipRc);
	SelectClipRgn(hdc, hRgn);

	// 2. アイテムを描画
	for (size_t i = 0; i < m_layout.size(); ++i)
	{
		const auto& item = m_layout[i];
		RECT shifted = item.bounds;
		OffsetRect(&shifted, 0, -m_scrollOffset);

		// 画面外ならスキップ
		if (shifted.bottom < m_headerHeight || shifted.top > rc.bottom) continue;

		DrawItem(hdc, item, -m_scrollOffset);
	}

	SelectClipRgn(hdc, nullptr);
	DeleteObject(hRgn);

	if (m_maxScroll > 0)
	{
		DrawScrollbar(hdc);
	}
}

void TrayMenuWindow::DrawHeader(HDC hdc, int offsetY) const
{
	const auto& theme = GetTheme();
	RECT header{ 0, offsetY, m_windowSize.cx, m_headerHeight + offsetY };
	HBRUSH hb = CreateSolidBrush(theme.headerBackground);
	FillRect(hdc, &header, hb);
	DeleteObject(hb);

	const int padding = Scale(16);

	SIZE titleSize;
	HGDIOBJ old = SelectObject(hdc, m_titleFont);
	GetTextExtentPoint32W(hdc, m_model.title.c_str(), static_cast<int>(m_model.title.size()), &titleSize);

	RECT titleRc{ padding, padding + offsetY, m_windowSize.cx - padding, padding + titleSize.cy + offsetY };

	SetTextColor(hdc, theme.textPrimary);
	DrawTextW(hdc, m_model.title.c_str(), static_cast<int>(m_model.title.size()), &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	if (!m_model.subtitle.empty())
	{
		SetTextColor(hdc, theme.textMuted);
		SelectObject(hdc, m_bodyFont);

		SIZE subSize;
		GetTextExtentPoint32W(hdc, m_model.subtitle.c_str(), static_cast<int>(m_model.subtitle.size()), &subSize);
		RECT subtitleRc{ padding, titleRc.bottom + Scale(4), m_windowSize.cx - padding, titleRc.bottom + Scale(4) + subSize.cy };

		DrawTextW(hdc, m_model.subtitle.c_str(), static_cast<int>(m_model.subtitle.size()), &subtitleRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	SelectObject(hdc, old);
}

void TrayMenuWindow::DrawItem(HDC hdc, const LayoutItem& item, int offsetY) const
{
	const auto& theme = GetTheme();
	RECT rc = item.bounds;
	OffsetRect(&rc, 0, offsetY);

	if (item.data.kind == TrayMenuItem::Kind::Separator)
	{
		HPEN pen = CreatePen(PS_SOLID, 1, theme.outline);
		HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
		MoveToEx(hdc, rc.left, rc.top, nullptr);
		LineTo(hdc, rc.right, rc.top);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		return;
	}

	if (item.data.kind == TrayMenuItem::Kind::Header)
	{
		HGDIOBJ oldFont = SelectObject(hdc, m_headerFont);
		SetTextColor(hdc, theme.textMuted);

		const LayoutItem* ptr = &item;
		if (static_cast<int>(ptr - m_layout.data()) == m_hoveredIndex)
		{
			SetTextColor(hdc, theme.textPrimary);
		}

		RECT textRc = rc;
		textRc.right -= Scale(20);
		DrawTextW(hdc, item.data.title.c_str(), static_cast<int>(item.data.title.size()), &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		bool isCollapsed = m_collapsedHeaders.count(item.data.title) > 0;
		HPEN chevPen = CreatePen(PS_SOLID, 1, theme.textMuted);
		HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, chevPen));

		int cy = (rc.top + rc.bottom) / 2;
		int cx = rc.right - Scale(10);
		int sz = Scale(3);

		if (isCollapsed)
		{
			MoveToEx(hdc, cx - sz, cy - sz, nullptr);
			LineTo(hdc, cx + sz / 2, cy);
			LineTo(hdc, cx - sz, cy + sz);
		}
		else
		{
			MoveToEx(hdc, cx - sz, cy - sz / 2, nullptr);
			LineTo(hdc, cx, cy + sz / 2);
			LineTo(hdc, cx + sz, cy - sz / 2);
		}

		SelectObject(hdc, oldPen);
		DeleteObject(chevPen);
		SelectObject(hdc, oldFont);
		return;
	}

	const LayoutItem* ptr = &item;
	const bool hovered = static_cast<int>(ptr - m_layout.data()) == m_hoveredIndex;
	// サブメニューが開いているアイテムはずっとホバー表示にする
	const bool subMenuOpen = (m_openSubMenuIndex != -1 && static_cast<int>(ptr - m_layout.data()) == m_openSubMenuIndex);

	if (hovered || subMenuOpen)
	{
		HBRUSH hover = CreateSolidBrush(theme.cardHover);
		FillRect(hdc, &rc, hover);
		DeleteObject(hover);
	}

	RECT textRc = rc;
	const int padding = Scale(16);
	textRc.left += padding;
	textRc.right -= padding;

	HGDIOBJ oldFont = SelectObject(hdc, m_bodyFont);
	SetTextColor(hdc, item.data.destructive ? theme.danger : theme.textPrimary);
	DrawTextW(hdc, item.data.title.c_str(), static_cast<int>(item.data.title.size()), &textRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

	if (!item.data.subtitle.empty())
	{
		RECT subRc = textRc;
		subRc.top += Scale(22);
		SetTextColor(hdc, theme.textMuted);
		DrawTextW(hdc, item.data.subtitle.c_str(), static_cast<int>(item.data.subtitle.size()), &subRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	// サブメニュー用の矢印 (>)
	if (!item.data.children.empty())
	{
		HPEN arrowPen = CreatePen(PS_SOLID, 1, theme.textMuted);
		HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, arrowPen));

		int cy = (rc.top + rc.bottom) / 2;
		int cx = rc.right - Scale(10);
		int sz = Scale(3);

		MoveToEx(hdc, cx - sz, cy - sz, nullptr);
		LineTo(hdc, cx + sz / 2, cy);
		LineTo(hdc, cx - sz, cy + sz);

		SelectObject(hdc, oldPen);
		DeleteObject(arrowPen);
	}

	if (item.data.kind == TrayMenuItem::Kind::Toggle)
	{
		const int toggleWidth = Scale(46);
		const int toggleHeight = Scale(24);
		const int marginRight = Scale(18);
		RECT pill{
			rc.right - marginRight - toggleWidth,
			rc.top + (rc.bottom - rc.top - toggleHeight) / 2,
			rc.right - marginRight,
			rc.top + (rc.bottom - rc.top - toggleHeight) / 2 + toggleHeight
		};

		HBRUSH pillBrush = CreateSolidBrush(item.data.toggled ? theme.accent : theme.outline);
		HPEN outline = CreatePen(PS_SOLID, 1, item.data.toggled ? theme.accent : theme.outline);
		HGDIOBJ oldPen = SelectObject(hdc, outline);
		HGDIOBJ oldBrush = SelectObject(hdc, pillBrush);

		RoundRect(hdc, pill.left, pill.top, pill.right, pill.bottom, Scale(20), Scale(20));

		SelectObject(hdc, oldBrush);
		SelectObject(hdc, oldPen);
		DeleteObject(pillBrush);
		DeleteObject(outline);

		const int knobSize = toggleHeight - Scale(8);
		const int knobTop = pill.top + Scale(4);
		int knobLeft = pill.left + Scale(4);
		if (item.data.toggled)
		{
			knobLeft = pill.right - knobSize - Scale(4);
		}
		RECT knob{ knobLeft, knobTop, knobLeft + knobSize, knobTop + knobSize };
		HBRUSH knobBrush = CreateSolidBrush(Rgb(245, 245, 245));
		HPEN knobPen = CreatePen(PS_SOLID, 1, Rgb(220, 220, 220));
		oldPen = SelectObject(hdc, knobPen);
		oldBrush = SelectObject(hdc, knobBrush);
		Ellipse(hdc, knob.left, knob.top, knob.right, knob.bottom);
		SelectObject(hdc, oldBrush);
		DeleteObject(knobBrush);
		DeleteObject(knobPen);
		SelectObject(hdc, oldPen);
	}

	SelectObject(hdc, oldFont);
}

void TrayMenuWindow::DrawScrollbar(HDC hdc) const
{
	const auto& theme = GetTheme();
	const int trackWidth = Scale(6);
	RECT rc{};
	GetClientRect(m_hWnd, &rc);

	// サブメニューはヘッダーが無いので上端から
	const int topOffset = m_headerHeight > 0 ? m_headerHeight + Scale(12) : Scale(4);
	const int trackLeft = rc.right - Scale(10);
	RECT track{ trackLeft, topOffset, trackLeft + trackWidth, rc.bottom - Scale(12) };

	if (track.bottom <= track.top) return;
	HBRUSH trackBrush = CreateSolidBrush(theme.outline);
	FillRect(hdc, &track, trackBrush);
	DeleteObject(trackBrush);

	const int contentHeight = std::max(0, m_totalHeight - m_headerHeight);
	const int viewportHeight = std::max(1l, m_windowSize.cy - m_headerHeight);
	const double ratio = contentHeight > 0
		? static_cast<double>(viewportHeight) / static_cast<double>(contentHeight)
		: 1.0;
	const int thumbHeight = std::max(Scale(30), static_cast<int>(ratio * (track.bottom - track.top)));
	const double scrollRatio = static_cast<double>(m_scrollOffset) / std::max(1, m_maxScroll);
	const int thumbTop = track.top + static_cast<int>((track.bottom - track.top - thumbHeight) * scrollRatio);

	RECT thumb{ track.left, thumbTop, track.right, thumbTop + thumbHeight };
	HBRUSH thumbBrush = CreateSolidBrush(theme.cardHover);
	FillRect(hdc, &thumb, thumbBrush);
	DeleteObject(thumbBrush);
}

void TrayMenuWindow::HandleMouse(POINT pt, bool activate)
{
	int index = -1;
	for (size_t i = 0; i < m_layout.size(); ++i)
	{
		RECT rc = m_layout[i].bounds;
		OffsetRect(&rc, 0, -m_scrollOffset);
		if (PtInRect(&rc, pt))
		{
			index = static_cast<int>(i);

			// --- ホバー時のサブメニュー制御 ---
			if (!activate) // MouseMove
			{
				// 違うアイテムに乗った & そのアイテムが子を持つならサブメニューを開く
				if (index != m_openSubMenuIndex)
				{
					if (!m_layout[i].data.children.empty())
					{
						OpenSubMenu(index);
					}
					// ここでのArmPendingSubMenuCloseは削除し、ループ外で統一的に処理する
				}
			}

			// --- クリック時の処理 ---
			if (activate)
			{
				if (m_layout[i].data.kind == TrayMenuItem::Kind::Header)
				{
					const auto& title = m_layout[i].data.title;
					if (m_collapsedHeaders.count(title))
						m_collapsedHeaders.erase(title);
					else
						m_collapsedHeaders.insert(title);

					RebuildLayout();
					UpdateTopLevelPlacement();
					InvalidateRect(m_hWnd, nullptr, FALSE);
					return;
				}
				else if (m_layout[i].data.kind != TrayMenuItem::Kind::Separator)
				{
					if (m_layout[i].data.children.empty())
					{
						HandleCommand(m_layout[i].data);
					}
				}
			}
			break;
		}
	}

	if (!activate)
	{
		// サブメニューを開いている親アイテムの上にいない場合、すべて閉じる対象とする
		if (index != m_openSubMenuIndex)
		{
			// 余白(index == -1) または 別のアイテム の場合
			ArmPendingSubMenuClose(index, 300);
		}
		else
		{
			// サブメニューを開いている親アイテム上にいるなら閉じない
			CancelPendingSubMenuClose();
		}
	}

	if (index != m_hoveredIndex)
	{
		m_hoveredIndex = index;
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void TrayMenuWindow::HandleCommand(const TrayMenuItem& item)
{
	if (item.commandId == 0) return;

	TrayMenuWindow* root = this;
	while (root->m_parentWindow)
	{
		root = root->m_parentWindow;
	}
	root->Hide();

	if (m_onCommand)
	{
		m_onCommand(item.commandId);
	}
}

int TrayMenuWindow::Scale(int value) const
{
	const int dpi = (m_dpi > 0) ? static_cast<int>(m_dpi) : 96;
	return MulDiv(value, dpi, 96);
}

POINT TrayMenuWindow::AdjustAnchorToWorkArea(const SIZE& size, POINT anchor) const
{
	HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info{ sizeof(MONITORINFO) };
	GetMonitorInfoW(monitor, &info);

	int x = anchor.x - size.cx + Scale(12);
	int y = anchor.y - size.cy - Scale(12);

	if (x < info.rcWork.left) x = info.rcWork.left + Scale(8);
	if (y < info.rcWork.top) y = anchor.y + Scale(12);

	if (x + size.cx > info.rcWork.right) x = info.rcWork.right - size.cx - Scale(8);
	if (y + size.cy > info.rcWork.bottom) y = info.rcWork.bottom - size.cy - Scale(8);

	return POINT{ x, y };
}

void TrayMenuWindow::OnMouseWheelDelta(int wheelDelta)
{
	if (!m_hWnd) return;
	if (m_maxScroll <= 0) return;

	const int step = Scale(40);
	m_scrollOffset -= (wheelDelta / WHEEL_DELTA) * step;
	if (m_scrollOffset < 0) m_scrollOffset = 0;
	if (m_scrollOffset > m_maxScroll) m_scrollOffset = m_maxScroll;
	InvalidateRect(m_hWnd, nullptr, FALSE);
}

TrayMenuWindow* TrayMenuWindow::GetRootWindow()
{
	TrayMenuWindow* w = this;
	while (w && w->m_parentWindow) w = w->m_parentWindow;
	return w ? w : this;
}

bool TrayMenuWindow::ContainsScreenPoint(POINT screenPt) const
{
	if (!m_hWnd || !m_visible) return false;
	RECT wr{};
	GetWindowRect(m_hWnd, &wr);
	return PtInRect(&wr, screenPt) != FALSE;
}

TrayMenuWindow* TrayMenuWindow::HitTestDeepestWindow(POINT screenPt)
{
	if (m_subMenu && m_subMenu->m_visible)
	{
		if (TrayMenuWindow* deeper = m_subMenu->HitTestDeepestWindow(screenPt))
			return deeper;
	}

	if (ContainsScreenPoint(screenPt)) return this;

	return nullptr;
}

void TrayMenuWindow::RouteCapturedMouseMove()
{
	TrayMenuWindow* root = GetRootWindow();
	if (!root || !root->m_hWnd || !root->m_visible) return;

	POINT spt{};
	GetCursorPos(&spt);
	TrayMenuWindow* target = root->HitTestDeepestWindow(spt);
	if (!target)
	{
		// メニュー外: ホバー解除（サブメニューは即座には閉じない）
		if (root->m_hoveredIndex != -1)
		{
			root->m_hoveredIndex = -1;
			InvalidateRect(root->m_hWnd, nullptr, FALSE);
		}
		return;
	}

	// サブメニュー上にいる間は、親側の遅延クローズをキャンセルする
	for (TrayMenuWindow* w = target; w; w = w->m_parentWindow)
	{
		w->CancelPendingSubMenuClose();
	}

	POINT cpt = spt;
	ScreenToClient(target->m_hWnd, &cpt);
	target->HandleMouse(cpt, false);
}

void TrayMenuWindow::RouteCapturedMouseButton(UINT msg, WPARAM wParam, LPARAM lParam)
{
	TrayMenuWindow* root = GetRootWindow();
	if (!root || !root->m_hWnd || !root->m_visible) return;

	POINT cptRoot{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	POINT spt = cptRoot;
	ClientToScreen(root->m_hWnd, &spt);

	TrayMenuWindow* target = root->HitTestDeepestWindow(spt);
	if (!target)
	{
		// メニュー外クリックで閉じる
		if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
		{
			root->Hide();
		}
		return;
	}

	POINT cpt = spt;
	ScreenToClient(target->m_hWnd, &cpt);

	if (msg == WM_LBUTTONUP)
	{
		target->HandleMouse(cpt, true);
	}
	(void)wParam;
}

void TrayMenuWindow::RouteCapturedMouseWheel(WPARAM wParam, LPARAM lParam)
{
	TrayMenuWindow* root = GetRootWindow();
	if (!root || !root->m_hWnd || !root->m_visible) return;

	POINT spt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	TrayMenuWindow* target = root->HitTestDeepestWindow(spt);
	if (!target) target = root;

	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	target->OnMouseWheelDelta(delta);
}

void TrayMenuWindow::CancelPendingSubMenuClose()
{
	if (!m_hWnd) return;
	if (m_subMenuCloseTimerArmed)
	{
		KillTimer(m_hWnd, kTimerSubMenuClose);
		m_subMenuCloseTimerArmed = false;
		m_subMenuCloseHoverIndex = -1;
	}
}

void TrayMenuWindow::ArmPendingSubMenuClose(int hoveredIndex, UINT delayMs)
{
	if (!m_hWnd) return;
	if (!m_subMenu || !m_subMenu->m_visible) return;
	if (hoveredIndex == m_openSubMenuIndex) return;

	KillTimer(m_hWnd, kTimerSubMenuClose);
	m_subMenuCloseTimerArmed = true;
	m_subMenuCloseHoverIndex = hoveredIndex;
	SetTimer(m_hWnd, kTimerSubMenuClose, delayMs, nullptr);
}
