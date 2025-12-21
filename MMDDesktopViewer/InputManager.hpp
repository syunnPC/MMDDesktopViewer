#pragma once

#include <windows.h>

class App;

class InputManager
{
public:
	explicit InputManager(App& app);

	void RegisterHotkeys(HWND renderWnd);
	void UnregisterHotkeys(HWND renderWnd);
	void SetWindows(HWND renderWnd, HWND gizmoWnd);

	bool HandleHotkey(WPARAM wParam);
	bool HandleMouseDown(HWND hWnd, UINT msg);
	bool HandleMouseUp(HWND hWnd, UINT msg);
	bool HandleMouseMove(HWND hWnd);
	bool HandleMouseWheel(HWND hWnd, int delta, WPARAM wParam);
	bool HandleCaptureChanged(HWND hWnd);
	void CancelGizmoDrag(HWND hWnd);
	void ResetGizmoDrag();

private:
	App& m_app;
	HWND m_renderWnd{};
	HWND m_gizmoWnd{};

	bool m_gizmoLeftDrag{ false };
	bool m_gizmoRightDrag{ false };
	POINT m_gizmoLastCursor{};
};