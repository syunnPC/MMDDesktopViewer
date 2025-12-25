#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PmxModel.hpp"
#include "PmxLoader.hpp"
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
	return PmxLoader::LoadModel(pmxPath, *this, onProgress);
}

void PmxModel::GetBounds(float& minx, float& miny, float& minz,
						 float& maxx, float& maxy, float& maxz) const
{
	minx = m_minx; miny = m_miny; minz = m_minz;
	maxx = m_maxx; maxy = m_maxy; maxz = m_maxz;
}

void PmxModel::LoadMorphs(BinaryReader& br)
{
	const int32_t morphCount = br.Read<std::int32_t>();
	if (morphCount < 0) throw std::runtime_error("Invalid morphCount.");

	m_morphs.clear();
	m_morphs.reserve(static_cast<size_t>(morphCount));

	for (int32_t i = 0; i < morphCount; ++i)
	{
		Morph morph{};
		morph.name = ReadPmxText(br);
		morph.nameEn = ReadPmxText(br);
		morph.panel = br.Read<std::uint8_t>();
		morph.type = static_cast<Morph::Type>(br.Read<std::uint8_t>());

		const int32_t offsetCount = br.Read<std::int32_t>();
		if (offsetCount < 0) throw std::runtime_error("Invalid morph offsetCount.");

		// オフセットデータの読み込み
		switch (morph.type)
		{
			case Morph::Type::Group:
				morph.groupOffsets.reserve(offsetCount);
				break;
			case Morph::Type::Vertex:
				morph.vertexOffsets.reserve(offsetCount);
				break;
			case Morph::Type::Bone:
				morph.boneOffsets.reserve(offsetCount);
				break;
			case Morph::Type::UV:
			case Morph::Type::AdditionalUV1:
			case Morph::Type::AdditionalUV2:
			case Morph::Type::AdditionalUV3:
			case Morph::Type::AdditionalUV4:
				morph.uvOffsets.reserve(offsetCount);
				break;
			case Morph::Type::Material:
				morph.materialOffsets.reserve(offsetCount);
				break;
			case Morph::Type::Flip:
				morph.flipOffsets.reserve(offsetCount);
				break;
			case Morph::Type::Impulse:
				morph.impulseOffsets.reserve(offsetCount);
				break;
		}

		for (int32_t k = 0; k < offsetCount; ++k)
		{
			switch (morph.type)
			{
				case Morph::Type::Group:
				{
					Morph::GroupOffset o{};
					o.morphIndex = ReadIndexSigned(br, m_header.morphIndexSize);
					o.weight = br.Read<float>();
					morph.groupOffsets.push_back(o);
					break;
				}
				case Morph::Type::Vertex:
				{
					Morph::VertexOffset o{};
					o.vertexIndex = ReadIndexUnsigned(br, m_header.vertexIndexSize);
					o.positionOffset.x = br.Read<float>();
					o.positionOffset.y = br.Read<float>();
					o.positionOffset.z = br.Read<float>();
					morph.vertexOffsets.push_back(o);
					break;
				}
				case Morph::Type::Bone:
				{
					Morph::BoneOffset o{};
					o.boneIndex = ReadIndexSigned(br, m_header.boneIndexSize);
					o.translation.x = br.Read<float>();
					o.translation.y = br.Read<float>();
					o.translation.z = br.Read<float>();
					o.rotation.x = br.Read<float>();
					o.rotation.y = br.Read<float>();
					o.rotation.z = br.Read<float>();
					o.rotation.w = br.Read<float>();
					morph.boneOffsets.push_back(o);
					break;
				}
				case Morph::Type::UV:
				case Morph::Type::AdditionalUV1:
				case Morph::Type::AdditionalUV2:
				case Morph::Type::AdditionalUV3:
				case Morph::Type::AdditionalUV4:
				{
					Morph::UVOffset o{};
					o.vertexIndex = ReadIndexUnsigned(br, m_header.vertexIndexSize);
					o.offset.x = br.Read<float>();
					o.offset.y = br.Read<float>();
					o.offset.z = br.Read<float>();
					o.offset.w = br.Read<float>();
					morph.uvOffsets.push_back(o);
					break;
				}
				case Morph::Type::Material:
				{
					Morph::MaterialOffset o{};
					o.materialIndex = ReadIndexSigned(br, m_header.materialIndexSize);
					o.operation = br.Read<std::uint8_t>();

					o.diffuse.x = br.Read<float>(); o.diffuse.y = br.Read<float>(); o.diffuse.z = br.Read<float>(); o.diffuse.w = br.Read<float>();
					o.specular.x = br.Read<float>(); o.specular.y = br.Read<float>(); o.specular.z = br.Read<float>();
					o.specularPower = br.Read<float>();
					o.ambient.x = br.Read<float>(); o.ambient.y = br.Read<float>(); o.ambient.z = br.Read<float>();
					o.edgeColor.x = br.Read<float>(); o.edgeColor.y = br.Read<float>(); o.edgeColor.z = br.Read<float>(); o.edgeColor.w = br.Read<float>();
					o.edgeSize = br.Read<float>();
					o.textureFactor.x = br.Read<float>(); o.textureFactor.y = br.Read<float>(); o.textureFactor.z = br.Read<float>(); o.textureFactor.w = br.Read<float>();
					o.sphereTextureFactor.x = br.Read<float>(); o.sphereTextureFactor.y = br.Read<float>(); o.sphereTextureFactor.z = br.Read<float>(); o.sphereTextureFactor.w = br.Read<float>();
					o.toonTextureFactor.x = br.Read<float>(); o.toonTextureFactor.y = br.Read<float>(); o.toonTextureFactor.z = br.Read<float>(); o.toonTextureFactor.w = br.Read<float>();

					morph.materialOffsets.push_back(o);
					break;
				}
				case Morph::Type::Flip:
				{
					Morph::FlipOffset o{};
					o.morphIndex = ReadIndexSigned(br, m_header.morphIndexSize);
					o.weight = br.Read<float>();
					morph.flipOffsets.push_back(o);
					break;
				}
				case Morph::Type::Impulse:
				{
					Morph::ImpulseOffset o{};
					o.rigidBodyIndex = ReadIndexSigned(br, m_header.rigidIndexSize);
					o.localFlag = br.Read<std::uint8_t>();
					o.velocity.x = br.Read<float>(); o.velocity.y = br.Read<float>(); o.velocity.z = br.Read<float>();
					o.torque.x = br.Read<float>(); o.torque.y = br.Read<float>(); o.torque.z = br.Read<float>();
					morph.impulseOffsets.push_back(o);
					break;
				}
				default:
					throw std::runtime_error("Unknown morph type.");
			}
		}

		m_morphs.push_back(std::move(morph));
	}
}

void PmxModel::LoadFrames(BinaryReader& br)
{
	const int32_t frameCount = br.Read<std::int32_t>();
	if (frameCount < 0) throw std::runtime_error("Invalid frameCount.");

	for (int32_t i = 0; i < frameCount; ++i)
	{
		(void)ReadPmxText(br);
		(void)ReadPmxText(br);
		(void)br.Read<std::uint8_t>(); // specialFlag
		const int32_t elemCount = br.Read<std::int32_t>();
		if (elemCount < 0) throw std::runtime_error("Invalid frame elemCount.");

		for (int32_t k = 0; k < elemCount; ++k)
		{
			const std::uint8_t elemType = br.Read<std::uint8_t>(); // 0: bone, 1: morph
			if (elemType == 0)
			{
				(void)ReadIndexSigned(br, m_header.boneIndexSize);
			}
			else if (elemType == 1)
			{
				(void)ReadIndexSigned(br, m_header.morphIndexSize);
			}
			else
			{
				throw std::runtime_error("Unknown frame element type.");
			}
		}
	}
}

void PmxModel::LoadRigidBodies(BinaryReader& br)
{
	m_rigidBodies.clear();

	const int32_t rigidCount = br.Read<std::int32_t>();
	if (rigidCount < 0) throw std::runtime_error("Invalid rigidCount.");
	m_rigidBodies.reserve(static_cast<size_t>(rigidCount));

	for (int32_t i = 0; i < rigidCount; ++i)
	{
		RigidBody rb{};
		rb.name = ReadPmxText(br);
		rb.nameEn = ReadPmxText(br);

		rb.boneIndex = ReadIndexSigned(br, m_header.boneIndexSize);
		rb.groupIndex = br.Read<std::uint8_t>();
		rb.ignoreCollisionGroup = br.Read<std::uint16_t>();

		rb.shapeType = static_cast<RigidBody::ShapeType>(br.Read<std::uint8_t>());

		rb.shapeSize.x = br.Read<float>();
		rb.shapeSize.y = br.Read<float>();
		rb.shapeSize.z = br.Read<float>();

		rb.position.x = br.Read<float>();
		rb.position.y = br.Read<float>();
		rb.position.z = br.Read<float>();

		rb.rotation.x = br.Read<float>();
		rb.rotation.y = br.Read<float>();
		rb.rotation.z = br.Read<float>();

		rb.mass = br.Read<float>();
		rb.linearDamping = br.Read<float>();
		rb.angularDamping = br.Read<float>();
		rb.restitution = br.Read<float>();
		rb.friction = br.Read<float>();

		rb.operation = static_cast<RigidBody::OperationType>(br.Read<std::uint8_t>());

		m_rigidBodies.push_back(std::move(rb));
	}
}

void PmxModel::LoadJoints(BinaryReader& br)
{
	m_joints.clear();

	const int32_t jointCount = br.Read<std::int32_t>();
	if (jointCount < 0) throw std::runtime_error("Invalid jointCount.");
	m_joints.reserve(static_cast<size_t>(jointCount));

	for (int32_t i = 0; i < jointCount; ++i)
	{
		Joint j{};
		j.name = ReadPmxText(br);
		j.nameEn = ReadPmxText(br);

		j.operation = static_cast<Joint::OperationType>(br.Read<std::uint8_t>());

		j.rigidBodyA = ReadIndexSigned(br, m_header.rigidIndexSize);
		j.rigidBodyB = ReadIndexSigned(br, m_header.rigidIndexSize);

		j.position.x = br.Read<float>();
		j.position.y = br.Read<float>();
		j.position.z = br.Read<float>();

		j.rotation.x = br.Read<float>();
		j.rotation.y = br.Read<float>();
		j.rotation.z = br.Read<float>();

		j.positionLower.x = br.Read<float>();
		j.positionLower.y = br.Read<float>();
		j.positionLower.z = br.Read<float>();

		j.positionUpper.x = br.Read<float>();
		j.positionUpper.y = br.Read<float>();
		j.positionUpper.z = br.Read<float>();

		j.rotationLower.x = br.Read<float>();
		j.rotationLower.y = br.Read<float>();
		j.rotationLower.z = br.Read<float>();

		j.rotationUpper.x = br.Read<float>();
		j.rotationUpper.y = br.Read<float>();
		j.rotationUpper.z = br.Read<float>();

		j.springPosition.x = br.Read<float>();
		j.springPosition.y = br.Read<float>();
		j.springPosition.z = br.Read<float>();

		j.springRotation.x = br.Read<float>();
		j.springRotation.y = br.Read<float>();
		j.springRotation.z = br.Read<float>();

		m_joints.push_back(std::move(j));
	}
}