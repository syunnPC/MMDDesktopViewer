#include "InputManager.hpp"
#include "App.hpp"
#include <windowsx.h>

namespace
{
	constexpr int kHotKeyToggleGizmoId = 1;
	constexpr int kHotKeyTogglePhysicsId = 2;
	constexpr int kHotKeyToggleWindowManipId = 3;

	constexpr UINT kHotKeyToggleGizmoMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyToggleGizmoVk = 'G';
	constexpr UINT kHotKeyTogglePhysicsMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyTogglePhysicsVk = 'P';
	constexpr UINT kHotKeyToggleWindowManipMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
	constexpr UINT kHotKeyToggleWindowManipVk = 'R';
}

InputManager::InputManager(App& app)
	: m_app(app)
{
}

void InputManager::RegisterHotkeys(HWND renderWnd)
{
	if (!renderWnd) return;

	if (!RegisterHotKey(renderWnd, kHotKeyToggleGizmoId, kHotKeyToggleGizmoMods, kHotKeyToggleGizmoVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+G).\n");
	}
	if (!RegisterHotKey(renderWnd, kHotKeyTogglePhysicsId, kHotKeyTogglePhysicsMods, kHotKeyTogglePhysicsVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+P).\n");
	}
	if (!RegisterHotKey(renderWnd, kHotKeyToggleWindowManipId, kHotKeyToggleWindowManipMods, kHotKeyToggleWindowManipVk))
	{
		OutputDebugStringA("RegisterHotKey failed (Ctrl+Alt+R).\n");
	}
}

void InputManager::UnregisterHotkeys(HWND renderWnd)
{
	if (!renderWnd) return;
	UnregisterHotKey(renderWnd, kHotKeyToggleGizmoId);
	UnregisterHotKey(renderWnd, kHotKeyTogglePhysicsId);
	UnregisterHotKey(renderWnd, kHotKeyToggleWindowManipId);
}

void InputManager::SetWindows(HWND renderWnd, HWND gizmoWnd)
{
	m_renderWnd = renderWnd;
	m_gizmoWnd = gizmoWnd;
}

bool InputManager::HandleHotkey(WPARAM wParam)
{
	if (wParam == kHotKeyToggleGizmoId)
	{
		m_app.ToggleGizmoWindow();
		return true;
	}
	if (wParam == kHotKeyTogglePhysicsId)
	{
		m_app.TogglePhysics();
		return true;
	}
	if (wParam == kHotKeyToggleWindowManipId)
	{
		m_app.ToggleWindowManipulation();
		return true;
	}
	return false;
}

bool InputManager::HandleMouseDown(HWND hWnd, UINT msg)
{
	if (hWnd != m_gizmoWnd) return false;

	if (msg == WM_LBUTTONDOWN)
	{
		m_gizmoLeftDrag = true;
		m_gizmoRightDrag = false;
		SetCapture(hWnd);
		GetCursorPos(&m_gizmoLastCursor);
		return true;
	}
	if (msg == WM_RBUTTONDOWN)
	{
		m_gizmoRightDrag = true;
		m_gizmoLeftDrag = false;
		SetCapture(hWnd);
		GetCursorPos(&m_gizmoLastCursor);
		return true;
	}
	return false;
}

bool InputManager::HandleMouseUp(HWND hWnd, UINT msg)
{
	if (hWnd != m_gizmoWnd) return false;

	if (msg == WM_RBUTTONUP && m_gizmoRightDrag)
	{
		m_gizmoRightDrag = false;
		ReleaseCapture();
		return true;
	}
	if (msg == WM_LBUTTONUP && m_gizmoLeftDrag)
	{
		m_gizmoLeftDrag = false;
		ReleaseCapture();
		return true;
	}
	return false;
}

bool InputManager::HandleMouseMove(HWND hWnd)
{
	if (hWnd != m_gizmoWnd) return false;
	if (!m_gizmoLeftDrag && !m_gizmoRightDrag) return false;

	POINT cursorNow{};
	GetCursorPos(&cursorNow);
	const int dx = cursorNow.x - m_gizmoLastCursor.x;
	const int dy = cursorNow.y - m_gizmoLastCursor.y;
	m_gizmoLastCursor = cursorNow;

	if (m_gizmoLeftDrag)
	{
		m_app.MoveRenderWindowBy(dx, dy);
	}
	else if (m_gizmoRightDrag)
	{
		m_app.AddCameraRotation(static_cast<float>(dx), static_cast<float>(dy));
	}

	m_app.RenderGizmo();
	return true;
}

bool InputManager::HandleMouseWheel(HWND hWnd, int delta, WPARAM wParam)
{
	if (hWnd != m_gizmoWnd) return false;
	(void)wParam;

	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

	if (ctrl && shift)
	{
		const float adjustment = (delta > 0) ? 0.1f : -0.1f;
		m_app.AdjustScale(adjustment);
		return true;
	}
	if (ctrl)
	{
		const float adjustment = (delta > 0) ? 0.1f : -0.1f;
		m_app.AdjustBrightness(adjustment);
		return true;
	}

	const float steps = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
	const float pseudoDx = steps * 12.0f;
	m_app.AddCameraRotation(pseudoDx, 0.0f);
	return true;
}

bool InputManager::HandleCaptureChanged(HWND hWnd)
{
	if (hWnd != m_gizmoWnd) return false;
	ResetGizmoDrag();
	return true;
}

void InputManager::CancelGizmoDrag(HWND hWnd)
{
	if (hWnd != m_gizmoWnd) return;
	ResetGizmoDrag();
	ReleaseCapture();
}

void InputManager::ResetGizmoDrag()
{
	m_gizmoLeftDrag = false;
	m_gizmoRightDrag = false;
}