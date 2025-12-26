#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DcompRenderer.hpp"
#include "d3dx12.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <utility>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace
{
	DXGI_SWAP_CHAIN_DESC1 MakeSwapChainDesc(UINT w, UINT h)
	{
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = w;
		desc.Height = h;
		desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		desc.Stereo = FALSE;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = DcompRenderer::FrameCount;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		return desc;
	}

	void GetClientSize(HWND hwnd, UINT& w, UINT& h)
	{
		RECT rc{};
		GetClientRect(hwnd, &rc);
		w = static_cast<UINT>(rc.right - rc.left);
		h = static_cast<UINT>(rc.bottom - rc.top);
		if (w == 0) w = 1;
		if (h == 0) h = 1;
	}

	UINT64 Align256(UINT64 size)
	{
		return (size + 255ull) & ~255ull;
	}
}

void DcompRenderer::SetLightSettings(const LightSettings& light)
{
	m_lightSettings = light;
	m_pmxDrawer.UpdateMaterialSettings(m_lightSettings); // 設定変更時にマテリアルパラメータも更新
}

void DcompRenderer::SetResizeOverlayEnabled(bool enabled)
{
	m_resizeOverlayEnabled = enabled;
}

void DcompRenderer::AdjustBrightness(float delta)
{
	m_lightSettings.brightness += delta;
	m_lightSettings.brightness = std::clamp(m_lightSettings.brightness, 0.1f, 3.0f);
}

static inline uint8_t To8_10(uint32_t v10)
{
	// 0..1023 -> 0..255（丸め込み）
	return static_cast<uint8_t>((v10 * 255u + 511u) / 1023u);
}
static inline uint8_t To8_2(uint32_t v2)
{
	// 0..3 -> 0..255
	return static_cast<uint8_t>((v2 * 255u + 1u) / 3u);
}

void DcompRenderer::RecreateLayeredBitmap()
{
	if (m_width == 0 || m_height == 0) return;

	if (!m_layeredDc)
	{
		m_layeredDc = CreateCompatibleDC(nullptr);
		if (!m_layeredDc) throw std::runtime_error("CreateCompatibleDC failed.");
	}

	// 古いのを外す
	if (m_layeredBmp)
	{
		if (m_layeredOld)
		{
			SelectObject(m_layeredDc, m_layeredOld);
			m_layeredOld = nullptr;
		}
		DeleteObject(m_layeredBmp);
		m_layeredBmp = nullptr;
		m_layeredBits = nullptr;
	}

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = static_cast<LONG>(m_width);
	bmi.bmiHeader.biHeight = -static_cast<LONG>(m_height); // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection(m_layeredDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) throw std::runtime_error("CreateDIBSection failed.");

	m_layeredOld = SelectObject(m_layeredDc, bmp);
	m_layeredBmp = bmp;
	m_layeredBits = bits;

	// 初期は透明で埋める
	memset(m_layeredBits, 0, static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u);
}

void DcompRenderer::PresentLayered(UINT frameIndex)
{
	if (!m_layeredDc || !m_layeredBmp || !m_layeredBits) return;
	auto* mapped = m_gpuResources.GetReadbackMapped(frameIndex);
	if (!mapped) return;

	// readback は R10G10B10A2（4byte/pixel）。RowPitch は footprint に従う
	const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(mapped);
	const UINT srcPitch = m_gpuResources.GetReadbackFootprint().Footprint.RowPitch;

	uint8_t* dstBase = reinterpret_cast<uint8_t*>(m_layeredBits);
	const UINT dstPitch = static_cast<UINT>(m_width) * 4u;

	for (UINT y = 0; y < m_height; ++y)
	{
		const uint32_t* src = reinterpret_cast<const uint32_t*>(srcBase + y * srcPitch);
		uint32_t* dst = reinterpret_cast<uint32_t*>(dstBase + y * dstPitch);

		for (UINT x = 0; x < m_width; ++x)
		{
			const uint32_t p = src[x];
			const uint32_t r10 = (p >> 0) & 0x3FFu;
			const uint32_t g10 = (p >> 10) & 0x3FFu;
			const uint32_t b10 = (p >> 20) & 0x3FFu;
			const uint32_t a2 = (p >> 30) & 0x3u;

			const uint8_t a8 = To8_2(a2);
			const uint8_t r8 = (a2 == 0) ? 0 : To8_10(r10);
			const uint8_t g8 = (a2 == 0) ? 0 : To8_10(g10);
			const uint8_t b8 = (a2 == 0) ? 0 : To8_10(b10);

			// DIB は BGRA（メモリ上の並び B,G,R,A）
			dst[x] = (uint32_t)b8 | ((uint32_t)g8 << 8) | ((uint32_t)r8 << 16) | ((uint32_t)a8 << 24);
		}
	}

	if (m_resizeOverlayEnabled)
	{
		const int w = static_cast<int>(m_width);
		const int h = static_cast<int>(m_height);
		if (w >= 20 && h >= 20)
		{
			auto premulWhite = [](uint8_t a) -> uint32_t
				{
					// premultiplied BGRA: (a,a,a,a)
					return (uint32_t)a | ((uint32_t)a << 8) | ((uint32_t)a << 16) | ((uint32_t)a << 24);
				};

			uint32_t* buf = reinterpret_cast<uint32_t*>(m_layeredBits);

			const uint32_t cOuter = premulWhite(180);
			const uint32_t cInner = premulWhite(80);
			const uint32_t cHandle = premulWhite(220);

			auto setPix = [&](int x, int y, uint32_t c)
				{
					if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
					buf[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = c;
				};

			// Outer border (1px)
			for (int x = 0; x < w; ++x)
			{
				setPix(x, 0, cOuter);
				setPix(x, h - 1, cOuter);
			}
			for (int y = 0; y < h; ++y)
			{
				setPix(0, y, cOuter);
				setPix(w - 1, y, cOuter);
			}

			// Inner border (1px, inset)
			const int inset = 2;
			for (int x = inset; x < w - inset; ++x)
			{
				setPix(x, inset, cInner);
				setPix(x, h - 1 - inset, cInner);
			}
			for (int y = inset; y < h - inset; ++y)
			{
				setPix(inset, y, cInner);
				setPix(w - 1 - inset, y, cInner);
			}

			// Corner handles (subtle squares)
			const int hs = 10;
			for (int yy = 0; yy < hs; ++yy)
			{
				for (int xx = 0; xx < hs; ++xx)
				{
					setPix(xx, yy, cHandle);
					setPix((w - hs) + xx, yy, cHandle);
					setPix(xx, (h - hs) + yy, cHandle);
					setPix((w - hs) + xx, (h - hs) + yy, cHandle);
				}
			}
		}
	}

	RECT rc{};
	GetWindowRect(m_hwnd, &rc);

	POINT ptDst{ rc.left, rc.top };
	SIZE  size{ (LONG)m_width, (LONG)m_height };
	POINT ptSrc{ 0, 0 };

	BLENDFUNCTION bf{};
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;

	if (!UpdateLayeredWindow(m_hwnd, nullptr, &ptDst, &size, m_layeredDc, &ptSrc, 0, &bf, ULW_ALPHA))
	{
		// 必要なら GetLastError() をログ
	}
}

DcompRenderer::~DcompRenderer()
{
	if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void DcompRenderer::CreateD3D()
{
	m_ctx.Initialize();
}

void DcompRenderer::ReportProgress(float value, const wchar_t* msg)
{
	if (m_progressCallback)
	{
		if (value < 0.0f) value = 0.0f;
		if (value > 1.0f) value = 1.0f;
		m_progressCallback(value, msg);
	}
}

void DcompRenderer::Initialize(HWND hwnd, ProgressCallback progress)
{
	m_hwnd = hwnd;
	m_progressCallback = std::move(progress);

	GetClientSize(m_hwnd, m_width, m_height);

	m_disableAutofitWindow = true;
	ReportProgress(0.05f, L"Direct3D を初期化しています...");
	CreateD3D();

	m_pipeline.Initialize(&m_ctx);
	m_gpuResources.Initialize(&m_ctx, [this]() { WaitForGpu(); }, FrameCount);
	m_pmxDrawer.Initialize(&m_ctx, &m_gpuResources);

	CreateCommandObjects();

	ReportProgress(0.15f, L"コマンドリストを準備しています...");
	m_gpuResources.CreateUploadObjects();

	CreateSwapChain();
	CreateRenderTargets();

	CreateMsaaTargets();
	CreateDepthBuffer();

	m_gpuResources.CreateReadbackBuffers(m_width, m_height);

	CreateIntermediateResources();

	RecreateLayeredBitmap();

	ReportProgress(0.30f, L"テクスチャ用のリソースを初期化しています...");
	m_gpuResources.CreateSrvHeap();
	m_gpuResources.ResetTextureCache();

	m_pipeline.CreatePmxRootSignature();

	ReportProgress(0.55f, L"メインシェーダーをコンパイルしています...");
	m_pipeline.CreatePmxPipeline(m_msaaSampleCount, m_msaaQuality);
	ReportProgress(0.80f, L"輪郭シェーダーをコンパイルしています...");
	m_pipeline.CreateEdgePipeline(m_msaaSampleCount, m_msaaQuality);

	ReportProgress(0.90f, L"FXAAパイプラインを準備しています...");
	m_pipeline.CreateFxaaPipeline();

	CreateSceneBuffers();
	CreateBoneBuffers();

	ReportProgress(1.0f, L"初期化が完了しました。");
}

void DcompRenderer::ReleaseIntermediateResources()
{
	m_intermediateTex = nullptr;
	m_intermediateRtvHeap = nullptr;
	m_intermediateSrvHeap = nullptr;
}

void DcompRenderer::CreateIntermediateResources()
{
	ReleaseIntermediateResources();

	if (m_width == 0 || m_height == 0) return;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R10G10B10A2_UNORM, m_width, m_height,
		1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	clear.Color[0] = 0.f; clear.Color[1] = 0.f; clear.Color[2] = 0.f; clear.Color[3] = 0.f;

	DX_CALL(m_ctx.Device()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
		IID_PPV_ARGS(m_intermediateTex.put())));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_intermediateRtvHeap.put())));
	m_intermediateRtvHandle = m_intermediateRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateRenderTargetView(m_intermediateTex.get(), nullptr, m_intermediateRtvHandle);

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(m_intermediateSrvHeap.put())));

	m_intermediateSrvCpuHandle = m_intermediateSrvHeap->GetCPUDescriptorHandleForHeapStart();
	m_intermediateSrvGpuHandle = m_intermediateSrvHeap->GetGPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	m_ctx.Device()->CreateShaderResourceView(m_intermediateTex.get(), &srvDesc, m_intermediateSrvCpuHandle);
}

void DcompRenderer::CreateSceneBuffers()
{
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(SceneCB)));

	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_ctx.Device()->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(m_sceneCb[i].put())));

		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		DX_CALL(m_sceneCb[i]->Map(0, &range, &mapped));
		m_sceneCbMapped[i] = reinterpret_cast<SceneCB*>(mapped);
		std::memset(m_sceneCbMapped[i], 0, sizeof(SceneCB));
	}
}

void DcompRenderer::CreateBoneBuffers()
{
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(PmxModelDrawer::BoneCB)));

	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_ctx.Device()->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(m_boneCb[i].put())));

		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		DX_CALL(m_boneCb[i]->Map(0, &range, &mapped));
		m_boneCbMapped[i] = reinterpret_cast<PmxModelDrawer::BoneCB*>(mapped);

		for (size_t b = 0; b < PmxModelDrawer::MaxBones; ++b)
		{
			DirectX::XMStoreFloat4x4(&m_boneCbMapped[i]->boneMatrices[b], DirectX::XMMatrixIdentity());
		}
	}
}

void DcompRenderer::CreateCommandObjects()
{
	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_ctx.Device()->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_alloc[i].put())));
	}

	DX_CALL(m_ctx.Device()->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].get(), nullptr,
		IID_PPV_ARGS(m_cmdList.put())));
	m_cmdList->Close();

	DX_CALL(m_ctx.Device()->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
	m_fenceValue = 1;

	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!m_fenceEvent)
	{
		throw std::runtime_error("CreateEvent failed.");
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = FrameCount;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CALL(m_ctx.Device()->CreateDescriptorHeap(
			&heapDesc, IID_PPV_ARGS(m_rtvHeap.put())));
		m_rtvDescriptorSize = m_ctx.Device()->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
		dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvDesc.NumDescriptors = 1;
		dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CALL(m_ctx.Device()->CreateDescriptorHeap(
			&dsvDesc, IID_PPV_ARGS(m_dsvHeap.put())));
	}
}

void DcompRenderer::CreateSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 desc = MakeSwapChainDesc(m_width, m_height);

	DX_CALL(m_ctx.Factory()->CreateSwapChainForComposition(
		m_ctx.Queue(), &desc, nullptr, m_swapChain1.put()));

	DX_CALL(m_swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), m_swapChain.put_void()));
}

void DcompRenderer::CreateRenderTargets()
{
	auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_swapChain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].put())));
		m_ctx.Device()->CreateRenderTargetView(m_renderTargets[i].get(), nullptr, handle);
		handle.ptr += m_rtvDescriptorSize;
	}
}

void DcompRenderer::CreateDComp()
{
	DX_CALL(DCompositionCreateDevice(
		nullptr, IID_PPV_ARGS(m_dcompDevice.put())));

	DX_CALL(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, m_dcompTarget.put()));

	DX_CALL(m_dcompDevice->CreateVisual(m_dcompVisual.put()));

	DX_CALL(m_dcompVisual->SetContent(m_swapChain.get()));

	DX_CALL(m_dcompTarget->SetRoot(m_dcompVisual.get()));

	DX_CALL(m_dcompDevice->Commit());
}

void DcompRenderer::ResizeIfNeeded()
{
	UINT newW{}, newH{};
	GetClientSize(m_hwnd, newW, newH);
	if (newW == m_width && newH == m_height) return;

	WaitForGpu();

	m_width = newW;
	m_height = newH;

	for (UINT i = 0; i < FrameCount; ++i)
	{
		m_renderTargets[i] = nullptr;
	}

	m_depth = nullptr;

	DX_CALL(m_swapChain->ResizeBuffers(
		FrameCount, m_width, m_height, DXGI_FORMAT_R10G10B10A2_UNORM, 0));

	CreateRenderTargets();
	CreateMsaaTargets();
	CreateDepthBuffer();

	m_gpuResources.CreateReadbackBuffers(m_width, m_height);

	CreateIntermediateResources();

	RecreateLayeredBitmap();
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

void DcompRenderer::Render(const MmdAnimator& animator)
{
	const auto* model = animator.Model();
	if (!model) return;

	m_pmxDrawer.EnsurePmxResources(model, m_lightSettings);
	if (!m_pmxDrawer.IsReady())
	{
		m_camera.InvalidateContentRect();
		return;
	}

	const auto& pmx = m_pmxDrawer.GetPmx();

	// 1. バウンディングボックスの取得
	float minx, miny, minz, maxx, maxy, maxz;
	animator.GetBounds(minx, miny, minz, maxx, maxy, maxz);

	// 足元スナップ計算用に、余白を含まない純粋な下端(Y)を保持しておく
	const float rawMinY = miny;

	// 自動リサイズ等の計算用に余白(margin)を含めた値を計算
	const float margin = 3.0f;
	minx -= margin; miny -= margin; minz -= margin;
	maxx += margin; maxy += margin; maxz += margin;

	// マージン込みの中心とサイズ
	const float cx = (minx + maxx) * 0.5f;
	const float cy = (miny + maxy) * 0.5f;
	const float cz = (minz + maxz) * 0.5f;
	const float sx = (maxx - minx);
	const float sy = (maxy - miny);
	const float sz = (maxz - minz);
	const float size = std::max({ sx, sy, sz, 1.0f });

	// まずリサイズを反映
	ResizeIfNeeded();
	if (!m_intermediateTex || !m_intermediateRtvHeap || !m_intermediateSrvHeap)
	{
		CreateIntermediateResources();
	}

	using namespace DirectX;
	const float scale = (1.0f / size) * m_lightSettings.modelScale;
	const XMMATRIX motionTransform = DirectX::XMLoadFloat4x4(&animator.MotionTransform());

	// モデルトラッキング行列: 中心を原点に移動し、スケール、モーションを適用
	const XMMATRIX M_track =
		XMMatrixTranslation(-cx, -cy, -cz) *
		XMMatrixScaling(scale, scale, scale) *
		motionTransform;

	const float baseDistance = 2.5f;
	const float cameraYaw = m_camera.GetYaw();
	const float cameraPitch = m_camera.GetPitch();
	const float cameraDistance = m_camera.GetDistance();
	const float distance = std::max(0.1f, baseDistance * cameraDistance);
	const float cosPitch = std::cos(cameraPitch);

	XMVECTOR eyeOffset = XMVectorSet(
		distance * std::sin(cameraYaw) * cosPitch,
		distance * std::sin(cameraPitch),
		-distance * std::cos(cameraYaw) * cosPitch,
		0.0f
	);

	XMVECTOR target = XMVector3TransformCoord(XMVectorZero(), M_track);
	XMVECTOR eye = XMVectorAdd(target, eyeOffset);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(eye, target, up);

	// カメラ上方向（ワールド）を取得
	XMMATRIX invV = XMMatrixInverse(nullptr, V);
	// View空間の(0,1,0)をワールドへ変換して、移動方向を厳密に一致させる（Yawでの微小な上下ズレ対策）
	XMVECTOR upWorld = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), invV));

	// ウインドウサイズとモデルの見かけサイズ（ピクセル）を独立にするため、
	// ピクセル単位の焦点距離を一定に保つ（=ウインドウサイズに応じてFOVが変化する）。
	const float refFov = XMConvertToRadians(30.0f);
	const float K = 600.0f / std::tan(refFov * 0.5f); // tan(fov/2)=H/K
	const float h = (m_height > 0) ? (float)m_height : 600.0f;
	const float tanHalfFov = h / K;
	float fovY = 2.0f * std::atan(tanHalfFov);
	fovY = std::clamp(fovY, XMConvertToRadians(10.0f), XMConvertToRadians(100.0f));

	const float aspect = (m_height > 0) ? (float)m_width / (float)m_height : 1.0f;
	XMMATRIX P = XMMatrixPerspectiveFovLH(fovY, aspect, 0.1f, 100.0f);

	float snapT = 0.0f;
	{
		float footOffsetY = (rawMinY - cy) * scale;
		// Track空間での足位置。X, Zは0（中心）とする。
		XMVECTOR footPosTrack = XMVectorSet(0.0f, footOffsetY, 0.0f, 1.0f);

		// ユーザーの移動操作 (m_modelOffset) を加算
		footPosTrack = XMVectorAdd(footPosTrack, XMVectorSet(m_modelOffset.x, m_modelOffset.y, 0.0f, 0.0f));

		// View行列を掛けて、カメラから見た足の位置(View Space)を計算
		XMVECTOR footPosView = XMVector3TransformCoord(footPosTrack, V);

		float currentY = XMVectorGetY(footPosView);
		float currentZ = XMVectorGetZ(footPosView);

		/*
			画面下端に足を合わせるが、モデルサイズやポスト処理などで見切れやすいので、
			下側に一定のピクセルマージンを確保する。
			(ウインドウリサイズで見かけサイズを保つ設計のため、px基準のマージンが扱いやすい)
		*/
		const float bottomMarginPx = std::clamp(h * 0.10f, 16.0f, 128.0f);
		// y_ndc = -1 + 2*margin/h を満たす targetY を求める。
		// tanHalfFov = h/K なので、targetY = -z*tanHalfFov + z*(2*marginPx/K)
		float targetY = -currentZ * tanHalfFov + currentZ * (2.0f * bottomMarginPx / K);

		snapT = targetY - currentY;
	}

	// レンダリング用モデル行列
	// snapT はカメラの上方向(upWorld)に沿って移動させることで、View空間でのY移動を実現する
	XMMATRIX M =
		M_track *
		XMMatrixTranslationFromVector(XMVectorScale(upWorld, snapT)) *
		XMMatrixTranslation(m_modelOffset.x, m_modelOffset.y, 0.0f);

	m_camera.UpdateWindowBounds(m_hwnd, m_disableAutofitWindow, minx, miny, minz, maxx, maxy, maxz, M, V, P);
	ResizeIfNeeded();

	m_camera.CacheMatrices(M, V, P, m_width, m_height);

	const UINT frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	WaitForFrame(frameIndex);

	if (!m_intermediateTex || !m_intermediateRtvHeap || !m_intermediateSrvHeap)
	{
		CreateIntermediateResources();
	}
	if (!m_dsvHeap || !m_depth || !m_intermediateTex || !m_intermediateRtvHeap || !m_intermediateSrvHeap) return;

	// -------------------------------------------------------------
	// モーフの更新処理 (頂点変形 & マテリアル更新)
	// -------------------------------------------------------------
	m_pmxDrawer.UpdatePmxMorphs(animator);
	// -------------------------------------------------------------

	// マテリアル設定（影の濃さなど）を再適用
	m_pmxDrawer.UpdateMaterialSettings(m_lightSettings);

	m_alloc[frameIndex]->Reset();
	m_cmdList->Reset(m_alloc[frameIndex].get(), nullptr);

	m_pmxDrawer.UpdateBoneMatrices(animator, m_boneCbMapped[frameIndex]);

	const bool useMsaa = (m_msaaSampleCount > 1) && m_msaaColor;

	static D3D12_RESOURCE_STATES s_interState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	D3D12_RESOURCE_DESC interDesc = m_intermediateTex->GetDesc();
	if (interDesc.Width != m_width || interDesc.Height != m_height)
	{
		s_interState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	if (useMsaa)
	{
		if (m_msaaColorState != D3D12_RESOURCE_STATE_RENDER_TARGET)
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_msaaColor.get(), m_msaaColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_cmdList->ResourceBarrier(1, &barrier);
			m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
	}
	else
	{
		if (s_interState != D3D12_RESOURCE_STATE_RENDER_TARGET)
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_intermediateTex.get(), s_interState, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_cmdList->ResourceBarrier(1, &barrier);
			s_interState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = useMsaa ? m_msaaRtvHandle : m_intermediateRtvHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

	m_cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	const float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT vp{};
	vp.Width = static_cast<float>(m_width);
	vp.Height = static_cast<float>(m_height);
	vp.MaxDepth = 1.0f;
	D3D12_RECT sc{};
	sc.right = static_cast<LONG>(m_width);
	sc.bottom = static_cast<LONG>(m_height);

	m_cmdList->RSSetViewports(1, &vp);
	m_cmdList->RSSetScissorRects(1, &sc);

	SceneCB* scene = m_sceneCbMapped[frameIndex];
	if (scene)
	{
		DirectX::XMVECTOR keyDirV = DirectX::XMVector3Normalize(
			DirectX::XMVectorSet(m_lightSettings.keyLightDirX, m_lightSettings.keyLightDirY, m_lightSettings.keyLightDirZ, 0.0f));
		DirectX::XMVECTOR fillDirV = DirectX::XMVector3Normalize(
			DirectX::XMVectorSet(m_lightSettings.fillLightDirX, m_lightSettings.fillLightDirY, m_lightSettings.fillLightDirZ, 0.0f));

		XMStoreFloat3(&scene->lightDir0, keyDirV);
		scene->ambient = m_lightSettings.ambientStrength;
		scene->lightColor0 = { m_lightSettings.keyLightColorR, m_lightSettings.keyLightColorG, m_lightSettings.keyLightColorB };
		scene->lightInt0 = m_lightSettings.keyLightIntensity;

		XMStoreFloat3(&scene->lightDir1, fillDirV);
		scene->lightColor1 = { m_lightSettings.fillLightColorR, m_lightSettings.fillLightColorG, m_lightSettings.fillLightColorB };
		scene->lightInt1 = m_lightSettings.fillLightIntensity;

		scene->specPower = 48.0f;
		scene->specColor = { 1.0f, 1.0f, 1.0f };
		scene->specStrength = 0.18f;
		scene->brightness = m_lightSettings.brightness;
		scene->toonContrast = m_lightSettings.toonContrast;
		scene->shadowHueShift = m_lightSettings.shadowHueShiftDeg * (XM_PI / 180.0f);
		scene->outlineRefDistance = distance;
		scene->outlineDistanceScale = 1.0f;
		scene->outlineDistancePower = 0.8f;
		scene->shadowRampShift = m_lightSettings.shadowRampShift;
		scene->shadowDeepThreshold = m_lightSettings.shadowDeepThreshold;
		scene->shadowDeepSoftness = m_lightSettings.shadowDeepSoftness;
		scene->shadowDeepMul = m_lightSettings.shadowDeepMul;
		scene->globalSaturation = m_lightSettings.globalSaturation;
		scene->shadowSaturation = m_lightSettings.shadowSaturationBoost;
		scene->rimWidth = m_lightSettings.rimWidth;
		scene->rimIntensity = m_lightSettings.rimIntensity;
		scene->specularStep = m_lightSettings.specularStep;
		scene->enableToon = m_lightSettings.toonEnabled ? 1u : 0u;
		scene->enableSkinning = animator.HasSkinnedPose() ? 1 : 0;
		XMStoreFloat3(&scene->cameraPos, eye);
		XMMATRIX MVP = M * V * P;
		XMStoreFloat4x4(&scene->model, XMMatrixTranspose(M));
		XMStoreFloat4x4(&scene->view, XMMatrixTranspose(V));
		XMStoreFloat4x4(&scene->proj, XMMatrixTranspose(P));
		XMStoreFloat4x4(&scene->mvp, XMMatrixTranspose(MVP));

		XMMATRIX normalMat = XMMatrixTranspose(XMMatrixInverse(nullptr, M));
		XMFLOAT4X4 normalMat4x4;
		XMStoreFloat4x4(&normalMat4x4, normalMat);
		scene->normalMatrixRow0 = { normalMat4x4._11, normalMat4x4._12, normalMat4x4._13, 0.0f };
		scene->normalMatrixRow1 = { normalMat4x4._21, normalMat4x4._22, normalMat4x4._23, 0.0f };
		scene->normalMatrixRow2 = { normalMat4x4._31, normalMat4x4._32, normalMat4x4._33, 0.0f };
	}

	auto srvHeap = m_gpuResources.GetSrvHeap();
	if (srvHeap)
	{
		ID3D12DescriptorHeap* heaps[] = { srvHeap };
		m_cmdList->SetDescriptorHeaps(1, heaps);

		m_cmdList->SetGraphicsRootSignature(m_pipeline.GetPmxRootSignature());
		m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdList->IASetVertexBuffers(0, 1, &pmx.vbv);
		m_cmdList->IASetIndexBuffer(&pmx.ibv);

		m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb[frameIndex]->GetGPUVirtualAddress());
		m_cmdList->SetGraphicsRootConstantBufferView(3, m_boneCb[frameIndex]->GetGPUVirtualAddress());

		std::vector<size_t> opaque, trans;
		opaque.reserve(pmx.materials.size());
		trans.reserve(pmx.materials.size());
		for (size_t i = 0; i < pmx.materials.size(); ++i)
		{
			if (pmx.materials[i].mat.diffuse[3] < 0.999f) trans.push_back(i);
			else opaque.push_back(i);
		}

		auto DrawMats = [&](const std::vector<size_t>& indices) {
			for (auto i : indices)
			{
				const auto& gm = pmx.materials[i];
				m_cmdList->SetGraphicsRootConstantBufferView(1, gm.materialCbGpu);
				m_cmdList->SetGraphicsRootDescriptorTable(2, m_gpuResources.GetSrvGpuHandle(gm.srvBlockIndex));
				if (gm.mat.indexCount > 0)
					m_cmdList->DrawIndexedInstanced((UINT)gm.mat.indexCount, 1, (UINT)gm.mat.indexOffset, 0, 0);
			}
			};

		m_cmdList->SetPipelineState(m_pipeline.GetPmxPsoOpaque());
		DrawMats(opaque);

		m_cmdList->SetPipelineState(m_pipeline.GetPmxPsoTrans());
		DrawMats(trans);

		const auto* materialCb = m_pmxDrawer.GetMaterialCbMapped();
		const auto materialStride = m_pmxDrawer.GetMaterialCbStride();

		m_cmdList->SetPipelineState(m_pipeline.GetEdgePso());
		m_cmdList->SetGraphicsRootSignature(m_pipeline.GetPmxRootSignature());
		m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdList->IASetVertexBuffers(0, 1, &pmx.vbv);
		m_cmdList->IASetIndexBuffer(&pmx.ibv);

		m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb[frameIndex]->GetGPUVirtualAddress());
		m_cmdList->SetGraphicsRootConstantBufferView(3, m_boneCb[frameIndex]->GetGPUVirtualAddress());

		for (size_t i = 0; i < pmx.materials.size(); ++i)
		{
			const auto& gm = pmx.materials[i];
			const auto* cb = reinterpret_cast<const PmxModelDrawer::MaterialCB*>(materialCb + i * materialStride);

			if (cb->edgeSize <= 0.0f || cb->edgeColor.w <= 0.001f) continue;

			m_cmdList->SetGraphicsRootConstantBufferView(1, gm.materialCbGpu);
			m_cmdList->SetGraphicsRootDescriptorTable(2, m_gpuResources.GetSrvGpuHandle(gm.srvBlockIndex));
			if (gm.mat.indexCount > 0)
				m_cmdList->DrawIndexedInstanced((UINT)gm.mat.indexCount, 1, (UINT)gm.mat.indexOffset, 0, 0);
		}
	}

	if (useMsaa)
	{
		auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_msaaColor.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_intermediateTex.get(), s_interState, D3D12_RESOURCE_STATE_RESOLVE_DEST);

		D3D12_RESOURCE_BARRIER barriers[] = { b1, b2 };
		m_cmdList->ResourceBarrier(2, barriers);

		m_cmdList->ResolveSubresource(
			m_intermediateTex.get(), 0, m_msaaColor.get(), 0, DXGI_FORMAT_R10G10B10A2_UNORM);

		m_msaaColorState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		s_interState = D3D12_RESOURCE_STATE_RESOLVE_DEST;

		auto b3 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_msaaColor.get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_cmdList->ResourceBarrier(1, &b3);
		m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	{
		D3D12_RESOURCE_BARRIER barriers[2];
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_intermediateTex.get(), s_interState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		m_cmdList->ResourceBarrier(2, barriers);
		s_interState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	auto backBufferRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	backBufferRtv.ptr += (SIZE_T)frameIndex * m_rtvDescriptorSize;
	m_cmdList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);

	ID3D12DescriptorHeap* fxaaHeaps[] = { m_intermediateSrvHeap.get() };
	m_cmdList->SetDescriptorHeaps(1, fxaaHeaps);

	m_cmdList->SetGraphicsRootSignature(m_pipeline.GetFxaaRootSignature());
	m_cmdList->SetPipelineState(m_pipeline.GetFxaaPso());
	m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float consts[2] = { 1.0f / (float)m_width, 1.0f / (float)m_height };
	m_cmdList->SetGraphicsRoot32BitConstants(0, 2, consts, 0);

	m_cmdList->SetGraphicsRootDescriptorTable(1, m_intermediateSrvGpuHandle);

	m_cmdList->DrawInstanced(3, 1, 0, 0);

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_cmdList->ResourceBarrier(1, &barrier);
	}

	if (auto* readbackBuffer = m_gpuResources.GetReadbackBuffer(frameIndex))
	{
		auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_cmdList->ResourceBarrier(1, &b1);

		CD3DX12_TEXTURE_COPY_LOCATION dst(readbackBuffer, m_gpuResources.GetReadbackFootprint());
		CD3DX12_TEXTURE_COPY_LOCATION src(m_renderTargets[frameIndex].get(), 0);

		m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
		m_cmdList->ResourceBarrier(1, &b2);
	}

	m_cmdList->Close();
	ID3D12CommandList* lists[] = { m_cmdList.get() };
	m_ctx.Queue()->ExecuteCommandLists(1, lists);

	m_swapChain->Present(1, 0);

	const UINT64 signalValue = m_fenceValue++;
	m_ctx.Queue()->Signal(m_fence.get(), signalValue);
	m_frameFenceValues[frameIndex] = signalValue;

	WaitForFrame(frameIndex);
	PresentLayered(frameIndex);
}

void DcompRenderer::WaitForGpu()
{
	const UINT64 fenceToWait = m_fenceValue++;
	m_ctx.Queue()->Signal(m_fence.get(), fenceToWait);

	if (m_fence->GetCompletedValue() < fenceToWait)
	{
		m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void DcompRenderer::CreateDepthBuffer()
{
	if (m_width == 0 || m_height == 0) return;
	if (!m_dsvHeap)
	{
		throw std::runtime_error("DSV heap not created.");
	}

	m_depth = nullptr;

	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_D32_FLOAT;
	clear.DepthStencil.Depth = 1.0f;
	clear.DepthStencil.Stencil = 0;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_D32_FLOAT,
		m_width, m_height,
		1, 1,
		m_msaaSampleCount, m_msaaQuality,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);

	DX_CALL(m_ctx.Device()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clear, IID_PPV_ARGS(m_depth.put())));

	m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateDepthStencilView(m_depth.get(), nullptr, m_dsvHandle);
}

void DcompRenderer::WaitForFrame(UINT frameIndex)
{
	const UINT64 fenceValue = m_frameFenceValues[frameIndex];
	if (fenceValue == 0) return;

	if (m_fence->GetCompletedValue() < fenceValue)
	{
		DX_CALL(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void DcompRenderer::UpdateMsaaSettings()
{
	SelectMaximumMsaa();
}

void DcompRenderer::ReleaseMsaaTargets()
{
	m_msaaColor = nullptr;
	m_msaaRtvHeap = nullptr;
	m_depth = nullptr;
}

void DcompRenderer::CreateMsaaTargets()
{
	SelectMaximumMsaa();
	CreateDepthBuffer();

	if (m_msaaSampleCount <= 1)
	{
		m_msaaColor = nullptr;
		m_msaaRtvHeap = nullptr;
		m_msaaColorState = D3D12_RESOURCE_STATE_COMMON;
		return;
	}

	// Create MSAA color target
	m_msaaColor = nullptr;
	m_msaaRtvHeap = nullptr;

	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	clear.Color[0] = 0.f;
	clear.Color[1] = 0.f;
	clear.Color[2] = 0.f;
	clear.Color[3] = 0.f;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R10G10B10A2_UNORM,
		m_width, m_height,
		1, 1,
		m_msaaSampleCount, m_msaaQuality,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	DX_CALL(m_ctx.Device()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clear, IID_PPV_ARGS(m_msaaColor.put())));

	m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// RTV heap for MSAA color
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = 1;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(
		&rtvDesc, IID_PPV_ARGS(m_msaaRtvHeap.put())));

	m_msaaRtvHandle = m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateRenderTargetView(
		m_msaaColor.get(), nullptr, m_msaaRtvHandle);
}

void DcompRenderer::SelectMaximumMsaa()
{
	const DXGI_FORMAT fmt = DXGI_FORMAT_R10G10B10A2_UNORM;

	m_msaaSampleCount = 1;
	m_msaaQuality = 0;

	const UINT candidates[] = { 32, 16, 8, 4, 2 };
	for (UINT count : candidates)
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
		levels.Format = fmt;
		levels.SampleCount = count;
		levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

		if (SUCCEEDED(m_ctx.Device()->CheckFeatureSupport(
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&levels, sizeof(levels))) &&
			levels.NumQualityLevels > 0)
		{

			m_msaaSampleCount = count;
			m_msaaQuality = levels.NumQualityLevels - 1;
			break;
		}
	}
}

void DcompRenderer::AdjustScale(float delta)
{
	m_camera.AdjustScale(m_lightSettings, delta);
}

void DcompRenderer::AddCameraRotation(float dxPixels, float dyPixels)
{
	m_camera.AddCameraRotation(dxPixels, dyPixels);
}

void DcompRenderer::AddModelOffsetPixels(float dxPixels, float dyPixels)
{
	// 画面上のドラッグ量（ピクセル）を、モデル座標系の平行移動に変換する。
	// モデルは Render() 内でスケール済み（だいたい画面内に収まる大きさ）なので、
	// ここでは「ウィンドウ短辺 = 1.0」程度の感度にしておく。
	const float w = (float)std::max<UINT>(1, m_width);
	const float h = (float)std::max<UINT>(1, m_height);
	const float denom = (std::min)(w, h);
	const float base = 1.0f / denom;
	const float invScale = 1.0f / std::max(0.001f, m_lightSettings.modelScale);

	m_modelOffset.x += dxPixels * base * invScale;
	// 画面Yは下向きが正なので反転
	m_modelOffset.y -= dyPixels * base * invScale;
}

bool DcompRenderer::IsPointOnModel(const POINT& clientPoint)
{
	// 1. まずバウンディングボックスで大まかに判定 (負荷軽減)
	if (!m_camera.IsPointInContentRect(clientPoint))
	{
		return false;
	}

	// 2. 座標チェック
	if (clientPoint.x < 0 || clientPoint.x >= static_cast<LONG>(m_width) ||
		clientPoint.y < 0 || clientPoint.y >= static_cast<LONG>(m_height))
	{
		return false;
	}

	// 3. 最新の「完了済み」フレームを探す
	// ※ 現在描画中のフレームはGPUで使用中のため読めません。
	//    Fenceを確認して、CPU側から安全に読める最新のフレームを探します。

	UINT bestFrame = UINT_MAX;
	UINT64 maxFence = 0;
	const UINT64 completedValue = m_fence->GetCompletedValue();

	for (UINT i = 0; i < FrameCount; ++i)
	{
		UINT64 fv = m_frameFenceValues[i];
		if (fv > 0 && fv <= completedValue)
		{
			// 完了済みの中で一番新しいもの
			if (fv > maxFence)
			{
				maxFence = fv;
				bestFrame = i;
			}
		}
	}

	// まだ1フレームも完了していない場合などは、安全のため「ヒット」扱いにする
	auto* mapped = (bestFrame == UINT_MAX) ? nullptr : m_gpuResources.GetReadbackMapped(bestFrame);
	if (bestFrame == UINT_MAX || !mapped)
	{
		return true;
	}

	// 4. ピクセル読み取り
	// R10G10B10A2 フォーマット: 32bit整数
	// Alphaは上位2ビット (30-31ビット目)

	const uint8_t* basePtr = static_cast<const uint8_t*>(mapped);
	const uint32_t pitch = m_gpuResources.GetReadbackFootprint().Footprint.RowPitch;

	// オフセット計算: y * pitch + x * 4bytes
	const uint64_t offset = static_cast<uint64_t>(clientPoint.y) * pitch + static_cast<uint64_t>(clientPoint.x) * 4;

	if (offset >= m_gpuResources.GetReadbackTotalSize()) return false;

	const uint32_t pixel = *reinterpret_cast<const uint32_t*>(basePtr + offset);

	// A2チャネルを取り出す (0, 1, 2, 3 のいずれか)
	const uint32_t alpha = (pixel >> 30) & 0x3;

	// アルファが0 (完全透明) なら透過、それ以外(1,2,3)ならヒット
	return (alpha != 0);
}

void DcompRenderer::LoadTexturesForModel(const PmxModel* model,
										 std::function<void(float, const wchar_t*)> onProgress,
										 float startProgress, float endProgress)
{
	if (!model) return;

	const auto& texPaths = model->TexturePaths();
	size_t total = texPaths.size();
	if (total == 0) return;

	m_gpuResources.CreateUploadObjects();

	for (size_t i = 0; i < total; ++i)
	{
		if (onProgress && (i % 5 == 0 || i == total - 1))
		{
			float ratio = (float)i / (float)total;
			float current = startProgress + ratio * (endProgress - startProgress);

			auto buf = std::format(L"テクスチャ読み込み中 ({}/{})...", i + 1, total);
			onProgress(current, buf.c_str());
		}

		m_gpuResources.LoadTextureSrv(texPaths[i]);
	}
}

DirectX::XMFLOAT3 DcompRenderer::ProjectToScreen(const DirectX::XMFLOAT3& localPos) const
{
	return m_camera.ProjectToScreen(localPos);
}

bool DcompRenderer::TryGetCachedMatrices(DirectX::XMFLOAT4X4& outModel,
										 DirectX::XMFLOAT4X4& outView,
										 DirectX::XMFLOAT4X4& outProj,
										 UINT& outWidth,
										 UINT& outHeight) const
{
	return m_camera.TryGetCachedMatrices(outModel, outView, outProj, outWidth, outHeight);
}
