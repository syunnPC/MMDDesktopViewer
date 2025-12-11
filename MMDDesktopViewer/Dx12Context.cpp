#include "Dx12Context.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"
#include <stdexcept>

void Dx12Context::Initialize()
{
	CreateFactory();
	CreateDevice();
	CreateQueue();
}

void Dx12Context::CreateFactory()
{
	UINT flags = 0;
#if defined(_DEBUG)
	flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	DX_CALL(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)));
}

void Dx12Context::CreateDevice()
{
	Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

	for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapter.Reset();
			continue;
		}
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
		{
			return;
		}
		adapter.Reset();
	}

	// Fallback to WARP only if needed; but you said DX12 is available.
	DX_CALL(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
}

void Dx12Context::CreateQueue()
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	DX_CALL(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue)));
}
