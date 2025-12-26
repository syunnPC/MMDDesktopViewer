#pragma once

#include <d3d12.h>
#include <winrt/base.h>

#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Dx12Context.hpp"
#include "GpuResourceManager.hpp"
#include "MmdAnimator.hpp"
#include "PmxModel.hpp"
#include "Settings.hpp"

class PmxModelDrawer
{
public:
	static constexpr size_t MaxBones = 1024;

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

	struct alignas(16) BoneCB
	{
		DirectX::XMFLOAT4X4 boneMatrices[MaxBones];
	};

	struct PmxGpuMaterial
	{
		PmxModel::Material mat;
		uint32_t srvBlockIndex{};
		D3D12_GPU_VIRTUAL_ADDRESS materialCbGpu{};
	};

	struct PmxGpu
	{
		winrt::com_ptr<ID3D12Resource> vb;
		winrt::com_ptr<ID3D12Resource> ib;
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		D3D12_INDEX_BUFFER_VIEW ibv{};
		std::vector<PmxGpuMaterial> materials;
		uint32_t indexCount{};
		uint64_t revision{};
		bool ready{ false };
	};

	void Initialize(Dx12Context* ctx, GpuResourceManager* resources);

	void EnsurePmxResources(const PmxModel* model, const LightSettings& lightSettings);
	void UpdatePmxMorphs(const MmdAnimator& animator);
	void UpdateBoneMatrices(const MmdAnimator& animator, BoneCB* dst);
	void UpdateMaterialSettings(const LightSettings& lightSettings);

	const PmxGpu& GetPmx() const
	{
		return m_pmx;
	}
	bool IsReady() const
	{
		return m_pmx.ready;
	}

	ID3D12Resource* GetMaterialCb() const
	{
		return m_materialCb.get();
	}
	uint8_t* GetMaterialCbMapped() const
	{
		return m_materialCbMapped;
	}
	UINT64 GetMaterialCbStride() const
	{
		return m_materialCbStride;
	}

private:
	void AddMorphWeight(const PmxModel* model, int morphIndex, float weight, std::vector<float>& totalWeights);

	Dx12Context* m_ctx{};
	GpuResourceManager* m_resources{};

	PmxGpu m_pmx;

	winrt::com_ptr<ID3D12Resource> m_materialCb;
	uint8_t* m_materialCbMapped = nullptr;
	UINT64 m_materialCbStride = 256;

	std::vector<PmxVsVertex> m_baseVertices;
	std::vector<PmxVsVertex> m_workingVertices;
	std::vector<float> m_morphWeights;
};
