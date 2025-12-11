#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "VmdMotion.hpp"
#include "BinaryReader.hpp"
#include <windows.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace
{
	std::wstring SjisBytesToW(const std::vector<std::uint8_t>& bytes)
	{
		int lenA = 0;
		while (lenA < (int)bytes.size() && bytes[lenA] != 0) ++lenA;
		if (lenA == 0) return {};

		int lenW = MultiByteToWideChar(932, 0, (const char*)bytes.data(), lenA, nullptr, 0);
		if (lenW <= 0) return {};
		std::wstring out(lenW, L'\0');
		MultiByteToWideChar(932, 0, (const char*)bytes.data(), lenA, out.data(), lenW);
		return out;
	}
}

bool VmdMotion::Load(const std::filesystem::path& vmdPath)
{
	m_path = vmdPath;
	m_boneKeys.clear();
	m_morphKeys.clear();

	BinaryReader br(vmdPath);

	// header string (30 bytes)
	auto header = br.ReadBytes(30);
	// loosely validate
	// "Vocaloid Motion Data 0002" etc
	bool ok = false;
	{
		const char* p = (const char*)header.data();
		std::string hs(p, p + header.size());
		if (hs.find("Vocaloid Motion Data") != std::string::npos) ok = true;
	}
	if (!ok)
	{
		throw std::runtime_error("Not a VMD file (header mismatch).");
	}

	// model name (20 bytes, Shift-JIS)
	auto modelNameBytes = br.ReadBytes(20);
	(void)modelNameBytes;

	// bone key count
	auto boneCount = br.Read<std::uint32_t>();
	m_boneKeys.reserve(boneCount);

	for (std::uint32_t i = 0; i < boneCount; ++i)
	{
		BoneKey k{};
		auto nameBytes = br.ReadBytes(15);
		k.boneName = SjisBytesToW(nameBytes);

		k.frame = br.Read<std::uint32_t>();
		k.tx = br.Read<float>(); k.ty = br.Read<float>(); k.tz = br.Read<float>();
		k.qx = br.Read<float>(); k.qy = br.Read<float>(); k.qz = br.Read<float>(); k.qw = br.Read<float>();

		auto interpBytes = br.ReadBytes(64);
		std::memcpy(k.interp, interpBytes.data(), 64);

		m_boneKeys.push_back(std::move(k));
	}

	// morph key count
	auto morphCount = br.Read<std::uint32_t>();
	m_morphKeys.reserve(morphCount);

	for (std::uint32_t i = 0; i < morphCount; ++i)
	{
		MorphKey k{};
		auto nameBytes = br.ReadBytes(15);
		k.morphName = SjisBytesToW(nameBytes);
		k.frame = br.Read<std::uint32_t>();
		k.weight = br.Read<float>();
		m_morphKeys.push_back(std::move(k));
	}

	// camera/light/shadow/ik etc are omitted in this minimal build

	BuildTracks();
	return true;
}

void VmdMotion::BuildTracks()
{
	m_boneTracks.clear();
	m_morphTracks.clear();
	m_maxFrame = 0;

	auto sortBone = m_boneKeys;
	auto sortMorph = m_morphKeys;

	std::sort(sortBone.begin(), sortBone.end(), [](const BoneKey& a, const BoneKey& b) {
		if (a.boneName == b.boneName) return a.frame < b.frame;
		return a.boneName < b.boneName;
			  });

	std::sort(sortMorph.begin(), sortMorph.end(), [](const MorphKey& a, const MorphKey& b) {
		if (a.morphName == b.morphName) return a.frame < b.frame;
		return a.morphName < b.morphName;
			  });

	auto pushBoneTrack = [&](std::wstring current, std::vector<BoneKey>&& keys) {
		if (!current.empty() && !keys.empty())
		{
			m_boneTracks.push_back({ std::move(current), std::move(keys) });
		}
		};

	std::wstring currentBone;
	std::vector<BoneKey> currentKeys;
	for (auto& k : sortBone)
	{
		m_maxFrame = std::max(m_maxFrame, k.frame);
		if (currentBone != k.boneName)
		{
			pushBoneTrack(std::move(currentBone), std::move(currentKeys));
			currentBone = k.boneName;
			currentKeys.clear();
		}
		currentKeys.push_back(std::move(k));
	}
	pushBoneTrack(std::move(currentBone), std::move(currentKeys));

	auto pushMorphTrack = [&](std::wstring current, std::vector<MorphKey>&& keys) {
		if (!current.empty() && !keys.empty())
		{
			m_morphTracks.push_back({ std::move(current), std::move(keys) });
		}
		};

	std::wstring currentMorph;
	std::vector<MorphKey> morphKeys;
	for (auto& k : sortMorph)
	{
		m_maxFrame = std::max(m_maxFrame, k.frame);
		if (currentMorph != k.morphName)
		{
			pushMorphTrack(std::move(currentMorph), std::move(morphKeys));
			currentMorph = k.morphName;
			morphKeys.clear();
		}
		morphKeys.push_back(std::move(k));
	}
	pushMorphTrack(std::move(currentMorph), std::move(morphKeys));
}