#include "PmxLoader.hpp"
#include "PmxModel.hpp"
#include "BinaryReader.hpp"
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace
{
	uint64_t g_revisionCounter = 1;

	void EnsureRemaining(BinaryReader& br, size_t bytes, const char* context)
	{
		if (bytes > br.Remaining())
		{
			throw std::runtime_error(std::string("PMX truncated while reading ") + context);
		}
	}
}

bool PmxLoader::LoadModel(const std::filesystem::path& pmxPath, PmxModel& model, PmxModel::ProgressCallback onProgress)
{
	model.m_path = pmxPath;

	model.m_vertices.clear();
	model.m_indices.clear();
	model.m_textures.clear();
	model.m_materials.clear();
	model.m_bones.clear();
	model.m_morphs.clear();
	model.m_rigidBodies.clear();
	model.m_joints.clear();

	model.m_minx = model.m_miny = model.m_minz = +std::numeric_limits<float>::infinity();
	model.m_maxx = model.m_maxy = model.m_maxz = -std::numeric_limits<float>::infinity();

	if (onProgress) onProgress(0.05f, L"ヘッダー解析中...");

	BinaryReader br(pmxPath);

	EnsureRemaining(br, 4, "signature");
	auto sig = br.ReadBytes(4);
	if (sig.size() != 4 || sig[0] != 'P' || sig[1] != 'M' || sig[2] != 'X' || sig[3] != ' ')
	{
		throw std::runtime_error("Not a PMX file.");
	}

	model.m_header.version = br.Read<float>();

	auto headerSize = br.Read<std::uint8_t>();
	if (headerSize < 8) throw std::runtime_error("Unsupported PMX header size.");

	model.m_header.encoding = br.Read<std::uint8_t>();
	model.m_header.additionalUV = br.Read<std::uint8_t>();
	model.m_header.vertexIndexSize = br.Read<std::uint8_t>();
	model.m_header.textureIndexSize = br.Read<std::uint8_t>();
	model.m_header.materialIndexSize = br.Read<std::uint8_t>();
	model.m_header.boneIndexSize = br.Read<std::uint8_t>();
	model.m_header.morphIndexSize = br.Read<std::uint8_t>();
	model.m_header.rigidIndexSize = br.Read<std::uint8_t>();

	if (headerSize > 8) br.Skip(headerSize - 8);

	model.m_name = model.ReadPmxText(br);
	model.m_nameEn = model.ReadPmxText(br);
	model.m_comment = model.ReadPmxText(br);
	model.m_commentEn = model.ReadPmxText(br);

	// ----------------
	// Vertices
	// ----------------
	if (onProgress) onProgress(0.1f, L"頂点データを読み込み中...");
	auto vertexCount = br.Read<std::int32_t>();
	if (vertexCount < 0) throw std::runtime_error("Invalid vertexCount.");
	model.m_vertices.reserve(static_cast<size_t>(vertexCount));

	const size_t minVertexBytes = static_cast<size_t>(vertexCount) * static_cast<size_t>(38 + 16 * model.m_header.additionalUV);
	EnsureRemaining(br, minVertexBytes, "vertex block");

	for (int32_t i = 0; i < vertexCount; ++i)
	{
		PmxModel::Vertex v{};
		v.px = br.Read<float>(); v.py = br.Read<float>(); v.pz = br.Read<float>();
		v.nx = br.Read<float>(); v.ny = br.Read<float>(); v.nz = br.Read<float>();
		v.u = br.Read<float>(); v.v = br.Read<float>();

		// additional UV (each is float4)
		for (uint8_t a = 0; a < model.m_header.additionalUV; ++a)
		{
			(void)br.Read<float>(); (void)br.Read<float>();
			(void)br.Read<float>(); (void)br.Read<float>();
		}

		// weight block
		v.weight = model.ReadVertexWeight(br);

		// edge scale
		v.edgeScale = br.Read<float>();

		model.m_vertices.push_back(v);

		model.m_minx = std::min(model.m_minx, v.px); model.m_miny = std::min(model.m_miny, v.py); model.m_minz = std::min(model.m_minz, v.pz);
		model.m_maxx = std::max(model.m_maxx, v.px); model.m_maxy = std::max(model.m_maxy, v.py); model.m_maxz = std::max(model.m_maxz, v.pz);
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

	model.m_indices.reserve(static_cast<size_t>(indexCount));
	EnsureRemaining(br, static_cast<size_t>(indexCount) * model.m_header.vertexIndexSize, "indices");

	for (int32_t i = 0; i < indexCount; ++i)
	{
		uint32_t idx = model.ReadIndexUnsigned(br, model.m_header.vertexIndexSize);

		if (idx >= static_cast<uint32_t>(vertexCount))
		{
			throw std::runtime_error("Vertex index out of range.");
		}

		model.m_indices.push_back(idx);
	}

	// ----------------
	// Textures
	// ----------------
	if (onProgress) onProgress(0.4f, L"テクスチャ定義を読み込み中...");
	auto textureCount = br.Read<std::int32_t>();
	if (textureCount < 0) throw std::runtime_error("Invalid textureCount.");
	model.m_textures.reserve((size_t)textureCount);

	auto baseDir = model.m_path.parent_path();
	for (int32_t i = 0; i < textureCount; ++i)
	{
		auto rel = model.ReadPmxText(br);
		std::filesystem::path p = baseDir / rel;
		model.m_textures.push_back(p);
	}

	// ----------------
	// Materials
	// ----------------
	if (onProgress) onProgress(0.4f, L"マテリアル定義を読み込み中...");
	auto materialCount = br.Read<std::int32_t>();
	if (materialCount < 0) throw std::runtime_error("Invalid materialCount.");
	model.m_materials.reserve((size_t)materialCount);

	int32_t runningIndexOffset = 0;

	for (int32_t i = 0; i < materialCount; ++i)
	{
		PmxModel::Material m{};
		m.name = model.ReadPmxText(br);
		m.nameEn = model.ReadPmxText(br);

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

		m.textureIndex = model.ReadIndexSigned(br, model.m_header.textureIndexSize);
		m.sphereTextureIndex = model.ReadIndexSigned(br, model.m_header.textureIndexSize);
		m.sphereMode = br.Read<std::uint8_t>();

		m.toonFlag = br.Read<std::uint8_t>();
		if (m.toonFlag == 0)
		{
			m.toonIndex = model.ReadIndexSigned(br, model.m_header.textureIndexSize);
		}
		else
		{
			m.toonIndex = (int32_t)br.Read<std::uint8_t>();
		}

		m.memo = model.ReadPmxText(br);

		m.indexCount = br.Read<std::int32_t>();
		if (m.indexCount < 0) throw std::runtime_error("Invalid material indexCount.");

		m.indexOffset = runningIndexOffset;
		runningIndexOffset += m.indexCount;

		model.m_materials.push_back(std::move(m));
	}

	if (runningIndexOffset != indexCount)
	{
		throw std::runtime_error("Material indexCount total mismatch.");
	}

	// ----------------
	// Bones
	// ----------------
	if (onProgress) onProgress(0.5f, L"ボーン構造を読み込み中...");
	model.LoadBones(br);

	// ----------------
	// Morph / DisplayFrame / Physics
	// ----------------
	model.LoadMorphs(br);
	model.LoadFrames(br);
	model.LoadRigidBodies(br);
	model.LoadJoints(br);

	if (onProgress) onProgress(0.6f, L"PMX解析完了");

	model.m_revision = g_revisionCounter++;
	return true;
}