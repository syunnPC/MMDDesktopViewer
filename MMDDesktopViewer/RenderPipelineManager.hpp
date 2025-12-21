#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Dx12Context.hpp"

class RenderPipelineManager
{
public:
	void Initialize(Dx12Context* ctx);

	void CreatePmxRootSignature();
	void CreatePmxPipeline(UINT msaaSampleCount, UINT msaaQuality);
	void CreateEdgePipeline(UINT msaaSampleCount, UINT msaaQuality);
	void CreateFxaaPipeline();

	ID3D12RootSignature* GetPmxRootSignature() const
	{
		return m_pmxRootSig.Get();
	}
	ID3D12PipelineState* GetPmxPsoOpaque() const
	{
		return m_pmxPsoOpaque.Get();
	}
	ID3D12PipelineState* GetPmxPsoTrans() const
	{
		return m_pmxPsoTrans.Get();
	}
	ID3D12PipelineState* GetEdgePso() const
	{
		return m_edgePso.Get();
	}
	ID3D12RootSignature* GetFxaaRootSignature() const
	{
		return m_fxaaRootSig.Get();
	}
	ID3D12PipelineState* GetFxaaPso() const
	{
		return m_fxaaPso.Get();
	}

private:
	Dx12Context* m_ctx{};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pmxRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pmxPsoOpaque;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pmxPsoTrans;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_edgePso;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_fxaaRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fxaaPso;
};