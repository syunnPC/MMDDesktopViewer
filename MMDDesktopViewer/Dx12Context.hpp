#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class Dx12Context
{
public:
	void Initialize();
	ID3D12Device* Device() const
	{
		return m_device.Get();
	}
	IDXGIFactory4* Factory() const
	{
		return m_factory.Get();
	}
	ID3D12CommandQueue* Queue() const
	{
		return m_queue.Get();
	}

private:
	void CreateFactory();
	void CreateDevice();
	void CreateQueue();

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
};
