#pragma once

#include <d3d12.h>
#include <winrt/base.h>

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
		return m_pmxRootSig.get();
	}
	ID3D12PipelineState* GetPmxPsoOpaque() const
	{
		return m_pmxPsoOpaque.get();
	}
	ID3D12PipelineState* GetPmxPsoTrans() const
	{
		return m_pmxPsoTrans.get();
	}
	ID3D12PipelineState* GetEdgePso() const
	{
		return m_edgePso.get();
	}
	ID3D12RootSignature* GetFxaaRootSignature() const
	{
		return m_fxaaRootSig.get();
	}
	ID3D12PipelineState* GetFxaaPso() const
	{
		return m_fxaaPso.get();
	}

private:
	Dx12Context* m_ctx{};

	winrt::com_ptr<ID3D12RootSignature> m_pmxRootSig;
	winrt::com_ptr<ID3D12PipelineState> m_pmxPsoOpaque;
	winrt::com_ptr<ID3D12PipelineState> m_pmxPsoTrans;
	winrt::com_ptr<ID3D12PipelineState> m_edgePso;

	winrt::com_ptr<ID3D12RootSignature> m_fxaaRootSig;
	winrt::com_ptr<ID3D12PipelineState> m_fxaaPso;
};
