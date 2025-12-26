#include "RenderPipelineManager.hpp"

#include "d3dx12.hpp"
#include "ExceptionHelper.hpp"
#include "FileUtil.hpp"
#include "DebugUtil.hpp"

#include <d3dcompiler.h>
#include <filesystem>

#pragma comment(lib, "d3dcompiler.lib")

void RenderPipelineManager::Initialize(Dx12Context* ctx)
{
	m_ctx = ctx;
}

void RenderPipelineManager::CreatePmxRootSignature()
{
	CD3DX12_ROOT_PARAMETER params[4]{};

	params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);
	params[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	params[3].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_ANISOTROPIC;
	samp.MaxAnisotropy = 16;
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

	winrt::com_ptr<ID3DBlob> sigBlob;
	winrt::com_ptr<ID3DBlob> errBlob;

	HRESULT hr = D3D12SerializeRootSignature(
		&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		sigBlob.put(), errBlob.put());

	if (FAILED(hr))
	{
		if (errBlob)
		{
			OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		}
		ThrowIfFailedEx(hr, "D3D12SerializeRootSignature", FILENAME, __LINE__);
	}

	DX_CALL(m_ctx->Device()->CreateRootSignature(
		0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(m_pmxRootSig.put())));
}

void RenderPipelineManager::CreatePmxPipeline(UINT msaaSampleCount, UINT msaaQuality)
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

	winrt::com_ptr<ID3DBlob> vsBlob, psBlob, errBlob;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), vsBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vsBlob.put(), errBlob.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vsBlob.put(), errBlob.put());
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile PMX VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), psBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, psBlob.put(), errBlob.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, psBlob.put(), errBlob.put());
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile PMX PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.get(), pscompiled.c_str(), FALSE);
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
		pso.pRootSignature = m_pmxRootSig.get();
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
		pso.SampleDesc.Count = msaaSampleCount;
		pso.SampleDesc.Quality = msaaQuality;
		pso.SampleMask = UINT_MAX;
		return pso;
		};

	{
		auto pso = MakeBaseDesc();
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		DX_CALL(m_ctx->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_pmxPsoOpaque.put())));
	}
	{
		auto pso = MakeBaseDesc();
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		DX_CALL(m_ctx->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_pmxPsoTrans.put())));
	}
}

void RenderPipelineManager::CreateEdgePipeline(UINT msaaSampleCount, UINT msaaQuality)
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

	winrt::com_ptr<ID3DBlob> vsBlob, psBlob, errBlob;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), vsBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.put(), errBlob.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.put(), errBlob.put());
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile Edge VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), psBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.put(), errBlob.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.put(), errBlob.put());
	}

	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile Edge PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.get(), pscompiled.c_str(), FALSE);
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
	pso.pRootSignature = m_pmxRootSig.get();
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
	pso.SampleDesc.Count = msaaSampleCount;
	pso.SampleDesc.Quality = msaaQuality;
	pso.SampleMask = UINT_MAX;

	DX_CALL(m_ctx->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_edgePso.put())));
}

void RenderPipelineManager::CreateFxaaPipeline()
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

	winrt::com_ptr<ID3DBlob> sigBlob, errBlob;
	DX_CALL(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.put(), errBlob.put()));
	DX_CALL(m_ctx->Device()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(m_fxaaRootSig.put())));

	auto base = FileUtil::GetExecutableDir();
	std::wstring psname = base / L"Shaders\\FXAA_PS.hlsl";
	std::wstring vsname = base / L"Shaders\\FXAA_VS.hlsl";
	std::wstring pscompiled = base / L"Shaders\\Compiled_FXAA_PS.cso";
	std::wstring vscompiled = base / L"Shaders\\Compiled_FXAA_VS.cso";

	winrt::com_ptr<ID3DBlob> vsBlob, psBlob, err;

	HRESULT hr;

	if (std::filesystem::exists(vscompiled))
	{
		hr = D3DReadFileToBlob(vscompiled.c_str(), vsBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.put(), err.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(vsname.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.put(), err.put());
	}

	if (FAILED(hr))
	{
		if (err) OutputDebugStringA(static_cast<char*>(err->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile FXAA VS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(vscompiled))
	{
		D3DWriteBlobToFile(vsBlob.get(), vscompiled.c_str(), FALSE);
	}

	if (std::filesystem::exists(pscompiled))
	{
		hr = D3DReadFileToBlob(pscompiled.c_str(), psBlob.put());
		if (FAILED(hr))
		{
			hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.put(), err.put());
		}
	}
	else
	{
		hr = D3DCompileFromFile(psname.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.put(), err.put());
	}

	if (FAILED(hr))
	{
		if (err) OutputDebugStringA(static_cast<char*>(err->GetBufferPointer()));
		ThrowIfFailedEx(hr, "D3DCompile FXAA PS", FILENAME, __LINE__);
	}

	if (!std::filesystem::exists(pscompiled))
	{
		D3DWriteBlobToFile(psBlob.get(), pscompiled.c_str(), FALSE);
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_fxaaRootSig.get();
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
	pso.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
	pso.SampleDesc.Count = 1;

	DX_CALL(m_ctx->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_fxaaPso.put())));
}
