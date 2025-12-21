#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Dx12Context.hpp"

class GpuResourceManager
{
public:
	struct GpuTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint32_t srvIndex{};
		uint32_t width{};
		uint32_t height{};
	};

	void Initialize(Dx12Context* ctx, std::function<void()> waitForGpu, UINT frameCount);

	void CreateSrvHeap();
	void CreateUploadObjects();
	void ResetTextureCache();

	uint32_t LoadTextureSrv(const std::filesystem::path& path);

	ID3D12DescriptorHeap* GetSrvHeap() const
	{
		return m_srvHeap.Get();
	}
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t index) const;

	uint32_t AllocSrvIndex();
	uint32_t AllocSrvBlock3();
	void CopySrv(uint32_t dstIndex, uint32_t srcIndex);

	uint32_t GetDefaultWhiteSrv() const
	{
		return m_defaultWhiteSrv;
	}
	uint32_t GetDefaultToonSrv() const
	{
		return m_defaultToonSrv;
	}

	void CreateReadbackBuffers(UINT width, UINT height);
	ID3D12Resource* GetReadbackBuffer(UINT index) const;
	void* GetReadbackMapped(UINT index) const;
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& GetReadbackFootprint() const
	{
		return m_readbackFootprint;
	}
	UINT64 GetReadbackTotalSize() const
	{
		return m_readbackTotalSize;
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture2DFromRgba(
		const uint8_t* rgba, uint32_t width, uint32_t height);

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture2DFromRgbaMips(
		uint32_t width, uint32_t height,
		const std::vector<std::vector<uint8_t>>& mips);

	uint32_t CreateWhiteTexture1x1();
	uint32_t CreateDefaultToonRamp();

	void ResetReadbackBuffers();

	Dx12Context* m_ctx{};
	std::function<void()> m_waitForGpu;
	UINT m_frameCount{};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	UINT m_srvDescriptorSize{};

	std::vector<GpuTexture> m_textures;
	std::unordered_map<std::wstring, uint32_t> m_textureCache;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_uploadAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_uploadCmdList;

	uint32_t m_nextSrvIndex = 0;
	uint32_t m_defaultWhiteSrv = 0;
	uint32_t m_defaultToonSrv = 0;

	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_readbackBuffers;
	std::vector<void*> m_readbackMapped;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_readbackFootprint{};
	UINT64 m_readbackTotalSize{};
};