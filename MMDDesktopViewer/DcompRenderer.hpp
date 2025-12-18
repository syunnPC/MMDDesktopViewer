#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl.h>

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <filesystem>
#include <cstdint>
#include <DirectXMath.h>
#include <atomic>

#include "WicTexture.hpp"
#include "Dx12Context.hpp"
#include "MmdAnimator.hpp"
#include "PmxModel.hpp"
#include "Settings.hpp"

class DcompRenderer
{
public:
	DcompRenderer() = default;
	~DcompRenderer();

	DcompRenderer(const DcompRenderer&) = delete;
	DcompRenderer& operator=(const DcompRenderer&) = delete;

	using ProgressCallback = std::function<void(float, const wchar_t*)>;

	void Initialize(HWND hwnd, ProgressCallback progress = {});
	void Render(const MmdAnimator& animator);

	void SetLightSettings(const LightSettings& light);
	const LightSettings& GetLightSettings() const
	{
		return m_lightSettings;
	}

	void AdjustBrightness(float delta);
	static constexpr UINT FrameCount = 3;
	static constexpr size_t MaxBones = 1024;

	void AdjustScale(float delta);
	void AddCameraRotation(float dxPixels, float dyPixels);
	void AddModelOffsetPixels(float dxPixels, float dyPixels);
	bool IsPointOnModel(const POINT& clientPoint);

	void LoadTexturesForModel(const PmxModel* model,
							  std::function<void(float, const wchar_t*)> onProgress,
							  float startProgress, float endProgress);

private:
	void CreateD3D();
	void CreateSwapChain();
	void CreateDComp();
	void CreateCommandObjects();
	void CreateRenderTargets();
	void WaitForGpu();
	void ResizeIfNeeded();
	void UpdateWindowBounds(float minx, float miny, float minz, float maxx, float maxy, float maxz,
							const DirectX::XMMATRIX& model, const DirectX::XMMATRIX& view,
							const DirectX::XMMATRIX& proj);
	void ReportProgress(float value, const wchar_t* msg);

	void UpdateMaterialSettings();

	HWND m_hwnd{};
	Dx12Context m_ctx;
	DirectX::XMFLOAT2 m_modelOffset{ 0.f, 0.f };

	LightSettings m_lightSettings;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain1;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_alloc[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	std::atomic<UINT64> m_fenceValue{};
	HANDLE m_fenceEvent{};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

	Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcompDevice;
	Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
	Microsoft::WRL::ComPtr<IDCompositionVisual> m_dcompVisual;

	UINT m_width{};
	UINT m_height{};

	ProgressCallback m_progressCallback;
	float m_baseClientWidth{ 1.0f };
	float m_baseClientHeight{ 1.0f };

	RECT m_lastContentRect{};
	bool m_hasContentRect{ false };

	float m_cameraYaw{ 0.0f };
	float m_cameraPitch{ 0.0f };
	float m_cameraDistance{ 2.5f };

	void CreatePmxPipeline();
	void EnsurePmxResources(const PmxModel* model);
	void UpdateBoneMatrices(const MmdAnimator& animator, UINT frameIndex);

	struct PmxVsVertex
	{
		float px, py, pz;
		float nx, ny, nz;
		float u, v;
		int32_t boneIndices[4];
		float boneWeights[4];
		float sdefC[3];
		float sdefR0[3];
		float sdefR1[3];
		uint32_t weightType;
	};

	struct alignas(16) SceneCB
	{
		DirectX::XMFLOAT4X4 model;
		DirectX::XMFLOAT4X4 view;
		DirectX::XMFLOAT4X4 proj;
		DirectX::XMFLOAT4X4 mvp;

		DirectX::XMFLOAT3 lightDir0; float ambient;
		DirectX::XMFLOAT3 lightColor0; float lightInt0;

		DirectX::XMFLOAT3 lightDir1; float lightInt1;
		DirectX::XMFLOAT3 lightColor1; float _pad1;

		DirectX::XMFLOAT3 cameraPos; float specPower;
		DirectX::XMFLOAT3 specColor; float specStrength;

		DirectX::XMFLOAT4 normalMatrixRow0;
		DirectX::XMFLOAT4 normalMatrixRow1;
		DirectX::XMFLOAT4 normalMatrixRow2;

		float brightness{ 1.3f };
		uint32_t enableSkinning{ 0 };
		float toonContrast{ 1.15f };
		float shadowHueShift{ -0.1396f };

		float shadowSaturation{ 0.25f };
		float rimWidth{ 0.6f };
		float rimIntensity{ 0.35f };
		float specularStep{ 0.3f };

		uint32_t enableToon{ 1 };

		float outlineRefDistance{ 2.5f };
		float outlineDistanceScale{ 1.0f };
		float outlineDistancePower{ 0.8f };
		float shadowRampShift{ 0.0f };

		float shadowDeepThreshold;
		float shadowDeepSoftness;
		float shadowDeepMul;
		float globalSaturation;
	};

	struct alignas(16) BoneCB
	{
		DirectX::XMFLOAT4X4 boneMatrices[MaxBones];
	};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pmxRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pmxPso;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneCb[FrameCount];
	SceneCB* m_sceneCbMapped[FrameCount] = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_boneCb[FrameCount];
	BoneCB* m_boneCbMapped[FrameCount] = {};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depth;
	D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle{};

	void CreateDepthBuffer();
	void CreateSceneBuffers();
	void CreateBoneBuffers();

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	UINT m_srvDescriptorSize{};

	struct GpuTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint32_t srvIndex{};
		uint32_t width{};
		uint32_t height{};
	};

	std::vector<GpuTexture> m_textures;
	std::unordered_map<std::wstring, uint32_t> m_textureCache;

	struct PmxGpuMaterial
	{
		PmxModel::Material mat;
		uint32_t srvBlockIndex{};
		D3D12_GPU_VIRTUAL_ADDRESS materialCbGpu{};
	};

	struct PmxGpu
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> vb;
		Microsoft::WRL::ComPtr<ID3D12Resource> ib;
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		D3D12_INDEX_BUFFER_VIEW ibv{};
		std::vector<PmxGpuMaterial> materials;
		uint32_t indexCount{};
		uint64_t revision{};
		bool ready{ false };
	};

	PmxGpu m_pmx;

	void CreateSrvHeap();
	uint32_t CreateWhiteTexture1x1();
	uint32_t LoadTextureSrv(const std::filesystem::path& path);

	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t index) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle(uint32_t index) const;

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture2DFromRgba(
		const uint8_t* rgba, uint32_t width, uint32_t height);

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_uploadAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_uploadCmdList;

	void CreateUploadObjects();

	UINT64 m_frameFenceValues[FrameCount]{};

	void WaitForFrame(UINT frameIndex);

	UINT m_msaaSampleCount = 4;
	UINT m_msaaQuality = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_msaaColor;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_msaaRtvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_msaaRtvHandle{};

	void UpdateMsaaSettings();
	void CreateMsaaTargets();
	void ReleaseMsaaTargets();

	D3D12_RESOURCE_STATES m_msaaColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	struct alignas(16) MaterialCB
	{
		DirectX::XMFLOAT4 diffuse;
		DirectX::XMFLOAT3 ambient;     float _pad0{};
		DirectX::XMFLOAT3 specular;    float specPower{};

		uint32_t sphereMode{};
		float edgeSize{};
		float rimMul{ 1.0f };
		float specMul{ 1.0f };

		DirectX::XMFLOAT4 edgeColor;

		uint32_t materialType{ 0 };
		float shadowMul{ 1.0f };
		float toonContrastMul{ 1.0f };
		float _pad2{};
	};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_materialCb;
	uint8_t* m_materialCbMapped = nullptr;
	UINT64 m_materialCbStride = 256;

	uint32_t CreateDefaultToonRamp();
	uint32_t m_nextSrvIndex = 0;
	uint32_t m_defaultWhiteSrv = 0;
	uint32_t m_defaultToonSrv = 0;

	uint32_t AllocSrvIndex();
	void CopySrv(uint32_t dstIndex, uint32_t srcIndex);
	uint32_t AllocSrvBlock3();

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pmxPsoOpaque;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pmxPsoTrans;

	Microsoft::WRL::ComPtr<ID3D12Resource>
		CreateTexture2DFromRgbaMips(
			uint32_t width, uint32_t height,
			const std::vector<std::vector<uint8_t>>& mips);

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_edgePso;

	void CreateEdgePipeline();
	void CreatePmxRootSignature();
	void SelectMaximumMsaa();

	void CreateReadbackBuffers();
	Microsoft::WRL::ComPtr<ID3D12Resource> m_readbackBuffers[FrameCount];
	void* m_readbackMapped[FrameCount]{};
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_readbackFootprint{};
	UINT64 m_readbackTotalSize{};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_fxaaRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fxaaPso;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_intermediateTex;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_intermediateRtvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_intermediateRtvHandle{};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_intermediateSrvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_intermediateSrvCpuHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE m_intermediateSrvGpuHandle{};

	void CreateFxaaPipeline();
	void CreateIntermediateResources();
	void ReleaseIntermediateResources();

	HDC m_layeredDc = nullptr;
	HBITMAP m_layeredBmp = nullptr;
	HGDIOBJ m_layeredOld = nullptr;
	void* m_layeredBits = nullptr;

	void RecreateLayeredBitmap();
	void PresentLayered(UINT frameIndex);
};