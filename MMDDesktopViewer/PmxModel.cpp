#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PmxModel.hpp"
#include "BinaryReader.hpp"
#include <windows.h>
#include <stdexcept>
#include <algorithm>

namespace
{
	std::wstring Utf8ToW(const std::string& s)
	{
		if (s.empty()) return {};
		int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		if (len <= 0) throw std::runtime_error("Utf8ToW failed.");
		std::wstring out(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
		return out;
	}

	std::wstring U16ToW(const std::u16string& s)
	{
		return std::wstring(reinterpret_cast<const wchar_t*>(s.data()), s.size());
	}

	uint64_t g_revisionCounter = 1;
}

std::wstring PmxModel::ReadPmxText(BinaryReader& br) const
{
	if (m_header.encoding == 0)
	{
		auto u16 = br.ReadStringUtf16LeWithLength();
		return U16ToW(u16);
	}
	else if (m_header.encoding == 1)
	{
		auto u8 = br.ReadStringUtf8WithLength();
		return Utf8ToW(u8);
	}
	throw std::runtime_error("Unknown PMX encoding.");
}

int32_t PmxModel::ReadIndexSigned(BinaryReader& br, std::uint8_t size) const
{
	if (size == 1)
	{
		auto v = br.Read<std::int8_t>();
		return (int32_t)v;
	}
	if (size == 2)
	{
		auto v = br.Read<std::int16_t>();
		return (int32_t)v;
	}
	if (size == 4)
	{
		auto v = br.Read<std::int32_t>();
		return v;
	}
	throw std::runtime_error("Unsupported index size.");
}

uint32_t PmxModel::ReadIndexUnsigned(BinaryReader& br, std::uint8_t size) const
{
	if (size == 1)
	{
		auto v = br.Read<std::uint8_t>();
		return static_cast<uint32_t>(v);
	}
	if (size == 2)
	{
		auto v = br.Read<std::uint16_t>();
		return static_cast<uint32_t>(v);
	}
	if (size == 4)
	{
		auto v = br.Read<std::uint32_t>();
		return v;
	}
	throw std::runtime_error("Unsupported index size.");
}

PmxModel::VertexWeight PmxModel::ReadVertexWeight(BinaryReader& br) const
{
	VertexWeight w{};
	w.type = br.Read<std::uint8_t>();

	auto readBone = [&]() -> std::int32_t {
		return ReadIndexSigned(br, m_header.boneIndexSize);
		};

	switch (w.type)
	{
		case 0: // BDEF1
			w.boneIndices[0] = readBone();
			w.weights[0] = 1.0f;
			break;

		case 1: // BDEF2
			w.boneIndices[0] = readBone();
			w.boneIndices[1] = readBone();
			w.weights[0] = br.Read<float>();
			w.weights[1] = 1.0f - w.weights[0];
			break;

		case 2: // BDEF4
			w.boneIndices[0] = readBone();
			w.boneIndices[1] = readBone();
			w.boneIndices[2] = readBone();
			w.boneIndices[3] = readBone();
			w.weights[0] = br.Read<float>();
			w.weights[1] = br.Read<float>();
			w.weights[2] = br.Read<float>();
			w.weights[3] = br.Read<float>();
			break;

		case 3: // SDEF
			w.boneIndices[0] = readBone();
			w.boneIndices[1] = readBone();
			w.weights[0] = br.Read<float>();
			w.weights[1] = 1.0f - w.weights[0];

			w.sdefC.x = br.Read<float>();
			w.sdefC.y = br.Read<float>();
			w.sdefC.z = br.Read<float>();
			w.sdefR0.x = br.Read<float>();
			w.sdefR0.y = br.Read<float>();
			w.sdefR0.z = br.Read<float>();
			w.sdefR1.x = br.Read<float>();
			w.sdefR1.y = br.Read<float>();
			w.sdefR1.z = br.Read<float>();
			break;

		case 4: // QDEF
			w.boneIndices[0] = readBone();
			w.boneIndices[1] = readBone();
			w.boneIndices[2] = readBone();
			w.boneIndices[3] = readBone();
			w.weights[0] = br.Read<float>();
			w.weights[1] = br.Read<float>();
			w.weights[2] = br.Read<float>();
			w.weights[3] = br.Read<float>();
			break;

		default:
			throw std::runtime_error("Unknown weight type.");
	}

	return w;
}

void PmxModel::LoadBones(BinaryReader& br)
{
	auto boneCount = br.Read<std::int32_t>();
	if (boneCount < 0) throw std::runtime_error("Invalid boneCount.");

	m_bones.reserve(static_cast<size_t>(boneCount));

	for (int32_t i = 0; i < boneCount; ++i)
	{
		Bone bone{};

		bone.name = ReadPmxText(br);
		bone.nameEn = ReadPmxText(br);

		bone.position.x = br.Read<float>();
		bone.position.y = br.Read<float>();
		bone.position.z = br.Read<float>();

		bone.parentIndex = ReadIndexSigned(br, m_header.boneIndexSize);
		bone.layer = br.Read<std::int32_t>();
		bone.flags = br.Read<std::uint16_t>();

		// 接続先
		if (bone.flags & 0x0001)
		{
			// ボーンで指定
			bone.tailBoneIndex = ReadIndexSigned(br, m_header.boneIndexSize);
		}
		else
		{
			// オフセットで指定
			bone.tailOffset.x = br.Read<float>();
			bone.tailOffset.y = br.Read<float>();
			bone.tailOffset.z = br.Read<float>();
		}

		// 回転付与または移動付与
		if ((bone.flags & 0x0100) || (bone.flags & 0x0200))
		{
			bone.grantParentIndex = ReadIndexSigned(br, m_header.boneIndexSize);
			bone.grantWeight = br.Read<float>();
		}

		// 軸固定
		if (bone.flags & 0x0400)
		{
			bone.axisDirection.x = br.Read<float>();
			bone.axisDirection.y = br.Read<float>();
			bone.axisDirection.z = br.Read<float>();
		}

		// ローカル軸
		if (bone.flags & 0x0800)
		{
			bone.localAxisX.x = br.Read<float>();
			bone.localAxisX.y = br.Read<float>();
			bone.localAxisX.z = br.Read<float>();
			bone.localAxisZ.x = br.Read<float>();
			bone.localAxisZ.y = br.Read<float>();
			bone.localAxisZ.z = br.Read<float>();
		}

		// 外部親変形
		if (bone.flags & 0x2000)
		{
			bone.externalParentKey = br.Read<std::int32_t>();
		}

		// IK
		if (bone.flags & 0x0020)
		{
			bone.ikTargetIndex = ReadIndexSigned(br, m_header.boneIndexSize);
			bone.ikLoopCount = br.Read<std::int32_t>();
			bone.ikLimitAngle = br.Read<float>();

			auto linkCount = br.Read<std::int32_t>();
			bone.ikLinks.reserve(static_cast<size_t>(linkCount));

			for (int32_t j = 0; j < linkCount; ++j)
			{
				Bone::IKLink link{};
				link.boneIndex = ReadIndexSigned(br, m_header.boneIndexSize);
				link.hasLimit = br.Read<std::uint8_t>() != 0;

				if (link.hasLimit)
				{
					link.limitMin.x = br.Read<float>();
					link.limitMin.y = br.Read<float>();
					link.limitMin.z = br.Read<float>();
					link.limitMax.x = br.Read<float>();
					link.limitMax.y = br.Read<float>();
					link.limitMax.z = br.Read<float>();
				}

				bone.ikLinks.push_back(link);
			}
		}

		m_bones.push_back(std::move(bone));
	}
}

bool PmxModel::Load(const std::filesystem::path& pmxPath, ProgressCallback onProgress)
{
	m_path = pmxPath;

	m_vertices.clear();
	m_indices.clear();
	m_textures.clear();
	m_materials.clear();
	m_bones.clear();

	m_minx = m_miny = m_minz = +std::numeric_limits<float>::infinity();
	m_maxx = m_maxy = m_maxz = -std::numeric_limits<float>::infinity();

	if (onProgress) onProgress(0.05f, L"ヘッダー解析中...");

	BinaryReader br(pmxPath);

	auto sig = br.ReadBytes(4);
	if (sig.size() != 4 || sig[0] != 'P' || sig[1] != 'M' || sig[2] != 'X' || sig[3] != ' ')
	{
		throw std::runtime_error("Not a PMX file.");
	}

	m_header.version = br.Read<float>();

	auto headerSize = br.Read<std::uint8_t>();
	if (headerSize < 8) throw std::runtime_error("Unsupported PMX header size.");

	m_header.encoding = br.Read<std::uint8_t>();
	m_header.additionalUV = br.Read<std::uint8_t>();
	m_header.vertexIndexSize = br.Read<std::uint8_t>();
	m_header.textureIndexSize = br.Read<std::uint8_t>();
	m_header.materialIndexSize = br.Read<std::uint8_t>();
	m_header.boneIndexSize = br.Read<std::uint8_t>();
	m_header.morphIndexSize = br.Read<std::uint8_t>();
	m_header.rigidIndexSize = br.Read<std::uint8_t>();

	if (headerSize > 8) br.Skip(headerSize - 8);

	m_name = ReadPmxText(br);
	m_nameEn = ReadPmxText(br);
	m_comment = ReadPmxText(br);
	m_commentEn = ReadPmxText(br);

	// ----------------
	// Vertices
	// ----------------
	if (onProgress) onProgress(0.1f, L"頂点データを読み込み中...");
	auto vertexCount = br.Read<std::int32_t>();
	if (vertexCount < 0) throw std::runtime_error("Invalid vertexCount.");
	m_vertices.reserve((size_t)vertexCount);

	for (int32_t i = 0; i < vertexCount; ++i)
	{
		Vertex v{};
		v.px = br.Read<float>(); v.py = br.Read<float>(); v.pz = br.Read<float>();
		v.nx = br.Read<float>(); v.ny = br.Read<float>(); v.nz = br.Read<float>();
		v.u = br.Read<float>(); v.v = br.Read<float>();

		// additional UV (each is float4)
		for (uint8_t a = 0; a < m_header.additionalUV; ++a)
		{
			(void)br.Read<float>(); (void)br.Read<float>();
			(void)br.Read<float>(); (void)br.Read<float>();
		}

		// weight block
		v.weight = ReadVertexWeight(br);

		// edge scale
		v.edgeScale = br.Read<float>();

		m_vertices.push_back(v);

		m_minx = std::min(m_minx, v.px); m_miny = std::min(m_miny, v.py); m_minz = std::min(m_minz, v.pz);
		m_maxx = std::max(m_maxx, v.px); m_maxy = std::max(m_maxy, v.py); m_maxz = std::max(m_maxz, v.pz);
	}

	// ----------------
	// Indices
	// ----------------
	if (onProgress) onProgress(0.3f, L"インデックスデータを読み込み中...");
	auto indexCount = br.Read<std::int32_t>();
	if ((indexCount % 3) != 0 || indexCount < 0)
	{
		throw std::runtime_error("Invalid indexCount.");
	}

	m_indices.reserve(static_cast<size_t>(indexCount));

	for (int32_t i = 0; i < indexCount; ++i)
	{
		uint32_t idx = ReadIndexUnsigned(br, m_header.vertexIndexSize);

		if (idx >= static_cast<uint32_t>(vertexCount))
		{
			throw std::runtime_error("Vertex index out of range.");
		}

		m_indices.push_back(idx);
	}

	// ----------------
	// Textures
	// ----------------
	if (onProgress) onProgress(0.4f, L"テクスチャ定義を読み込み中...");
	auto textureCount = br.Read<std::int32_t>();
	if (textureCount < 0) throw std::runtime_error("Invalid textureCount.");
	m_textures.reserve((size_t)textureCount);

	auto baseDir = m_path.parent_path();
	for (int32_t i = 0; i < textureCount; ++i)
	{
		auto rel = ReadPmxText(br);
		std::filesystem::path p = baseDir / rel;
		m_textures.push_back(p);
	}

	// ----------------
	// Materials
	// ----------------
	if (onProgress) onProgress(0.4f, L"マテリアル定義を読み込み中...");
	auto materialCount = br.Read<std::int32_t>();
	if (materialCount < 0) throw std::runtime_error("Invalid materialCount.");
	m_materials.reserve((size_t)materialCount);

	int32_t runningIndexOffset = 0;

	for (int32_t i = 0; i < materialCount; ++i)
	{
		Material m{};
		m.name = ReadPmxText(br);
		m.nameEn = ReadPmxText(br);

		m.diffuse[0] = br.Read<float>();
		m.diffuse[1] = br.Read<float>();
		m.diffuse[2] = br.Read<float>();
		m.diffuse[3] = br.Read<float>();

		m.specular[0] = br.Read<float>();
		m.specular[1] = br.Read<float>();
		m.specular[2] = br.Read<float>();

		m.specularPower = br.Read<float>();

		m.ambient[0] = br.Read<float>();
		m.ambient[1] = br.Read<float>();
		m.ambient[2] = br.Read<float>();

		m.drawFlags = br.Read<std::uint8_t>();

		m.edgeColor[0] = br.Read<float>();
		m.edgeColor[1] = br.Read<float>();
		m.edgeColor[2] = br.Read<float>();
		m.edgeColor[3] = br.Read<float>();
		m.edgeSize = br.Read<float>();

		m.textureIndex = ReadIndexSigned(br, m_header.textureIndexSize);
		m.sphereTextureIndex = ReadIndexSigned(br, m_header.textureIndexSize);
		m.sphereMode = br.Read<std::uint8_t>();

		m.toonFlag = br.Read<std::uint8_t>();
		if (m.toonFlag == 0)
		{
			m.toonIndex = ReadIndexSigned(br, m_header.textureIndexSize);
		}
		else
		{
			m.toonIndex = (int32_t)br.Read<std::uint8_t>();
		}

		m.memo = ReadPmxText(br);

		m.indexCount = br.Read<std::int32_t>();
		if (m.indexCount < 0) throw std::runtime_error("Invalid material indexCount.");

		m.indexOffset = runningIndexOffset;
		runningIndexOffset += m.indexCount;

		m_materials.push_back(std::move(m));
	}

	if (runningIndexOffset != indexCount)
	{
		throw std::runtime_error("Material indexCount total mismatch.");
	}

	// ----------------
	// Bones
	// ----------------
	if (onProgress) onProgress(0.5f, L"ボーン構造を読み込み中...");
	LoadBones(br);

	if (onProgress) onProgress(0.6f, L"PMX解析完了");

	m_revision = g_revisionCounter++;
	return true;
}

void PmxModel::GetBounds(float& minx, float& miny, float& minz,
						 float& maxx, float& maxy, float& maxz) const
{
	minx = m_minx; miny = m_miny; minz = m_minz;
	maxx = m_maxx; maxy = m_maxy; maxz = m_maxz;
}