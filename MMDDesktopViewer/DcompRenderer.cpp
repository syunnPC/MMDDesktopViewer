#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "DcompRenderer.hpp"
#include "d3dx12.hpp"
#include "ExceptionHelper.hpp"
#include "FileUtil.hpp"
#include "DebugUtil.hpp"
#include <d3dcompiler.h>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cwctype>
#include <optional>
#include <cstring>
#include <utility>
#include <array>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
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

	bool IsTransparentMaterial(const PmxModel::Material& m)
	{
		return m.diffuse[3] < 0.999f;
	}

	std::wstring ToLowerW(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
					   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return s;
	}

	// 複数のキーワードのいずれかを含むか判定
	static bool ContainsAnyW(const std::wstring& hay, std::initializer_list<const wchar_t*> needles)
	{
		const std::wstring low = ToLowerW(hay);
		for (auto n : needles)
		{
			if (low.find(n) != std::wstring::npos) return true;
		}
		return false;
	}

	bool ContainsI(const std::wstring& hay, const std::wstring& needle)
	{
		if (needle.empty()) return false;
		return ToLowerW(hay).find(ToLowerW(needle)) != std::wstring::npos;
	}

	int TryParseTypeTag(const std::wstring& memo)
	{
		if (memo.empty()) return -1;
		const auto m = ToLowerW(memo);
		auto has = [&](const wchar_t* s) { return m.find(s) != std::wstring::npos; };

		if (has(L"type=face") || has(L"type:face") || has(L"#face")) return 3;
		if (has(L"type=eye") || has(L"type:eye") || has(L"#eye"))  return 4;
		if (has(L"type=skin") || has(L"type:skin") || has(L"#skin")) return 1;
		if (has(L"type=hair") || has(L"type:hair") || has(L"#hair")) return 2;
		if (has(L"type=glass") || has(L"type:glass") || has(L"#glass")) return 5;
		return -1;
	}

	uint32_t GuessMaterialType(const PmxModel::Material& mat)
	{
		if (int t = TryParseTypeTag(mat.memo); t >= 0)
			return static_cast<uint32_t>(t);

		const std::wstring n = mat.name;
		const std::wstring ne = mat.nameEn;

		if (ContainsAnyW(n, { L"目", L"瞳", L"eye", L"iris", L"pupil" }) ||
			ContainsAnyW(ne, { L"eye", L"iris" })) return 4;

		if (ContainsAnyW(n, { L"顔", L"face", L"頬", L"ほほ" }) ||
			ContainsAnyW(ne, { L"face", L"cheek" })) return 3;

		if (ContainsAnyW(n, { L"髪", L"hair", L"ヘア" }) ||
			ContainsAnyW(ne, { L"hair" })) return 2;

		if (ContainsAnyW(n, { L"肌", L"skin" }) ||
			ContainsAnyW(ne, { L"skin" })) return 1;

		if (mat.diffuse[3] < 0.98f ||
			ContainsAnyW(n, { L"glass", L"透明" }) ||
			ContainsAnyW(ne, { L"glass", L"transparent" })) return 5;

		const float avg = (mat.diffuse[0] + mat.diffuse[1] + mat.diffuse[2]) / 3.0f;
		if (mat.specularPower >= 80.0f) return 2;
		if (avg >= 0.55f && mat.specularPower <= 25.0f) return 1;

		return 0;
	}

	bool LooksLikeFaceMaterial(const PmxModel::Material& m)
	{
		const std::wstring all = m.name + L" " + m.nameEn + L" " + m.memo;
		const std::wstring low = ToLowerW(all);

		// 英語
		if (low.find(L"face") != std::wstring::npos) return true;
		if (low.find(L"facial") != std::wstring::npos) return true;

		// 日本語
		if (all.find(L"顔") != std::wstring::npos) return true;
		if (all.find(L"かお") != std::wstring::npos) return true;
		if (all.find(L"頭部") != std::wstring::npos) return true;

		return false;
	}

	void GetMaterialStyleParams(
		uint32_t type,
		float& outRimMul,
		float& outSpecMul,
		float& outShadowMul,
		float& outToonContrastMul)
	{
		outRimMul = 1.0f; outSpecMul = 1.0f; outShadowMul = 1.0f; outToonContrastMul = 1.0f;

		switch (type)
		{
			case 3:
				outRimMul = 0.55f; outSpecMul = 0.35f; outShadowMul = 0.60f; outToonContrastMul = 0.85f; break;
			case 1:
				outRimMul = 0.65f; outSpecMul = 0.45f; outShadowMul = 0.70f; outToonContrastMul = 0.90f; break;
			case 2:
				outRimMul = 1.00f; outSpecMul = 1.35f; outShadowMul = 1.00f; outToonContrastMul = 1.05f; break;
			case 4:
				outRimMul = 0.20f; outSpecMul = 1.20f; outShadowMul = 0.85f; outToonContrastMul = 1.00f; break;
			case 5:
				outRimMul = 1.10f; outSpecMul = 1.00f; outShadowMul = 1.00f; outToonContrastMul = 1.00f; break;
		}
	}

	static bool IsEyeOrLashMaterial(
		const PmxModel::Material& m,
		const std::vector<std::filesystem::path>& texPaths)
	{
		if (ContainsAnyW(m.name + L" " + m.nameEn + L" " + m.memo,
						 { L"eye", L"iris", L"pupil", L"eyeball", L"lash", L"eyelash", L"eyeline",
						   L"hitomi", L"matsuge", L"matuge", L"目", L"瞳", L"白目", L"虹彩",
						   L"まつ毛", L"まつげ", L"睫毛", L"アイライン" }))
		{
			return true;
		}

		auto checkIdx = [&](int32_t idx) -> bool {
			if (idx < 0 || idx >= static_cast<int32_t>(texPaths.size())) return false;
			const std::wstring file = texPaths[idx].filename().wstring();
			return ContainsAnyW(file,
								{ L"eye", L"iris", L"pupil", L"eyeball", L"lash", L"eyelash", L"white", L"hitomi" });
			};

		if (checkIdx(m.textureIndex)) return true;
		if (m.toonFlag == 0 && checkIdx(m.toonIndex)) return true;
		if (checkIdx(m.sphereTextureIndex)) return true;

		return false;
	}
}

void DcompRenderer::SetLightSettings(const LightSettings& light)
{
	m_lightSettings = light;
	UpdateMaterialSettings(); // 設定変更時にマテリアルパラメータも更新
}

void DcompRenderer::SetResizeOverlayEnabled(bool enabled)
{
	m_resizeOverlayEnabled = enabled;
	// ここで自動フィット状態を切り替えると「ウインドウサイズとモデルサイズが連動」して見えるため、
	// オーバーレイの表示/非表示とは独立に扱う。
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
	if (!m_readbackMapped[frameIndex]) return;

	// readback は R10G10B10A2（4byte/pixel）。RowPitch は footprint に従う
	const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(m_readbackMapped[frameIndex]);
	const UINT srcPitch = m_readbackFootprint.Footprint.RowPitch;

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

void DcompRenderer::UpdateMaterialSettings()
{
	if (!m_pmx.ready || !m_materialCbMapped) return;

	for (size_t mi = 0; mi < m_pmx.materials.size(); ++mi)
	{
		auto& gm = m_pmx.materials[mi];

		uint32_t matType = GuessMaterialType(gm.mat);
		float rimMul, specMul, shadowMul, toonContrastMul;
		GetMaterialStyleParams(matType, rimMul, specMul, shadowMul, toonContrastMul);

		// 顔と判定された場合は設定値を優先
		if (LooksLikeFaceMaterial(gm.mat))
		{
			shadowMul = m_lightSettings.faceShadowMul;
			toonContrastMul = m_lightSettings.faceToonContrastMul;
		}

		// マッピング済みバッファを直接書き換え
		MaterialCB* mcb = reinterpret_cast<MaterialCB*>(m_materialCbMapped + mi * m_materialCbStride);
		mcb->shadowMul = shadowMul;
		mcb->toonContrastMul = toonContrastMul;
		// 必要に応じて他のパラメータもここで更新可能です
	}
}

uint32_t DcompRenderer::CreateDefaultToonRamp()
{
	std::vector<uint8_t> rgba(256 * 4);

	for (int x = 0; x < 256; ++x)
	{
		float t = x / 255.0f;

		float v;
		if (t < 0.25f)      v = 0.15f;
		else if (t < 0.60f) v = 0.45f;
		else if (t < 0.85f) v = 0.78f;
		else                v = 1.00f;

		uint8_t c = static_cast<uint8_t>(v * 255.0f);
		rgba[x * 4 + 0] = c;
		rgba[x * 4 + 1] = c;
		rgba[x * 4 + 2] = c;
		rgba[x * 4 + 3] = 255;
	}

	const uint32_t srvIndex = AllocSrvIndex();

	auto tex = CreateTexture2DFromRgba(rgba.data(), 256, 1);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;

	m_ctx.Device()->CreateShaderResourceView(tex.Get(), &srv, GetSrvCpuHandle(srvIndex));

	m_textures.push_back(GpuTexture{ tex, srvIndex, 256, 1 });

	return srvIndex;
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
	m_baseClientWidth = static_cast<float>(m_width);
	m_baseClientHeight = static_cast<float>(m_height);


	// ウインドウサイズとモデルサイズを独立に扱うため、起動時は自動フィットを無効化。
	// （必要なら UI/ホットキーで再度有効化してください）
	m_disableAutofitWindow = true;
	ReportProgress(0.05f, L"Direct3D を初期化しています...");
	CreateD3D();

	CreateCommandObjects();

	ReportProgress(0.15f, L"コマンドリストを準備しています...");
	CreateUploadObjects();

	CreateSwapChain();
	CreateRenderTargets();

	CreateMsaaTargets();
	CreateDepthBuffer();

	CreateReadbackBuffers();

	// 初回描画前に FXAA 入力用の中間RT を必ず作成しておく。
	// ResizeIfNeeded() はサイズが変わらないと早期returnするため、
	// ここで作らないと m_intermediateTex が未作成のままになり Render() が早期returnして
	// 「起動後に何も表示されない」状態になります。
	CreateIntermediateResources();

	RecreateLayeredBitmap();

	ReportProgress(0.30f, L"テクスチャ用のリソースを初期化しています...");
	CreateSrvHeap();
	m_nextSrvIndex = 0;
	m_textureCache.clear();
	m_textures.clear();
	m_defaultWhiteSrv = CreateWhiteTexture1x1();
	m_defaultToonSrv = CreateDefaultToonRamp();

	CreatePmxRootSignature();

	ReportProgress(0.55f, L"メインシェーダーをコンパイルしています...");
	CreatePmxPipeline();
	ReportProgress(0.80f, L"輪郭シェーダーをコンパイルしています...");
	CreateEdgePipeline();

	ReportProgress(0.90f, L"FXAAパイプラインを準備しています...");
	CreateFxaaPipeline();

	CreateSceneBuffers();
	CreateBoneBuffers();

	ReportProgress(1.0f, L"初期化が完了しました。");
}

void DcompRenderer::ReleaseIntermediateResources()
{
	m_intermediateTex.Reset();
	m_intermediateRtvHeap.Reset();
	m_intermediateSrvHeap.Reset();
}

void DcompRenderer::CreateFxaaPipeline()
{

	CD3DX12_ROOT_PARAMETER params[2]{};
	params[0].InitAsConstants(2, 0);

	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(2, params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
	DX_CALL(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob));
	DX_CALL(m_ctx.Device()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_fxaaRootSig)));

	auto base = FileUtil::GetExecutableDir();
	std::wstring psname = base / L"Shaders\\FXAA_PS.hlsl";
	std::wstring vsname = base / L"Shaders\\FXAA_VS.hlsl";
	std::wstring pscompiled = base / L"Shaders\\Compiled_FXAA_PS.cso";
	std::wstring vscompiled = base / L"Shaders\\Compiled_FXAA_VS.cso";

	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, err;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), &vsBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &err);
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &err);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(err->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile FXAA VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.Get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), &psBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &err);
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &err);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(err->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile FXAA PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.Get(), pscompiled.c_str(), FALSE);
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_fxaaRootSig.Get();
	pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = FALSE;
	pso.DepthStencilState.StencilEnable = FALSE;
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM; // SwapChainと同じ
	pso.SampleDesc.Count = 1;

	DX_CALL(m_ctx.Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_fxaaPso)));
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
		IID_PPV_ARGS(&m_intermediateTex)));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_intermediateRtvHeap)));
	m_intermediateRtvHandle = m_intermediateRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateRenderTargetView(m_intermediateTex.Get(), nullptr, m_intermediateRtvHandle);

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_intermediateSrvHeap)));

	m_intermediateSrvCpuHandle = m_intermediateSrvHeap->GetCPUDescriptorHandleForHeapStart();
	m_intermediateSrvGpuHandle = m_intermediateSrvHeap->GetGPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	m_ctx.Device()->CreateShaderResourceView(m_intermediateTex.Get(), &srvDesc, m_intermediateSrvCpuHandle);
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
			IID_PPV_ARGS(&m_sceneCb[i])));

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
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(BoneCB)));

	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_ctx.Device()->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&m_boneCb[i])));

		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		DX_CALL(m_boneCb[i]->Map(0, &range, &mapped));
		m_boneCbMapped[i] = reinterpret_cast<BoneCB*>(mapped);

		for (size_t b = 0; b < MaxBones; ++b)
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
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[i])));
	}

	DX_CALL(m_ctx.Device()->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr,
		IID_PPV_ARGS(&m_cmdList)));
	m_cmdList->Close();

	DX_CALL(m_ctx.Device()->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
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
			&heapDesc, IID_PPV_ARGS(&m_rtvHeap)));
		m_rtvDescriptorSize = m_ctx.Device()->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
		dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvDesc.NumDescriptors = 1;
		dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CALL(m_ctx.Device()->CreateDescriptorHeap(
			&dsvDesc, IID_PPV_ARGS(&m_dsvHeap)));
	}
}

void DcompRenderer::CreateSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 desc = MakeSwapChainDesc(m_width, m_height);

	DX_CALL(m_ctx.Factory()->CreateSwapChainForComposition(
		m_ctx.Queue(), &desc, nullptr, &m_swapChain1));

	DX_CALL(m_swapChain1.As(&m_swapChain));
}

void DcompRenderer::CreateRenderTargets()
{
	auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
		m_ctx.Device()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);
		handle.ptr += m_rtvDescriptorSize;
	}
}

void DcompRenderer::CreateDComp()
{
	DX_CALL(DCompositionCreateDevice(
		nullptr, IID_PPV_ARGS(&m_dcompDevice)));

	DX_CALL(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget));

	DX_CALL(m_dcompDevice->CreateVisual(&m_dcompVisual));

	DX_CALL(m_dcompVisual->SetContent(m_swapChain.Get()));

	DX_CALL(m_dcompTarget->SetRoot(m_dcompVisual.Get()));

	DX_CALL(m_dcompDevice->Commit());
}

void DcompRenderer::ResizeIfNeeded()
{
	UINT newW{}, newH{};
	GetClientSize(m_hwnd, newW, newH);
	if (newW == m_width && newH == m_height) return;

	WaitForGpu();

	for (UINT i = 0; i < FrameCount; ++i)
	{
		if (m_readbackBuffers[i])
		{
			m_readbackBuffers[i]->Unmap(0, nullptr);
			m_readbackMapped[i] = nullptr;
			m_readbackBuffers[i].Reset();
		}
	}

	m_width = newW;
	m_height = newH;

	for (UINT i = 0; i < FrameCount; ++i)
	{
		m_renderTargets[i].Reset();
	}

	m_depth.Reset();

	DX_CALL(m_swapChain->ResizeBuffers(
		FrameCount, m_width, m_height, DXGI_FORMAT_R10G10B10A2_UNORM, 0));

	CreateRenderTargets();
	CreateMsaaTargets();
	CreateDepthBuffer();

	CreateReadbackBuffers();

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

void DcompRenderer::UpdateWindowBounds(
	float minx, float miny, float minz, float maxx, float maxy, float maxz,
	const DirectX::XMMATRIX& model, const DirectX::XMMATRIX& view,
	const DirectX::XMMATRIX& /*proj*/)
{
	using namespace DirectX;

	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);

	const int maxW = static_cast<int>(screenW * 0.95f);
	const int maxH = static_cast<int>(screenH * 0.95f);

	// ウインドウサイズ変更でモデルの見かけサイズ（ピクセル）が変わらないように、
	// 「ピクセル単位の焦点距離」を一定に保つ（=ウインドウサイズに応じてFOVが変化する）。
	// tan(fov/2) = H / K を満たすように fov を決めると、
	// ピクセル焦点距離 fy = H / (2*tan(fov/2)) = K/2 が一定になり、
	// ウインドウリサイズでモデルの見かけサイズが変わらなくなります。
	const float refFov = XMConvertToRadians(30.0f);
	const float K = 600.0f / std::tan(refFov * 0.5f);
	const float focalPx = K * 0.5f;

	// 1) bboxの8頂点を view space へ変換し、x/z と y/z の範囲を取る
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

	// 2) ピクセル焦点距離(focalPx)で画面ピクセル上のAABB（中心基準）を求める
	const float minU = minRx * focalPx;
	const float maxU = maxRx * focalPx;
	const float minV = minRy * focalPx;
	const float maxV = maxRy * focalPx;

	// 3) オートフィット用の必要クライアントサイズ（ピクセル）はそのまま求まる
	float desiredClientW = (maxU - minU) + marginPx * 2.0f;
	float desiredClientH = (maxV - minV) + marginPx * 2.0f;
	desiredClientW = quantize(desiredClientW);
	desiredClientH = quantize(desiredClientH);
	desiredClientW = std::clamp(desiredClientW, minClientSize, (float)maxW);
	desiredClientH = std::clamp(desiredClientH, minClientSize, (float)maxH);

	// クライアントサイズ -> ウインドウサイズに変換
	RECT rc = { 0, 0, (LONG)desiredClientW, (LONG)desiredClientH };
	const DWORD style = (DWORD)GetWindowLongPtrW(m_hwnd, GWL_STYLE);
	const DWORD exStyle = (DWORD)GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&rc, style, FALSE, exStyle);

	int desiredW = rc.right - rc.left;
	int desiredH = rc.bottom - rc.top;

	desiredW = std::clamp(desiredW, (int)minClientSize, maxW);
	desiredH = std::clamp(desiredH, (int)minClientSize, maxH);

	RECT wnd{};
	GetWindowRect(m_hwnd, &wnd);
	const int curWidth = wnd.right - wnd.left;
	const int curHeight = wnd.bottom - wnd.top;

#if DCOMP_AUTOFIT_WINDOW
	static int reservedW = 0;
	static int reservedH = 0;

	static uint64_t frameCounter = 0;
	static uint64_t lastResizeFrame = 0;
	++frameCounter;

	if (!m_disableAutofitWindow)
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
			SetWindowPos(m_hwnd, nullptr, 0, 0, desiredW, desiredH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			lastResizeFrame = frameCounter;

#if DCOMP_AUTOFIT_GROW_ONLY
			reservedW = std::max(reservedW, desiredW);
			reservedH = std::max(reservedH, desiredH);
#endif
		}
	}
#endif

	// 4) 現在のクライアントサイズでコンテンツ矩形を計算して保持
	UINT clientW = 0, clientH = 0;
	GetClientSize(m_hwnd, clientW, clientH);
	if (clientW == 0 || clientH == 0)
	{
		m_hasContentRect = false;
		return;
	}

	// minU/maxU/minV/maxV は上で focalPx を用いてピクセル単位で計算済み

	const float centerX = clientW * 0.5f;
	const float centerY = clientH * 0.5f;

	m_lastContentRect.left = static_cast<LONG>(centerX + minU);
	m_lastContentRect.right = static_cast<LONG>(centerX + maxU);
	m_lastContentRect.top = static_cast<LONG>(centerY - maxV);
	m_lastContentRect.bottom = static_cast<LONG>(centerY - minV);

	m_hasContentRect = true;
}

void DcompRenderer::Render(const MmdAnimator& animator)
{
	const auto* model = animator.Model();
	if (!model) return;

	EnsurePmxResources(model);
	if (!m_pmx.ready)
	{
		m_hasContentRect = false;
		return;
	}

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
	const float distance = std::max(0.1f, baseDistance * m_cameraDistance);
	const float cosPitch = std::cos(m_cameraPitch);

	XMVECTOR eyeOffset = XMVectorSet(
		distance * std::sin(m_cameraYaw) * cosPitch,
		distance * std::sin(m_cameraPitch),
		-distance * std::cos(m_cameraYaw) * cosPitch,
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

	UpdateWindowBounds(minx, miny, minz, maxx, maxy, maxz, M, V, P);
	ResizeIfNeeded();

	XMStoreFloat4x4(&m_lastModelMatrix, M);
	XMStoreFloat4x4(&m_lastViewMatrix, V);
	XMStoreFloat4x4(&m_lastProjMatrix, P);
	m_matricesValid = true;

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
	UpdatePmxMorphs(animator);
	// -------------------------------------------------------------

	// マテリアル設定（影の濃さなど）を再適用
	UpdateMaterialSettings();

	m_alloc[frameIndex]->Reset();
	m_cmdList->Reset(m_alloc[frameIndex].Get(), nullptr);

	UpdateBoneMatrices(animator, frameIndex);

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
				m_msaaColor.Get(), m_msaaColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_cmdList->ResourceBarrier(1, &barrier);
			m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
	}
	else
	{
		if (s_interState != D3D12_RESOURCE_STATE_RENDER_TARGET)
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_intermediateTex.Get(), s_interState, D3D12_RESOURCE_STATE_RENDER_TARGET);
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

	if (m_srvHeap)
	{
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
		m_cmdList->SetDescriptorHeaps(1, heaps);

		m_cmdList->SetGraphicsRootSignature(m_pmxRootSig.Get());
		m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdList->IASetVertexBuffers(0, 1, &m_pmx.vbv);
		m_cmdList->IASetIndexBuffer(&m_pmx.ibv);

		m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb[frameIndex]->GetGPUVirtualAddress());
		m_cmdList->SetGraphicsRootConstantBufferView(3, m_boneCb[frameIndex]->GetGPUVirtualAddress());

		std::vector<size_t> opaque, trans;
		opaque.reserve(m_pmx.materials.size());
		trans.reserve(m_pmx.materials.size());
		for (size_t i = 0; i < m_pmx.materials.size(); ++i)
		{
			if (m_pmx.materials[i].mat.diffuse[3] < 0.999f) trans.push_back(i);
			else opaque.push_back(i);
		}

		auto DrawMats = [&](const std::vector<size_t>& indices) {
			for (auto i : indices)
			{
				const auto& gm = m_pmx.materials[i];
				m_cmdList->SetGraphicsRootConstantBufferView(1, gm.materialCbGpu);
				m_cmdList->SetGraphicsRootDescriptorTable(2, GetSrvGpuHandle(gm.srvBlockIndex));
				if (gm.mat.indexCount > 0)
					m_cmdList->DrawIndexedInstanced((UINT)gm.mat.indexCount, 1, (UINT)gm.mat.indexOffset, 0, 0);
			}
			};

		m_cmdList->SetPipelineState(m_pmxPsoOpaque.Get());
		DrawMats(opaque);

		m_cmdList->SetPipelineState(m_pmxPsoTrans.Get());
		DrawMats(trans);

		// 輪郭線 (Edge Pass)
		m_cmdList->SetPipelineState(m_edgePso.Get());
		m_cmdList->SetGraphicsRootSignature(m_pmxRootSig.Get());
		m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdList->IASetVertexBuffers(0, 1, &m_pmx.vbv);
		m_cmdList->IASetIndexBuffer(&m_pmx.ibv);

		m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb[frameIndex]->GetGPUVirtualAddress());
		m_cmdList->SetGraphicsRootConstantBufferView(3, m_boneCb[frameIndex]->GetGPUVirtualAddress());

		for (size_t i = 0; i < m_pmx.materials.size(); ++i)
		{
			const auto& gm = m_pmx.materials[i];
			const MaterialCB* cb = reinterpret_cast<const MaterialCB*>(m_materialCbMapped + i * m_materialCbStride);

			if (cb->edgeSize <= 0.0f || cb->edgeColor.w <= 0.001f) continue;

			m_cmdList->SetGraphicsRootConstantBufferView(1, gm.materialCbGpu);
			m_cmdList->SetGraphicsRootDescriptorTable(2, GetSrvGpuHandle(gm.srvBlockIndex));
			if (gm.mat.indexCount > 0)
				m_cmdList->DrawIndexedInstanced((UINT)gm.mat.indexCount, 1, (UINT)gm.mat.indexOffset, 0, 0);
		}
	}

	if (useMsaa)
	{
		auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_msaaColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_intermediateTex.Get(), s_interState, D3D12_RESOURCE_STATE_RESOLVE_DEST);

		D3D12_RESOURCE_BARRIER barriers[] = { b1, b2 };
		m_cmdList->ResourceBarrier(2, barriers);

		m_cmdList->ResolveSubresource(
			m_intermediateTex.Get(), 0, m_msaaColor.Get(), 0, DXGI_FORMAT_R10G10B10A2_UNORM);

		m_msaaColorState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		s_interState = D3D12_RESOURCE_STATE_RESOLVE_DEST;

		auto b3 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_msaaColor.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_cmdList->ResourceBarrier(1, &b3);
		m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	{
		D3D12_RESOURCE_BARRIER barriers[2];
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_intermediateTex.Get(), s_interState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		m_cmdList->ResourceBarrier(2, barriers);
		s_interState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	auto backBufferRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	backBufferRtv.ptr += (SIZE_T)frameIndex * m_rtvDescriptorSize;
	m_cmdList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);

	ID3D12DescriptorHeap* fxaaHeaps[] = { m_intermediateSrvHeap.Get() };
	m_cmdList->SetDescriptorHeaps(1, fxaaHeaps);

	m_cmdList->SetGraphicsRootSignature(m_fxaaRootSig.Get());
	m_cmdList->SetPipelineState(m_fxaaPso.Get());
	m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float consts[2] = { 1.0f / (float)m_width, 1.0f / (float)m_height };
	m_cmdList->SetGraphicsRoot32BitConstants(0, 2, consts, 0);

	m_cmdList->SetGraphicsRootDescriptorTable(1, m_intermediateSrvGpuHandle);

	m_cmdList->DrawInstanced(3, 1, 0, 0);

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_cmdList->ResourceBarrier(1, &barrier);
	}

	if (m_readbackBuffers[frameIndex])
	{
		auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_cmdList->ResourceBarrier(1, &b1);

		CD3DX12_TEXTURE_COPY_LOCATION dst(m_readbackBuffers[frameIndex].Get(), m_readbackFootprint);
		CD3DX12_TEXTURE_COPY_LOCATION src(m_renderTargets[frameIndex].Get(), 0);

		m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
		m_cmdList->ResourceBarrier(1, &b2);
	}

	m_cmdList->Close();
	ID3D12CommandList* lists[] = { m_cmdList.Get() };
	m_ctx.Queue()->ExecuteCommandLists(1, lists);

	m_swapChain->Present(1, 0);

	const UINT64 signalValue = m_fenceValue++;
	m_ctx.Queue()->Signal(m_fence.Get(), signalValue);
	m_frameFenceValues[frameIndex] = signalValue;

	WaitForFrame(frameIndex);
	PresentLayered(frameIndex);
}

void DcompRenderer::AddMorphWeight(const PmxModel* model, int morphIndex, float weight, std::vector<float>& totalWeights)
{
	if (morphIndex < 0 || morphIndex >= static_cast<int>(totalWeights.size())) return;

	const auto& m = model->Morphs()[morphIndex];

	if (m.type == PmxModel::Morph::Type::Group)
	{
		// グループモーフ：子モーフに対してウェイトを乗算して再帰的に適用
		for (const auto& offset : m.groupOffsets)
		{
			AddMorphWeight(model, offset.morphIndex, weight * offset.weight, totalWeights);
		}
	}
	else
	{
		// 通常モーフ：ウェイトを加算（同一モーフが複数回参照される場合のため）
		totalWeights[morphIndex] += weight;
	}
}

void DcompRenderer::UpdatePmxMorphs(const MmdAnimator& animator)
{
	if (!m_pmx.ready) return;
	const auto* model = animator.Model();
	if (!model) return;

	// モーフ一覧を取得
	const auto& morphs = model->Morphs();
	if (morphs.empty()) return;

	// 1. モーフウェイトの解決 (グループモーフの再帰展開)
	// -------------------------------------------------------------------------
	// ウェイト配列をリセット
	if (m_morphWeights.size() != morphs.size())
	{
		m_morphWeights.resize(morphs.size());
	}
	std::fill(m_morphWeights.begin(), m_morphWeights.end(), 0.0f);

	const auto& currentPose = animator.CurrentPose();

	// 名前ベースでアクティブなモーフを検索し、インデックスベースのウェイト配列に反映
	for (size_t i = 0; i < morphs.size(); ++i)
	{
		auto it = currentPose.morphWeights.find(morphs[i].name);
		if (it != currentPose.morphWeights.end())
		{
			float w = it->second;
			if (std::abs(w) > 0.0001f)
			{
				AddMorphWeight(model, static_cast<int>(i), w, m_morphWeights);
			}
		}
	}

	// 2. 頂点・UVモーフの適用 (CPU計算 -> GPU転送)
	// -------------------------------------------------------------------------
	// 累積誤差を防ぐため、毎回「初期状態(Base)」から計算を行う
	if (m_baseVertices.empty()) return;

	// 作業用バッファを初期化
	if (m_workingVertices.size() != m_baseVertices.size())
	{
		m_workingVertices = m_baseVertices;
	}
	else
	{
		std::memcpy(m_workingVertices.data(), m_baseVertices.data(), m_baseVertices.size() * sizeof(PmxVsVertex));
	}

	bool vertexDirty = false;

	for (size_t i = 0; i < morphs.size(); ++i)
	{
		float w = m_morphWeights[i];
		if (std::abs(w) < 0.0001f) continue;

		const auto& m = morphs[i];

		if (m.type == PmxModel::Morph::Type::Vertex)
		{
			vertexDirty = true;
			for (const auto& vo : m.vertexOffsets)
			{
				if (vo.vertexIndex < m_workingVertices.size())
				{
					auto& v = m_workingVertices[vo.vertexIndex];
					v.px += vo.positionOffset.x * w;
					v.py += vo.positionOffset.y * w;
					v.pz += vo.positionOffset.z * w;
				}
			}
		}
		else if (m.type == PmxModel::Morph::Type::UV) // UV0
		{
			vertexDirty = true;
			for (const auto& uvo : m.uvOffsets)
			{
				if (uvo.vertexIndex < m_workingVertices.size())
				{
					auto& v = m_workingVertices[uvo.vertexIndex];
					v.u += uvo.offset.x * w;
					v.v += uvo.offset.y * w;
					// PMX仕様ではUVオフセットは float4 だが、頂点データは float2 (u,v) のため z,w は無視
				}
			}
		}
		// 追加UV(1~4)が必要な場合はここに実装を追加する
	}

	// 変更があった場合のみ GPU リソースを更新
	if (vertexDirty && m_pmx.vb)
	{
		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0); // 書き込み専用
		if (SUCCEEDED(m_pmx.vb->Map(0, &range, &mapped)))
		{
			std::memcpy(mapped, m_workingVertices.data(), m_workingVertices.size() * sizeof(PmxVsVertex));
			m_pmx.vb->Unmap(0, nullptr);
		}
	}

	// 3. マテリアルモーフの適用 (Mapped Buffer 直接書き換え)
	// -------------------------------------------------------------------------
	// マテリアル定数バッファを先頭から順に更新する
	// ※マテリアル数が一致していることを前提とする

	for (size_t mi = 0; mi < m_pmx.materials.size(); ++mi)
	{
		const auto& gm = m_pmx.materials[mi]; // GPU用マテリアル定義(初期値保持)

		// マップ済みポインタを取得
		MaterialCB* cb = reinterpret_cast<MaterialCB*>(m_materialCbMapped + mi * m_materialCbStride);

		// まず初期値にリセット
		cb->diffuse = { gm.mat.diffuse[0], gm.mat.diffuse[1], gm.mat.diffuse[2], gm.mat.diffuse[3] };
		cb->specular = { gm.mat.specular[0], gm.mat.specular[1], gm.mat.specular[2] };
		cb->specPower = gm.mat.specularPower;
		cb->ambient = { gm.mat.ambient[0], gm.mat.ambient[1], gm.mat.ambient[2] };
		cb->edgeColor = { gm.mat.edgeColor[0], gm.mat.edgeColor[1], gm.mat.edgeColor[2], gm.mat.edgeColor[3] };
		cb->edgeSize = gm.mat.edgeSize;

		// 以下のパラメータはモーフ対象外だが、UpdateMaterialSettings で上書きされる可能性があるため
		// ここでは初期値をセットし、マテリアル設定更新関数との競合に注意する必要がある。
		// 今回は「モーフが優先」される構成とする。

		// マテリアルモーフ適用
		for (size_t i = 0; i < morphs.size(); ++i)
		{
			float w = m_morphWeights[i];
			if (std::abs(w) < 0.0001f) continue;

			const auto& m = morphs[i];
			if (m.type == PmxModel::Morph::Type::Material)
			{
				for (const auto& mo : m.materialOffsets)
				{
					// 対象インデックスが -1 なら全マテリアル対象、そうでなければ一致判定
					if (mo.materialIndex == -1 || mo.materialIndex == static_cast<int>(mi))
					{
						// 演算タイプ: 0=乗算, 1=加算
						if (mo.operation == 0) // 乗算
						{
							// MMD仕様の乗算: Base * (1 + (Offset - 1) * w)
							// Offsetが1なら変化なし、0なら w=1 で0になる
							auto applyMul4 = [&](DirectX::XMFLOAT4& target, const DirectX::XMFLOAT4& offset) {
								target.x *= (1.0f + (offset.x - 1.0f) * w);
								target.y *= (1.0f + (offset.y - 1.0f) * w);
								target.z *= (1.0f + (offset.z - 1.0f) * w);
								target.w *= (1.0f + (offset.w - 1.0f) * w);
								};
							auto applyMul3 = [&](DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& offset) {
								target.x *= (1.0f + (offset.x - 1.0f) * w);
								target.y *= (1.0f + (offset.y - 1.0f) * w);
								target.z *= (1.0f + (offset.z - 1.0f) * w);
								};
							auto applyMul1 = [&](float& target, float offset) {
								target *= (1.0f + (offset - 1.0f) * w);
								};

							applyMul4(cb->diffuse, mo.diffuse);
							applyMul3(cb->specular, mo.specular);
							applyMul1(cb->specPower, mo.specularPower);
							applyMul3(cb->ambient, mo.ambient);
							applyMul4(cb->edgeColor, mo.edgeColor);
							applyMul1(cb->edgeSize, mo.edgeSize);

							// テクスチャ係数等はMaterialCBに含まれていないため省略
						}
						else if (mo.operation == 1) // 加算
						{
							// 単純加算: Base + Offset * w
							auto applyAdd4 = [&](DirectX::XMFLOAT4& target, const DirectX::XMFLOAT4& offset) {
								target.x += offset.x * w;
								target.y += offset.y * w;
								target.z += offset.z * w;
								target.w += offset.w * w;
								};
							auto applyAdd3 = [&](DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& offset) {
								target.x += offset.x * w;
								target.y += offset.y * w;
								target.z += offset.z * w;
								};
							auto applyAdd1 = [&](float& target, float offset) {
								target += offset * w;
								};

							applyAdd4(cb->diffuse, mo.diffuse);
							applyAdd3(cb->specular, mo.specular);
							applyAdd1(cb->specPower, mo.specularPower);
							applyAdd3(cb->ambient, mo.ambient);
							applyAdd4(cb->edgeColor, mo.edgeColor);
							applyAdd1(cb->edgeSize, mo.edgeSize);
						}
					}
				}
			}
		}

		// 値のクランプ (色などが負になったり1を超えすぎないように)
		auto saturateColor = [](float& v) { v = std::clamp(v, 0.0f, 1.0f); };
		saturateColor(cb->diffuse.x); saturateColor(cb->diffuse.y); saturateColor(cb->diffuse.z); saturateColor(cb->diffuse.w);
		saturateColor(cb->specular.x); saturateColor(cb->specular.y); saturateColor(cb->specular.z);
		saturateColor(cb->ambient.x); saturateColor(cb->ambient.y); saturateColor(cb->ambient.z);
		saturateColor(cb->edgeColor.x); saturateColor(cb->edgeColor.y); saturateColor(cb->edgeColor.z); saturateColor(cb->edgeColor.w);
		cb->edgeSize = std::max(0.0f, cb->edgeSize);
	}
}

void DcompRenderer::UpdateBoneMatrices(const MmdAnimator& animator, UINT frameIndex)
{
	if (frameIndex >= FrameCount) return;
	auto* dst = m_boneCbMapped[frameIndex];
	if (!dst) return;

	if (animator.HasSkinnedPose())
	{
		const auto& matrices = animator.GetSkinningMatrices();
		size_t count = std::min(matrices.size(), static_cast<size_t>(MaxBones));

		for (size_t i = 0; i < count; ++i)
		{
			DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&matrices[i]);
			DirectX::XMStoreFloat4x4(&dst->boneMatrices[i], DirectX::XMMatrixTranspose(mat));
		}

		for (size_t i = count; i < MaxBones; ++i)
		{
			DirectX::XMStoreFloat4x4(&dst->boneMatrices[i], DirectX::XMMatrixIdentity());
		}
	}
	else
	{
		for (size_t i = 0; i < MaxBones; ++i)
		{
			DirectX::XMStoreFloat4x4(&dst->boneMatrices[i], DirectX::XMMatrixIdentity());
		}
	}
}

void DcompRenderer::WaitForGpu()
{
	const UINT64 fenceToWait = m_fenceValue++;
	m_ctx.Queue()->Signal(m_fence.Get(), fenceToWait);

	if (m_fence->GetCompletedValue() < fenceToWait)
	{
		m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void DcompRenderer::CreatePmxPipeline()
{
	if (!m_pmxRootSig)
	{
		CreatePmxRootSignature();
	}

	auto base = FileUtil::GetExecutableDir();
	std::wstring psname = base / L"Shaders\\PMX_PS.hlsl";
	std::wstring vsname = base / L"Shaders\\PMX_VS.hlsl";
	std::wstring pscompiled = base / L"Shaders\\Compiled_PMX_PS.cso";
	std::wstring vscompiled = base / L"Shaders\\Compiled_PMX_VS.cso";

	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), &vsBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile PMX VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.Get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), &psBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errBlob);
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errBlob);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile PMX PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.Get(), pscompiled.c_str(), FALSE);
	}

	D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 76, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 4, DXGI_FORMAT_R32_UINT, 0, 100, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	auto MakeBaseDesc = [&]() {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = m_pmxRootSig.Get();
		pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
		pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
		pso.InputLayout = { layout, _countof(layout) };
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = TRUE;
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		auto& rt = pso.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
		pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso.SampleDesc.Count = m_msaaSampleCount;
		pso.SampleDesc.Quality = m_msaaQuality;
		pso.SampleMask = UINT_MAX;
		return pso;
		};

	{
		auto pso = MakeBaseDesc();
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		DX_CALL(m_ctx.Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pmxPsoOpaque)));
	}
	{
		auto pso = MakeBaseDesc();
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		DX_CALL(m_ctx.Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pmxPsoTrans)));
	}
}

void DcompRenderer::CreateEdgePipeline()
{
	if (!m_pmxRootSig)
	{
		CreatePmxRootSignature();
	}

	auto base = FileUtil::GetExecutableDir();
	std::wstring psname = base / L"Shaders\\Edge_PS.hlsl";
	std::wstring vsname = base / L"Shaders\\Edge_VS.hlsl";
	std::wstring pscompiled = base / L"Shaders\\Compiled_Edge_PS.cso";
	std::wstring vscompiled = base / L"Shaders\\Compiled_Edge_VS.cso";

	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), &vsBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errBlob);
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errBlob);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile Edge VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.Get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), &psBlob);
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile Edge PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.Get(), pscompiled.c_str(), FALSE);
	}

	D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 76, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 4, DXGI_FORMAT_R32_UINT, 0, 100, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_pmxRootSig.Get();
	pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	pso.InputLayout = { layout, _countof(layout) };
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = TRUE;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	auto& rt = pso.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_ONE;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = m_msaaSampleCount;
	pso.SampleDesc.Quality = m_msaaQuality;
	pso.SampleMask = UINT_MAX;

	DX_CALL(m_ctx.Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_edgePso)));
}

// DcompRenderer.cpp の EnsurePmxResources をこれに置き換え

void DcompRenderer::EnsurePmxResources(const PmxModel* model)
{
	if (!model || !model->HasGeometry())
	{
		m_pmx.ready = false;
		return;
	}

	if (m_pmx.ready && m_pmx.revision == model->Revision())
	{
		return;
	}

	m_pmx = {};

	const auto& verts = model->Vertices();
	const auto& inds = model->Indices();
	const auto& mats = model->Materials();
	const auto& texPaths = model->TexturePaths();

	// スキニング対応の頂点バッファを構築
	std::vector<PmxVsVertex> vtx;
	vtx.reserve(verts.size());

	const auto boneCount = model->Bones().size();

	// まず頂点データを構築
	for (const auto& v : verts)
	{
		PmxVsVertex pv{};
		pv.px = v.px; pv.py = v.py; pv.pz = v.pz;
		pv.nx = v.nx; pv.ny = v.ny; pv.nz = v.nz;
		pv.u = v.u; pv.v = v.v;

		// 初期化
		for (int i = 0; i < 4; ++i)
		{
			pv.boneIndices[i] = -1;
			pv.boneWeights[i] = 0.0f;
		}

		// ボーンインデックスとウェイトをコピー
		int32_t fallbackBone = -1;
		for (int i = 0; i < 4; ++i)
		{
			int32_t boneIdx = v.weight.boneIndices[i];
			float weight = v.weight.weights[i];

			// 有効なボーンインデックスかつウェイトが正の場合のみ設定
			if (boneIdx >= 0 && boneIdx < static_cast<int32_t>(boneCount) && weight > 0.0f)
			{
				pv.boneIndices[i] = boneIdx;
				pv.boneWeights[i] = weight;
				if (fallbackBone < 0) fallbackBone = boneIdx;
			}
		}

		// ウェイトの正規化
		float totalWeight = pv.boneWeights[0] + pv.boneWeights[1] +
			pv.boneWeights[2] + pv.boneWeights[3];

		if (totalWeight > 0.001f)
		{
			for (int i = 0; i < 4; ++i)
			{
				pv.boneWeights[i] /= totalWeight;
			}
		}
		else if (fallbackBone >= 0)
		{
			pv.boneIndices[0] = fallbackBone;
			pv.boneWeights[0] = 1.0f;
		}

		// SDEF 情報
		pv.weightType = v.weight.type;
		if (v.weight.type == 3)
		{
			pv.sdefC[0] = v.weight.sdefC.x;
			pv.sdefC[1] = v.weight.sdefC.y;
			pv.sdefC[2] = v.weight.sdefC.z;
			pv.sdefR0[0] = v.weight.sdefR0.x;
			pv.sdefR0[1] = v.weight.sdefR0.y;
			pv.sdefR0[2] = v.weight.sdefR0.z;
			pv.sdefR1[0] = v.weight.sdefR1.x;
			pv.sdefR1[1] = v.weight.sdefR1.y;
			pv.sdefR1[2] = v.weight.sdefR1.z;
		}

		vtx.push_back(pv);
	}

	// --------------------------------------------------------
	// モーフ計算用に初期状態を保存 (ここが追加重要ポイント)
	// --------------------------------------------------------
	m_baseVertices = vtx;
	m_workingVertices = vtx;
	m_morphWeights.resize(model->Morphs().size());
	// --------------------------------------------------------

	// ここから頂点バッファ作成
	const UINT vbSize = static_cast<UINT>(vtx.size() * sizeof(PmxVsVertex));
	const UINT ibSize = static_cast<UINT>(inds.size() * sizeof(uint32_t));

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// 頂点バッファ
	{
		auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
		DX_CALL(m_ctx.Device()->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&m_pmx.vb)));

		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		DX_CALL(m_pmx.vb->Map(0, &range, &mapped));
		std::memcpy(mapped, vtx.data(), vbSize);
		m_pmx.vb->Unmap(0, nullptr);

		m_pmx.vbv.BufferLocation = m_pmx.vb->GetGPUVirtualAddress();
		m_pmx.vbv.StrideInBytes = sizeof(PmxVsVertex);
		m_pmx.vbv.SizeInBytes = vbSize;
	}

	// インデックスバッファ
	{
		auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
		DX_CALL(m_ctx.Device()->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&m_pmx.ib)));

		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		DX_CALL(m_pmx.ib->Map(0, &range, &mapped));
		std::memcpy(mapped, inds.data(), ibSize);
		m_pmx.ib->Unmap(0, nullptr);

		m_pmx.ibv.BufferLocation = m_pmx.ib->GetGPUVirtualAddress();
		m_pmx.ibv.Format = DXGI_FORMAT_R32_UINT;
		m_pmx.ibv.SizeInBytes = ibSize;
	}

	// マテリアルCBを確保
	{
		const size_t matCount = mats.size();
		const UINT64 totalSize = m_materialCbStride * matCount;

		if (totalSize > 0)
		{
			auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
			DX_CALL(m_ctx.Device()->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(&m_materialCb)));

			CD3DX12_RANGE range(0, 0);
			DX_CALL(m_materialCb->Map(0, &range, reinterpret_cast<void**>(&m_materialCbMapped)));
		}
	}

	// マテリアルごとの処理
	m_pmx.materials.clear();
	m_pmx.materials.reserve(mats.size());

	for (size_t mi = 0; mi < mats.size(); ++mi)
	{
		const auto& mat = mats[mi];
		PmxGpuMaterial gm{};
		gm.mat = mat;

		// 目/まつ毛(目テクスチャ一体型含む)はアウトライン無効
		float edgeSize = mat.edgeSize;
		if (IsEyeOrLashMaterial(mat, texPaths))
		{
			edgeSize = 0.0f;
		}
		gm.mat.edgeSize = edgeSize;

		// SRVブロックを確保（base, toon, sphere）
		uint32_t matType = GuessMaterialType(mat);
		float rimMul, specMul, shadowMul, toonContrastMul;
		GetMaterialStyleParams(matType, rimMul, specMul, shadowMul, toonContrastMul);

		gm.srvBlockIndex = AllocSrvBlock3();

		// ベーステクスチャ
		uint32_t baseSrv = m_defaultWhiteSrv;
		if (mat.textureIndex >= 0 && mat.textureIndex < static_cast<int32_t>(texPaths.size()))
		{
			baseSrv = LoadTextureSrv(texPaths[mat.textureIndex]);
		}
		CopySrv(gm.srvBlockIndex + 0, baseSrv);

		// トゥーンテクスチャ
		uint32_t toonSrv = m_defaultToonSrv;
		if (mat.toonFlag == 0 && mat.toonIndex >= 0 && mat.toonIndex < static_cast<int32_t>(texPaths.size()))
		{
			toonSrv = LoadTextureSrv(texPaths[mat.toonIndex]);
		}
		CopySrv(gm.srvBlockIndex + 1, toonSrv);

		// スフィアテクスチャ
		uint32_t sphereSrv = m_defaultWhiteSrv;
		if (mat.sphereTextureIndex >= 0 && mat.sphereTextureIndex < static_cast<int32_t>(texPaths.size()))
		{
			sphereSrv = LoadTextureSrv(texPaths[mat.sphereTextureIndex]);
		}
		CopySrv(gm.srvBlockIndex + 2, sphereSrv);

		const bool isFace = LooksLikeFaceMaterial(mat);
		if (isFace)
		{
			shadowMul = m_lightSettings.faceShadowMul;
			toonContrastMul = m_lightSettings.faceToonContrastMul;
		}

		// マテリアルCBを書き込み
		gm.materialCbGpu = m_materialCb->GetGPUVirtualAddress() + mi * m_materialCbStride;

		MaterialCB* mcb = reinterpret_cast<MaterialCB*>(m_materialCbMapped + mi * m_materialCbStride);
		mcb->diffuse = { mat.diffuse[0],  mat.diffuse[1],  mat.diffuse[2],  mat.diffuse[3] };
		mcb->ambient = { mat.ambient[0],  mat.ambient[1],  mat.ambient[2] };
		mcb->specular = { mat.specular[0], mat.specular[1], mat.specular[2] };
		mcb->specPower = mat.specularPower;
		mcb->sphereMode = mat.sphereMode;
		mcb->edgeSize = edgeSize;
		mcb->edgeColor = { mat.edgeColor[0], mat.edgeColor[1], mat.edgeColor[2], mat.edgeColor[3] };

		mcb->materialType = matType;
		mcb->rimMul = rimMul;
		mcb->specMul = specMul;
		mcb->shadowMul = shadowMul;
		mcb->toonContrastMul = toonContrastMul;

		m_pmx.materials.push_back(gm);
	}

	m_pmx.indexCount = static_cast<uint32_t>(inds.size());
	m_pmx.revision = model->Revision();
	m_pmx.ready = true;
}

void DcompRenderer::CreateDepthBuffer()
{
	if (m_width == 0 || m_height == 0) return;
	if (!m_dsvHeap)
	{
		throw std::runtime_error("DSV heap not created.");
	}

	m_depth.Reset();

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
		&clear, IID_PPV_ARGS(&m_depth)));

	m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsvHandle);
}

void DcompRenderer::CreateSrvHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 4096; // 余裕を持たせる
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)));
	m_srvDescriptorSize = m_ctx.Device()->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	);
}

D3D12_CPU_DESCRIPTOR_HANDLE DcompRenderer::GetSrvCpuHandle(uint32_t index) const
{
	auto h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_srvDescriptorSize;
	return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE DcompRenderer::GetSrvGpuHandle(uint32_t index) const
{
	auto h = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_srvDescriptorSize;
	return h;
}

uint32_t DcompRenderer::CreateWhiteTexture1x1()
{
	const uint8_t white[4] = { 255, 255, 255, 255 };

	const uint32_t srvIndex = AllocSrvIndex();

	auto tex = CreateTexture2DFromRgba(white, 1, 1);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;

	m_ctx.Device()->CreateShaderResourceView(tex.Get(), &srv, GetSrvCpuHandle(srvIndex));

	m_textures.push_back(GpuTexture{ tex, srvIndex, 1, 1 });

	return srvIndex;
}

static float Lanczos(float x, float a = 3.0f)
{
	if (std::abs(x) < 1e-6f) return 1.0f;
	if (x < -a || x > a) return 0.0f;
	const float pi = 3.14159265359f;
	float pi_x = pi * x;
	return (a * std::sin(pi_x) * std::sin(pi_x / a)) / (pi_x * pi_x);
}

static std::vector<std::vector<uint8_t>> BuildMipChainRGBA(const uint8_t* src, uint32_t w, uint32_t h)
{
	static constexpr int kRadius = 3;
	static constexpr int kTaps = kRadius * 2; // 6

	// 8-bit -> linear (gamma 2.2) / 8-bit -> [0,1] をLUT化（元コードのpow結果をfloat化した値と一致）
	static const std::array<float, 256> kToLinear = [] {
		std::array<float, 256> t{};
		for (int i = 0; i < 256; ++i)
		{
			const float v = (float)i / 255.0f;
			t[i] = std::pow(v, 2.2f);
		}
		return t;
		}();
	static const std::array<float, 256> kToNorm = [] {
		std::array<float, 256> t{};
		for (int i = 0; i < 256; ++i) t[i] = (float)i / 255.0f;
		return t;
		}();

	auto clampi = [](int v, int lo, int hi) -> int {
		return (v < lo) ? lo : (v > hi ? hi : v);
		};

	// mip数を先に見積もってreserve（再確保回避）
	uint32_t levels = 1;
	for (uint32_t tw = w, th = h; tw > 1 || th > 1; )
	{
		tw = std::max(1u, tw / 2);
		th = std::max(1u, th / 2);
		++levels;
	}

	std::vector<std::vector<uint8_t>> mips;
	mips.reserve(levels);

	mips.emplace_back(src, src + (size_t)w * h * 4);

	uint32_t cw = w, ch = h;

	while (cw > 1 || ch > 1)
	{
		const uint32_t nw = std::max(1u, cw / 2);
		const uint32_t nh = std::max(1u, ch / 2);

		std::vector<uint8_t> next((size_t)nw * nh * 4);

		const auto& prevVec = mips.back();
		const uint8_t* prev = prevVec.data();
		uint8_t* dst = next.data();

		const float scaleX = (float)cw / (float)nw;
		const float scaleY = (float)ch / (float)nh;
		static constexpr float kInvGamma = 1.0f / 2.2f;

		// x方向は各y行で再利用されるので、px/wxを前計算（Lanczos呼び出し削減）
		std::vector<std::array<int, kTaps>> pxTable(nw);
		std::vector<std::array<float, kTaps>> wxTable(nw);

		for (uint32_t x = 0; x < nw; ++x)
		{
			const float centerX = (x + 0.5f) * scaleX - 0.5f;
			const int startX = (int)std::floor(centerX) - kRadius + 1;

			auto& pxA = pxTable[x];
			auto& wxA = wxTable[x];

			for (int kx = 0; kx < kTaps; ++kx)
			{
				const int px = clampi(startX + kx, 0, (int)cw - 1);
				pxA[kx] = px;
				wxA[kx] = Lanczos(centerX - (float)px);
			}
		}

#if defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
		for (int yi = 0; yi < (int)nh; ++yi)
		{
			const uint32_t y = (uint32_t)yi;

			// y方向はこのy行でだけ使うので、py/wy/rowBaseを行単位で前計算
			std::array<int, kTaps> pyA{};
			std::array<float, kTaps> wyA{};
			std::array<size_t, kTaps> rowBaseA{};

			const float centerY = (y + 0.5f) * scaleY - 0.5f;
			const int startY = (int)std::floor(centerY) - kRadius + 1;

			for (int ky = 0; ky < kTaps; ++ky)
			{
				const int py = clampi(startY + ky, 0, (int)ch - 1);
				pyA[ky] = py;
				wyA[ky] = Lanczos(centerY - (float)py);
				rowBaseA[ky] = (size_t)py * (size_t)cw * 4;
			}

			for (uint32_t x = 0; x < nw; ++x)
			{
				const auto& pxA = pxTable[x];
				const auto& wxA = wxTable[x];

				float acc0 = 0.0f, acc1 = 0.0f, acc2 = 0.0f, acc3 = 0.0f;
				float weightSum = 0.0f;

				for (int ky = 0; ky < kTaps; ++ky)
				{
					const float wy = wyA[ky];
					const size_t rowBase = rowBaseA[ky];

					for (int kx = 0; kx < kTaps; ++kx)
					{
						const float weight = wxA[kx] * wy;
						weightSum += weight;

						const size_t idx = rowBase + (size_t)pxA[kx] * 4;

						const uint8_t r8 = prev[idx + 0];
						const uint8_t g8 = prev[idx + 1];
						const uint8_t b8 = prev[idx + 2];
						const uint8_t a8 = prev[idx + 3];

						acc0 += kToLinear[r8] * weight;
						acc1 += kToLinear[g8] * weight;
						acc2 += kToLinear[b8] * weight;
						acc3 += kToNorm[a8] * weight;
					}
				}

				if (weightSum > 0.0f)
				{
					const float invW = 1.0f / weightSum;
					acc0 *= invW;
					acc1 *= invW;
					acc2 *= invW;
					acc3 *= invW;
				}

				const size_t di = ((size_t)y * nw + x) * 4;
				dst[di + 0] = (uint8_t)std::clamp(std::pow(acc0, kInvGamma) * 255.0f, 0.0f, 255.0f);
				dst[di + 1] = (uint8_t)std::clamp(std::pow(acc1, kInvGamma) * 255.0f, 0.0f, 255.0f);
				dst[di + 2] = (uint8_t)std::clamp(std::pow(acc2, kInvGamma) * 255.0f, 0.0f, 255.0f);
				dst[di + 3] = (uint8_t)std::clamp(acc3 * 255.0f, 0.0f, 255.0f);
			}
		}

		mips.emplace_back(std::move(next));
		cw = nw;
		ch = nh;
	}

	return mips;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
DcompRenderer::CreateTexture2DFromRgbaMips(
	uint32_t width, uint32_t height,
	const std::vector<std::vector<uint8_t>>& mips)
{
	if (mips.empty()) throw std::runtime_error("mips empty.");

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		width, height,
		1, (UINT16)mips.size()
	);

	Microsoft::WRL::ComPtr<ID3D12Resource> tex;
	DX_CALL(m_ctx.Device()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&tex)));

	const UINT64 uploadSize = GetRequiredIntermediateSize(tex.Get(), 0, (UINT)mips.size());

	auto upHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

	Microsoft::WRL::ComPtr<ID3D12Resource> upload;
	DX_CALL(m_ctx.Device()->CreateCommittedResource(
		&upHeap, D3D12_HEAP_FLAG_NONE, &upDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&upload)));

	std::vector<D3D12_SUBRESOURCE_DATA> subs;
	subs.reserve(mips.size());

	uint32_t cw = width, ch = height;
	for (size_t i = 0; i < mips.size(); ++i)
	{
		D3D12_SUBRESOURCE_DATA s{};
		s.pData = mips[i].data();
		s.RowPitch = (LONG_PTR)cw * 4;
		s.SlicePitch = s.RowPitch * ch;
		subs.push_back(s);

		cw = std::max(1u, cw / 2);
		ch = std::max(1u, ch / 2);
	}

	m_uploadAlloc->Reset();
	m_uploadCmdList->Reset(m_uploadAlloc.Get(), nullptr);

	UpdateSubresources(
		m_uploadCmdList.Get(), tex.Get(), upload.Get(),
		0, 0, (UINT)subs.size(), subs.data());

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		tex.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_uploadCmdList->ResourceBarrier(1, &barrier);

	m_uploadCmdList->Close();
	ID3D12CommandList* lists[] = { m_uploadCmdList.Get() };
	m_ctx.Queue()->ExecuteCommandLists(1, lists);

	WaitForGpu();

	return tex;
}

uint32_t DcompRenderer::LoadTextureSrv(const std::filesystem::path& path)
{
	const std::wstring key = path.wstring();

	if (auto it = m_textureCache.find(key); it != m_textureCache.end())
	{
		return it->second;
	}

	if (!std::filesystem::exists(path))
	{
		m_textureCache[key] = m_defaultWhiteSrv;
		return m_defaultWhiteSrv;
	}

	WicImage img = WicTexture::LoadRgba(path);
	auto mips = BuildMipChainRGBA(img.rgba.data(), img.width, img.height);

	const uint32_t srvIndex = AllocSrvIndex();
	auto tex = CreateTexture2DFromRgbaMips(img.width, img.height, mips);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = (UINT)mips.size();

	m_ctx.Device()->CreateShaderResourceView(tex.Get(), &srv, GetSrvCpuHandle(srvIndex));

	m_textures.push_back(GpuTexture{ tex, srvIndex, img.width, img.height });

	m_textureCache[key] = srvIndex;
	return srvIndex;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
DcompRenderer::CreateTexture2DFromRgba(const uint8_t* rgba, uint32_t width, uint32_t height)
{
	if (!rgba || width == 0 || height == 0)
	{
		throw std::runtime_error("CreateTexture2DFromRgba: invalid input.");
	}

	if (!m_uploadAlloc || !m_uploadCmdList)
	{
		CreateUploadObjects();
	}

	auto device = m_ctx.Device();

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM, width, height);

	Microsoft::WRL::ComPtr<ID3D12Resource> tex;
	DX_CALL(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&tex)));

	const UINT64 uploadSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);

	auto upHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

	Microsoft::WRL::ComPtr<ID3D12Resource> upload;
	DX_CALL(device->CreateCommittedResource(
		&upHeap, D3D12_HEAP_FLAG_NONE, &upDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&upload)));

	D3D12_SUBRESOURCE_DATA sub{};
	sub.pData = rgba;
	sub.RowPitch = (LONG_PTR)width * 4;
	sub.SlicePitch = sub.RowPitch * height;

	m_uploadAlloc->Reset();
	m_uploadCmdList->Reset(m_uploadAlloc.Get(), nullptr);

	UpdateSubresources(
		m_uploadCmdList.Get(),
		tex.Get(), upload.Get(),
		0, 0, 1, &sub);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		tex.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	m_uploadCmdList->ResourceBarrier(1, &barrier);

	m_uploadCmdList->Close();
	ID3D12CommandList* lists[] = { m_uploadCmdList.Get() };
	m_ctx.Queue()->ExecuteCommandLists(1, lists);

	WaitForGpu();

	return tex;
}

void DcompRenderer::CreateUploadObjects()
{
	if (m_uploadAlloc && m_uploadCmdList) return;

	DX_CALL(m_ctx.Device()->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadAlloc)));

	DX_CALL(m_ctx.Device()->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_uploadAlloc.Get(), nullptr,
		IID_PPV_ARGS(&m_uploadCmdList)));

	m_uploadCmdList->Close();
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
	m_msaaColor.Reset();
	m_msaaRtvHeap.Reset();
	m_depth.Reset();
}

void DcompRenderer::CreateMsaaTargets()
{
	SelectMaximumMsaa();
	CreateDepthBuffer();

	if (m_msaaSampleCount <= 1)
	{
		m_msaaColor.Reset();
		m_msaaRtvHeap.Reset();
		m_msaaColorState = D3D12_RESOURCE_STATE_COMMON;
		return;
	}

	// Create MSAA color target
	m_msaaColor.Reset();
	m_msaaRtvHeap.Reset();

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
		&clear, IID_PPV_ARGS(&m_msaaColor)));

	m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// RTV heap for MSAA color
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = 1;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	DX_CALL(m_ctx.Device()->CreateDescriptorHeap(
		&rtvDesc, IID_PPV_ARGS(&m_msaaRtvHeap)));

	m_msaaRtvHandle = m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_ctx.Device()->CreateRenderTargetView(
		m_msaaColor.Get(), nullptr, m_msaaRtvHandle);
}

uint32_t DcompRenderer::AllocSrvIndex()
{
	const uint32_t idx = m_nextSrvIndex++;
	return idx;
}

uint32_t DcompRenderer::AllocSrvBlock3()
{
	const uint32_t base = m_nextSrvIndex;
	m_nextSrvIndex += 3;
	return base;
}

void DcompRenderer::CopySrv(uint32_t dstIndex, uint32_t srcIndex)
{
	auto dst = GetSrvCpuHandle(dstIndex);
	auto src = GetSrvCpuHandle(srcIndex);

	m_ctx.Device()->CopyDescriptorsSimple(
		1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DcompRenderer::CreatePmxRootSignature()
{
	// ルートパラメータ:
	// 0: SceneCB (b0) - VS/PS
	// 1: MaterialCB (b1) - VS/PS
	// 2: SRV table (t0, t1, t2) - PS
	// 3: BoneCB (b2) - VS

	CD3DX12_ROOT_PARAMETER params[4]{};

	// SceneCB
	params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

	// MaterialCB
	params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);

	// SRV table (テクスチャ3枚: base, toon, sphere)
	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);
	params[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	// BoneCB
	params[3].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_ANISOTROPIC;
	samp.MaxAnisotropy = 16; // Maximum anisotropic filtering
	samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samp.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samp.MinLOD = 0.0f;
	samp.MaxLOD = D3D12_FLOAT32_MAX;
	samp.MipLODBias = -0.5f;
	samp.ShaderRegister = 0;
	samp.RegisterSpace = 0;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc{};
	rsDesc.Init(
		_countof(params), params,
		1, &samp,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errBlob;

	HRESULT hr = D3D12SerializeRootSignature(
		&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		&sigBlob, &errBlob);

	if (FAILED(hr))
	{
		if (errBlob)
		{
			OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		}
		ThrowIfFailedEx(hr, "D3D12SerializeRootSignature", FILENAME, __LINE__);
	}

	DX_CALL(m_ctx.Device()->CreateRootSignature(
		0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_pmxRootSig)));
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
	m_lightSettings.modelScale += delta;
	m_lightSettings.modelScale = std::clamp(m_lightSettings.modelScale, 0.1f, 8.75f);
}

void DcompRenderer::AddCameraRotation(float dxPixels, float dyPixels)
{
	constexpr float sensitivity = 0.005f;

	m_cameraYaw += dxPixels * sensitivity;
	m_cameraPitch += dyPixels * sensitivity;

	const float limit = DirectX::XM_PIDIV2 - 0.05f;
	m_cameraPitch = std::clamp(m_cameraPitch, -limit, limit);
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
	if (!m_hasContentRect || !PtInRect(&m_lastContentRect, clientPoint))
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
	if (bestFrame == UINT_MAX || !m_readbackMapped[bestFrame])
	{
		return true;
	}

	// 4. ピクセル読み取り
	// R10G10B10A2 フォーマット: 32bit整数
	// Alphaは上位2ビット (30-31ビット目)

	const uint8_t* basePtr = static_cast<const uint8_t*>(m_readbackMapped[bestFrame]);
	const uint32_t pitch = m_readbackFootprint.Footprint.RowPitch;

	// オフセット計算: y * pitch + x * 4bytes
	const uint64_t offset = static_cast<uint64_t>(clientPoint.y) * pitch + static_cast<uint64_t>(clientPoint.x) * 4;

	if (offset >= m_readbackTotalSize) return false;

	const uint32_t pixel = *reinterpret_cast<const uint32_t*>(basePtr + offset);

	// A2チャネルを取り出す (0, 1, 2, 3 のいずれか)
	const uint32_t alpha = (pixel >> 30) & 0x3;

	// アルファが0 (完全透明) なら透過、それ以外(1,2,3)ならヒット
	return (alpha != 0);
}

void DcompRenderer::CreateReadbackBuffers()
{
	// 既存があれば解放（Unmapが必要）
	for (UINT i = 0; i < FrameCount; ++i)
	{
		if (m_readbackBuffers[i])
		{
			m_readbackBuffers[i]->Unmap(0, nullptr);
			m_readbackMapped[i] = nullptr;
			m_readbackBuffers[i].Reset();
		}
	}

	if (m_width == 0 || m_height == 0) return;

	auto device = m_ctx.Device();

	// コピー元のテクスチャ情報 (R10G10B10A2_UNORM)
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R10G10B10A2_UNORM,
		m_width, m_height,
		1, 1
	);

	// コピー先のバッファサイズとレイアウトを計算
	device->GetCopyableFootprints(&desc, 0, 1, 0, &m_readbackFootprint, nullptr, nullptr, &m_readbackTotalSize);

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_readbackTotalSize);

	for (UINT i = 0; i < FrameCount; ++i)
	{
		DX_CALL(device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&m_readbackBuffers[i])));

		// 永続的にマップしておく（ReadbackヒープはMapしたままでもOK）
		CD3DX12_RANGE readRange(0, 0); // CPU書き込みはしないので0-0
		DX_CALL(m_readbackBuffers[i]->Map(0, nullptr, &m_readbackMapped[i]));
	}
}

void DcompRenderer::LoadTexturesForModel(const PmxModel* model,
										 std::function<void(float, const wchar_t*)> onProgress,
										 float startProgress, float endProgress)
{
	if (!model) return;

	const auto& texPaths = model->TexturePaths();
	size_t total = texPaths.size();
	if (total == 0) return;

	if (!m_uploadAlloc || !m_uploadCmdList)
	{
		CreateUploadObjects();
	}

	for (size_t i = 0; i < total; ++i)
	{
		if (onProgress && (i % 5 == 0 || i == total - 1))
		{
			float ratio = (float)i / (float)total;
			float current = startProgress + ratio * (endProgress - startProgress);

			auto buf = std::format(L"テクスチャ読み込み中 ({}/{})...", i + 1, total);
			onProgress(current, buf.c_str());
		}

		LoadTextureSrv(texPaths[i]);
	}
}

DirectX::XMFLOAT3 DcompRenderer::ProjectToScreen(const DirectX::XMFLOAT3& localPos) const
{
	using namespace DirectX;
	if (!m_matricesValid || m_width == 0 || m_height == 0)
	{
		return { 0.0f, 0.0f, 0.0f };
	}

	XMMATRIX M = XMLoadFloat4x4(&m_lastModelMatrix);
	XMMATRIX V = XMLoadFloat4x4(&m_lastViewMatrix);
	XMMATRIX P = XMLoadFloat4x4(&m_lastProjMatrix);

	XMVECTOR local = XMLoadFloat3(&localPos);
	// local.w = 1.0
	local = XMVectorSetW(local, 1.0f);

	// World Space
	XMVECTOR world = XMVector3TransformCoord(local, M);

	// Clip Space
	XMVECTOR clip = XMVector3Transform(world, V * P); // TransformCoordはwで割ってしまうのでTransformを使う

	float w = XMVectorGetW(clip);
	if (w < 0.001f) w = 0.001f;

	// NDC
	XMVECTOR ndc = XMVectorScale(clip, 1.0f / w);

	// Viewport
	float x = (XMVectorGetX(ndc) + 1.0f) * 0.5f * (float)m_width;
	float y = (1.0f - XMVectorGetY(ndc)) * 0.5f * (float)m_height;

	return { x, y, w };
}