#include "PmxModelDrawer.hpp"

#include "d3dx12.hpp"
#include "ExceptionHelper.hpp"
#include "DebugUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>

namespace
{
	std::wstring ToLowerW(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
					   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return s;
	}

	static bool ContainsAnyW(const std::wstring& hay, std::initializer_list<const wchar_t*> needles)
	{
		const std::wstring low = ToLowerW(hay);
		for (auto n : needles)
		{
			if (low.find(n) != std::wstring::npos) return true;
		}
		return false;
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

		if (ContainsAnyW(n, { L"–Ú", L"“µ", L"eye", L"iris", L"pupil" }) ||
			ContainsAnyW(ne, { L"eye", L"iris" })) return 4;

		if (ContainsAnyW(n, { L"Šç", L"face", L"–j", L"‚Ù‚Ù" }) ||
			ContainsAnyW(ne, { L"face", L"cheek" })) return 3;

		if (ContainsAnyW(n, { L"”¯", L"hair", L"ƒwƒA" }) ||
			ContainsAnyW(ne, { L"hair" })) return 2;

		if (ContainsAnyW(n, { L"”§", L"skin" }) ||
			ContainsAnyW(ne, { L"skin" })) return 1;

		if (mat.diffuse[3] < 0.98f ||
			ContainsAnyW(n, { L"glass", L"“§–¾" }) ||
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

		if (low.find(L"face") != std::wstring::npos) return true;
		if (low.find(L"facial") != std::wstring::npos) return true;

		if (all.find(L"Šç") != std::wstring::npos) return true;
		if (all.find(L"‚©‚¨") != std::wstring::npos) return true;
		if (all.find(L"“ª•”") != std::wstring::npos) return true;

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
						   L"hitomi", L"matsuge", L"matuge", L"–Ú", L"“µ", L"”’–Ú", L"“øÊ",
						   L"‚Ü‚Â–Ñ", L"‚Ü‚Â‚°", L"áÊ–Ñ", L"ƒAƒCƒ‰ƒCƒ“" }))
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

void PmxModelDrawer::Initialize(Dx12Context* ctx, GpuResourceManager* resources)
{
	m_ctx = ctx;
	m_resources = resources;
}

void PmxModelDrawer::EnsurePmxResources(const PmxModel* model, const LightSettings& lightSettings)
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

	std::vector<PmxVsVertex> vtx;
	vtx.reserve(verts.size());

	const auto boneCount = model->Bones().size();

	for (const auto& v : verts)
	{
		PmxVsVertex pv{};
		pv.px = v.px; pv.py = v.py; pv.pz = v.pz;
		pv.nx = v.nx; pv.ny = v.ny; pv.nz = v.nz;
		pv.u = v.u; pv.v = v.v;

		for (int i = 0; i < 4; ++i)
		{
			pv.boneIndices[i] = -1;
			pv.boneWeights[i] = 0.0f;
		}

		int32_t fallbackBone = -1;
		for (int i = 0; i < 4; ++i)
		{
			int32_t boneIdx = v.weight.boneIndices[i];
			float weight = v.weight.weights[i];

			if (boneIdx >= 0 && boneIdx < static_cast<int32_t>(boneCount) && weight > 0.0f)
			{
				pv.boneIndices[i] = boneIdx;
				pv.boneWeights[i] = weight;
				if (fallbackBone < 0) fallbackBone = boneIdx;
			}
		}

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

	m_baseVertices = vtx;
	m_workingVertices = vtx;
	m_morphWeights.resize(model->Morphs().size());

	const UINT vbSize = static_cast<UINT>(vtx.size() * sizeof(PmxVsVertex));
	const UINT ibSize = static_cast<UINT>(inds.size() * sizeof(uint32_t));

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	{
		auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
		DX_CALL(m_ctx->Device()->CreateCommittedResource(
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

	{
		auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
		DX_CALL(m_ctx->Device()->CreateCommittedResource(
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

	{
		const size_t matCount = mats.size();
		const UINT64 totalSize = m_materialCbStride * matCount;

		if (totalSize > 0)
		{
			auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
			DX_CALL(m_ctx->Device()->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(&m_materialCb)));

			CD3DX12_RANGE range(0, 0);
			DX_CALL(m_materialCb->Map(0, &range, reinterpret_cast<void**>(&m_materialCbMapped)));
		}
	}

	m_pmx.materials.clear();
	m_pmx.materials.reserve(mats.size());

	for (size_t mi = 0; mi < mats.size(); ++mi)
	{
		const auto& mat = mats[mi];
		PmxGpuMaterial gm{};
		gm.mat = mat;

		float edgeSize = mat.edgeSize;
		if (IsEyeOrLashMaterial(mat, texPaths))
		{
			edgeSize = 0.0f;
		}
		gm.mat.edgeSize = edgeSize;

		uint32_t matType = GuessMaterialType(mat);
		float rimMul, specMul, shadowMul, toonContrastMul;
		GetMaterialStyleParams(matType, rimMul, specMul, shadowMul, toonContrastMul);

		gm.srvBlockIndex = m_resources->AllocSrvBlock3();

		uint32_t baseSrv = m_resources->GetDefaultWhiteSrv();
		if (mat.textureIndex >= 0 && mat.textureIndex < static_cast<int32_t>(texPaths.size()))
		{
			baseSrv = m_resources->LoadTextureSrv(texPaths[mat.textureIndex]);
		}
		m_resources->CopySrv(gm.srvBlockIndex + 0, baseSrv);

		uint32_t toonSrv = m_resources->GetDefaultToonSrv();
		if (mat.toonFlag == 0 && mat.toonIndex >= 0 && mat.toonIndex < static_cast<int32_t>(texPaths.size()))
		{
			toonSrv = m_resources->LoadTextureSrv(texPaths[mat.toonIndex]);
		}
		m_resources->CopySrv(gm.srvBlockIndex + 1, toonSrv);

		uint32_t sphereSrv = m_resources->GetDefaultWhiteSrv();
		if (mat.sphereTextureIndex >= 0 && mat.sphereTextureIndex < static_cast<int32_t>(texPaths.size()))
		{
			sphereSrv = m_resources->LoadTextureSrv(texPaths[mat.sphereTextureIndex]);
		}
		m_resources->CopySrv(gm.srvBlockIndex + 2, sphereSrv);

		const bool isFace = LooksLikeFaceMaterial(mat);
		if (isFace)
		{
			shadowMul = lightSettings.faceShadowMul;
			toonContrastMul = lightSettings.faceToonContrastMul;
		}

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

void PmxModelDrawer::UpdateMaterialSettings(const LightSettings& lightSettings)
{
	if (!m_pmx.ready || !m_materialCbMapped) return;

	for (size_t mi = 0; mi < m_pmx.materials.size(); ++mi)
	{
		auto& gm = m_pmx.materials[mi];

		uint32_t matType = GuessMaterialType(gm.mat);
		float rimMul, specMul, shadowMul, toonContrastMul;
		GetMaterialStyleParams(matType, rimMul, specMul, shadowMul, toonContrastMul);

		if (LooksLikeFaceMaterial(gm.mat))
		{
			shadowMul = lightSettings.faceShadowMul;
			toonContrastMul = lightSettings.faceToonContrastMul;
		}

		MaterialCB* mcb = reinterpret_cast<MaterialCB*>(m_materialCbMapped + mi * m_materialCbStride);
		mcb->shadowMul = shadowMul;
		mcb->toonContrastMul = toonContrastMul;
	}
}

void PmxModelDrawer::AddMorphWeight(const PmxModel* model, int morphIndex, float weight, std::vector<float>& totalWeights)
{
	if (morphIndex < 0 || morphIndex >= static_cast<int>(totalWeights.size())) return;

	const auto& m = model->Morphs()[morphIndex];

	if (m.type == PmxModel::Morph::Type::Group)
	{
		for (const auto& offset : m.groupOffsets)
		{
			AddMorphWeight(model, offset.morphIndex, weight * offset.weight, totalWeights);
		}
	}
	else
	{
		totalWeights[morphIndex] += weight;
	}
}

void PmxModelDrawer::UpdatePmxMorphs(const MmdAnimator& animator)
{
	if (!m_pmx.ready) return;
	const auto* model = animator.Model();
	if (!model) return;

	const auto& morphs = model->Morphs();
	if (morphs.empty()) return;

	if (m_morphWeights.size() != morphs.size())
	{
		m_morphWeights.resize(morphs.size());
	}
	std::fill(m_morphWeights.begin(), m_morphWeights.end(), 0.0f);

	const auto& currentPose = animator.CurrentPose();

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

	if (m_baseVertices.empty()) return;

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
		else if (m.type == PmxModel::Morph::Type::UV)
		{
			vertexDirty = true;
			for (const auto& uvo : m.uvOffsets)
			{
				if (uvo.vertexIndex < m_workingVertices.size())
				{
					auto& v = m_workingVertices[uvo.vertexIndex];
					v.u += uvo.offset.x * w;
					v.v += uvo.offset.y * w;
				}
			}
		}
	}

	if (vertexDirty && m_pmx.vb)
	{
		void* mapped = nullptr;
		CD3DX12_RANGE range(0, 0);
		if (SUCCEEDED(m_pmx.vb->Map(0, &range, &mapped)))
		{
			std::memcpy(mapped, m_workingVertices.data(), m_workingVertices.size() * sizeof(PmxVsVertex));
			m_pmx.vb->Unmap(0, nullptr);
		}
	}

	for (size_t mi = 0; mi < m_pmx.materials.size(); ++mi)
	{
		const auto& gm = m_pmx.materials[mi];

		MaterialCB* cb = reinterpret_cast<MaterialCB*>(m_materialCbMapped + mi * m_materialCbStride);

		cb->diffuse = { gm.mat.diffuse[0], gm.mat.diffuse[1], gm.mat.diffuse[2], gm.mat.diffuse[3] };
		cb->specular = { gm.mat.specular[0], gm.mat.specular[1], gm.mat.specular[2] };
		cb->specPower = gm.mat.specularPower;
		cb->ambient = { gm.mat.ambient[0], gm.mat.ambient[1], gm.mat.ambient[2] };
		cb->edgeColor = { gm.mat.edgeColor[0], gm.mat.edgeColor[1], gm.mat.edgeColor[2], gm.mat.edgeColor[3] };
		cb->edgeSize = gm.mat.edgeSize;

		for (size_t i = 0; i < morphs.size(); ++i)
		{
			float w = m_morphWeights[i];
			if (std::abs(w) < 0.0001f) continue;

			const auto& m = morphs[i];
			if (m.type == PmxModel::Morph::Type::Material)
			{
				for (const auto& mo : m.materialOffsets)
				{
					if (mo.materialIndex == -1 || mo.materialIndex == static_cast<int>(mi))
					{
						if (mo.operation == 0)
						{
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
						}
						else if (mo.operation == 1)
						{
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

		auto saturateColor = [](float& v) { v = std::clamp(v, 0.0f, 1.0f); };
		saturateColor(cb->diffuse.x); saturateColor(cb->diffuse.y); saturateColor(cb->diffuse.z); saturateColor(cb->diffuse.w);
		saturateColor(cb->specular.x); saturateColor(cb->specular.y); saturateColor(cb->specular.z);
		saturateColor(cb->ambient.x); saturateColor(cb->ambient.y); saturateColor(cb->ambient.z);
		saturateColor(cb->edgeColor.x); saturateColor(cb->edgeColor.y); saturateColor(cb->edgeColor.z); saturateColor(cb->edgeColor.w);
		cb->edgeSize = std::max(0.0f, cb->edgeSize);
	}
}

void PmxModelDrawer::UpdateBoneMatrices(const MmdAnimator& animator, BoneCB* dst)
{
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