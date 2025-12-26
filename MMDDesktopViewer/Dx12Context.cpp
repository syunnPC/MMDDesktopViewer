#include "Dx12Context.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"
#include <stdexcept>
#include <vector>
#include <algorithm>

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
	DX_CALL(CreateDXGIFactory2(flags, IID_PPV_ARGS(m_factory.put())));
}

void Dx12Context::CreateDevice()
{
	struct Candidate
	{
		winrt::com_ptr<IDXGIAdapter1> adapter;
		DXGI_ADAPTER_DESC1 desc{};
		int64_t score{};
	};
	std::vector<Candidate> candidates;

	winrt::com_ptr<IDXGIFactory6> factory6;
	m_factory.as(__uuidof(IDXGIFactory6), factory6.put_void());

	auto collectAdapter = [&](IDXGIAdapter1* rawAdapter)
		{
			if (!rawAdapter) return;
			Candidate c{};
			c.adapter = rawAdapter;
			c.adapter->GetDesc1(&c.desc);
			if (c.desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return;
			candidates.push_back(std::move(c));
		};

	if (factory6)
	{
		for (UINT i = 0;; ++i)
		{
			winrt::com_ptr<IDXGIAdapter1> adapter;
			if (factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.put())) == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}
			collectAdapter(adapter.get());
		}
	}

	if (candidates.empty())
	{
		for (UINT i = 0;; ++i)
		{
			winrt::com_ptr<IDXGIAdapter1> adapter;
			if (m_factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
			collectAdapter(adapter.get());
		}
	}

	SYSTEM_POWER_STATUS power{};
	const bool onBattery = (GetSystemPowerStatus(&power) && power.ACLineStatus == 0);
	const bool preferDiscrete = !onBattery;

	auto scoreAdapter = [&](const DXGI_ADAPTER_DESC1& desc) -> int64_t
		{
			const bool discrete = desc.DedicatedVideoMemory > 0;
			const bool perfVendor =
				(desc.VendorId == 0x10DE) || // NVIDIA
				(desc.VendorId == 0x1002) || // AMD
				(desc.VendorId == 0x1022);   // AMD Alt (APU)

			int64_t score = static_cast<int64_t>(std::min<UINT64>(desc.DedicatedVideoMemory / (1024ull * 1024ull), 500'000ull));
			if (discrete)
			{
				score += preferDiscrete ? 1'000'000 : 50'000;
			}
			else if (onBattery)
			{
				score += 200'000;
			}
			if (perfVendor) score += 100'000;
			return score;
		};

	for (auto& c : candidates)
	{
		c.score = scoreAdapter(c.desc);
	}

	std::sort(candidates.begin(), candidates.end(),
			  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

	for (const auto& c : candidates)
	{
		if (SUCCEEDED(D3D12CreateDevice(c.adapter.get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_device.put()))))
		{
			return;
		}
	}

	// Fallback to WARP only if needed; but you said DX12 is available.
	DX_CALL(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_device.put())));
}

void Dx12Context::CreateQueue()
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	DX_CALL(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(m_queue.put())));
}
