#include "Camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	void GetClientSize(HWND hwnd, UINT& w, UINT& h)
	{
		RECT rc{};
		GetClientRect(hwnd, &rc);
		w = static_cast<UINT>(rc.right - rc.left);
		h = static_cast<UINT>(rc.bottom - rc.top);
		if (w == 0) w = 1;
		if (h == 0) h = 1;
	}
}

// 0 = 自動ウィンドウリサイズしない
// 1 = 自動リサイズする
#ifndef DCOMP_AUTOFIT_WINDOW
#define DCOMP_AUTOFIT_WINDOW 0
#endif

// 1 の場合、必要エリアが縮んでも解放しない
#ifndef DCOMP_AUTOFIT_GROW_ONLY
#define DCOMP_AUTOFIT_GROW_ONLY 1
#endif

// 連続リサイズを抑制
#ifndef DCOMP_AUTOFIT_COOLDOWN_FRAMES
#define DCOMP_AUTOFIT_COOLDOWN_FRAMES 8
#endif

void Camera::AdjustScale(LightSettings& lightSettings, float delta)
{
	lightSettings.modelScale += delta;
	lightSettings.modelScale = std::clamp(lightSettings.modelScale, 0.1f, 8.75f);
}

void Camera::AddCameraRotation(float dxPixels, float dyPixels)
{
	constexpr float sensitivity = 0.005f;

	m_cameraYaw += dxPixels * sensitivity;
	m_cameraPitch += dyPixels * sensitivity;

	const float limit = DirectX::XM_PIDIV2 - 0.05f;
	m_cameraPitch = std::clamp(m_cameraPitch, -limit, limit);
}

void Camera::UpdateWindowBounds(
	HWND hwnd,
	bool disableAutofitWindow,
	float minx, float miny, float minz, float maxx, float maxy, float maxz,
	const DirectX::XMMATRIX& model, const DirectX::XMMATRIX& view,
	const DirectX::XMMATRIX& /*proj*/)
{
	using namespace DirectX;

	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);

	const int maxW = static_cast<int>(screenW * 0.95f);
	const int maxH = static_cast<int>(screenH * 0.95f);

	const float refFov = XMConvertToRadians(30.0f);
	const float K = 600.0f / std::tan(refFov * 0.5f);
	const float focalPx = K * 0.5f;

	const DirectX::XMFLOAT3 corners[8] = {
		{ minx, miny, minz }, { maxx, miny, minz },
		{ minx, maxy, minz }, { maxx, maxy, minz },
		{ minx, miny, maxz }, { maxx, miny, maxz },
		{ minx, maxy, maxz }, { maxx, maxy, maxz }
	};

	float minRx = std::numeric_limits<float>::max();
	float maxRx = std::numeric_limits<float>::lowest();
	float minRy = std::numeric_limits<float>::max();
	float maxRy = std::numeric_limits<float>::lowest();

	const XMMATRIX MV = model * view;

	for (const auto& c : corners)
	{
		XMVECTOR vView = XMVector3TransformCoord(XMLoadFloat3(&c), MV);

		float z = XMVectorGetZ(vView);
		if (z < 0.1f) z = 0.1f;

		const float rx = XMVectorGetX(vView) / z;
		const float ry = XMVectorGetY(vView) / z;

		minRx = std::min(minRx, rx);
		maxRx = std::max(maxRx, rx);
		minRy = std::min(minRy, ry);
		maxRy = std::max(maxRy, ry);
	}

	if (minRx >= maxRx || minRy >= maxRy)
	{
		m_hasContentRect = false;
		return;
	}

	constexpr float marginPx = 40.0f;
	constexpr float minClientSize = 64.0f;

	auto quantize = [](float val) {
		const float step = 64.0f;
		return std::ceil(val / step) * step;
		};

	const float minU = minRx * focalPx;
	const float maxU = maxRx * focalPx;
	const float minV = minRy * focalPx;
	const float maxV = maxRy * focalPx;

	float desiredClientW = (maxU - minU) + marginPx * 2.0f;
	float desiredClientH = (maxV - minV) + marginPx * 2.0f;
	desiredClientW = quantize(desiredClientW);
	desiredClientH = quantize(desiredClientH);
	desiredClientW = std::clamp(desiredClientW, minClientSize, (float)maxW);
	desiredClientH = std::clamp(desiredClientH, minClientSize, (float)maxH);

	RECT rc = { 0, 0, (LONG)desiredClientW, (LONG)desiredClientH };
	const DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
	const DWORD exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&rc, style, FALSE, exStyle);

	int desiredW = rc.right - rc.left;
	int desiredH = rc.bottom - rc.top;

	desiredW = std::clamp(desiredW, (int)minClientSize, maxW);
	desiredH = std::clamp(desiredH, (int)minClientSize, maxH);

	RECT wnd{};
	GetWindowRect(hwnd, &wnd);
	const int curWidth = wnd.right - wnd.left;
	const int curHeight = wnd.bottom - wnd.top;

#if DCOMP_AUTOFIT_WINDOW
	static int reservedW = 0;
	static int reservedH = 0;

	static uint64_t frameCounter = 0;
	static uint64_t lastResizeFrame = 0;
	++frameCounter;

	if (!disableAutofitWindow)
	{
		if (reservedW == 0) reservedW = curWidth;
		if (reservedH == 0) reservedH = curHeight;

#if DCOMP_AUTOFIT_GROW_ONLY
		desiredW = std::max(desiredW, reservedW);
		desiredH = std::max(desiredH, reservedH);
#endif

		const bool sizeChanged = (std::abs(curWidth - desiredW) >= 32) || (std::abs(curHeight - desiredH) >= 32);
		const bool cooldownOk = (frameCounter - lastResizeFrame) >= DCOMP_AUTOFIT_COOLDOWN_FRAMES;

		if (sizeChanged && cooldownOk)
		{
			SetWindowPos(hwnd, nullptr, 0, 0, desiredW, desiredH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			lastResizeFrame = frameCounter;

#if DCOMP_AUTOFIT_GROW_ONLY
			reservedW = std::max(reservedW, desiredW);
			reservedH = std::max(reservedH, desiredH);
#endif
		}
	}
#endif

	UINT clientW = 0, clientH = 0;
	GetClientSize(hwnd, clientW, clientH);
	if (clientW == 0 || clientH == 0)
	{
		m_hasContentRect = false;
		return;
	}

	const float centerX = clientW * 0.5f;
	const float centerY = clientH * 0.5f;

	m_lastContentRect.left = static_cast<LONG>(centerX + minU);
	m_lastContentRect.right = static_cast<LONG>(centerX + maxU);
	m_lastContentRect.top = static_cast<LONG>(centerY - maxV);
	m_lastContentRect.bottom = static_cast<LONG>(centerY - minV);

	m_hasContentRect = true;
}

void Camera::CacheMatrices(const DirectX::XMMATRIX& model,
						   const DirectX::XMMATRIX& view,
						   const DirectX::XMMATRIX& proj,
						   UINT width,
						   UINT height)
{
	DirectX::XMStoreFloat4x4(&m_lastModelMatrix, model);
	DirectX::XMStoreFloat4x4(&m_lastViewMatrix, view);
	DirectX::XMStoreFloat4x4(&m_lastProjMatrix, proj);
	m_cachedWidth = width;
	m_cachedHeight = height;
	m_matricesValid = true;
}

DirectX::XMFLOAT3 Camera::ProjectToScreen(const DirectX::XMFLOAT3& localPos) const
{
	using namespace DirectX;
	if (!m_matricesValid || m_cachedWidth == 0 || m_cachedHeight == 0)
	{
		return { 0.0f, 0.0f, 0.0f };
	}

	XMMATRIX M = XMLoadFloat4x4(&m_lastModelMatrix);
	XMMATRIX V = XMLoadFloat4x4(&m_lastViewMatrix);
	XMMATRIX P = XMLoadFloat4x4(&m_lastProjMatrix);

	XMVECTOR local = XMLoadFloat3(&localPos);
	local = XMVectorSetW(local, 1.0f);

	XMVECTOR world = XMVector3TransformCoord(local, M);
	XMVECTOR clip = XMVector3Transform(world, V * P);

	float w = XMVectorGetW(clip);
	if (w < 0.001f) w = 0.001f;

	XMVECTOR ndc = XMVectorScale(clip, 1.0f / w);

	float x = (XMVectorGetX(ndc) + 1.0f) * 0.5f * (float)m_cachedWidth;
	float y = (1.0f - XMVectorGetY(ndc)) * 0.5f * (float)m_cachedHeight;

	return { x, y, w };
}

bool Camera::TryGetCachedMatrices(DirectX::XMFLOAT4X4& outModel,
								  DirectX::XMFLOAT4X4& outView,
								  DirectX::XMFLOAT4X4& outProj,
								  UINT& outWidth,
								  UINT& outHeight) const
{
	if (!m_matricesValid || m_cachedWidth == 0 || m_cachedHeight == 0)
	{
		return false;
	}
	outModel = m_lastModelMatrix;
	outView = m_lastViewMatrix;
	outProj = m_lastProjMatrix;
	outWidth = m_cachedWidth;
	outHeight = m_cachedHeight;
	return true;
}

void Camera::InvalidateContentRect()
{
	m_hasContentRect = false;
}

bool Camera::IsPointInContentRect(const POINT& clientPoint) const
{
	return m_hasContentRect && PtInRect(&m_lastContentRect, clientPoint);
}