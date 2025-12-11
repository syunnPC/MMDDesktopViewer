#pragma once
#include <windows.h>
#include <shellapi.h>

class TrayIcon
{
public:
	TrayIcon(HWND owner, UINT id);
	~TrayIcon();

	TrayIcon(const TrayIcon&) = delete;
	TrayIcon& operator=(const TrayIcon&) = delete;

	void Show(const wchar_t* tooltip);
	void Hide();
	void SetContextMenu(HMENU menu);

	bool HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;

	UINT CallbackMessage() const
	{
		return m_callbackMsg;
	}

private:
	void ShowContextMenu() const;

	HWND m_owner{};
	UINT m_id{};
	HMENU m_menu{};

	NOTIFYICONDATAW m_nid{};
	UINT m_callbackMsg{ WM_APP + 10 };
	bool m_visible{ false };
};