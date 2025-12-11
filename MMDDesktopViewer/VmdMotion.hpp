#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

class VmdMotion
{
public:
	struct BoneKey
	{
		std::wstring boneName;
		std::uint32_t frame{};
		float tx{}, ty{}, tz{};
		float qx{}, qy{}, qz{}, qw{};
		std::uint8_t interp[64]{};
	};

	struct MorphKey
	{
		std::wstring morphName;
		std::uint32_t frame{};
		float weight{};
	};

	struct BoneTrack
	{
		std::wstring name;
		std::vector<BoneKey> keys;
	};

	struct MorphTrack
	{
		std::wstring name;
		std::vector<MorphKey> keys;
	};

	bool Load(const std::filesystem::path& vmdPath);

	const std::vector<BoneKey>& BoneKeys() const
	{
		return m_boneKeys;
	}
	const std::vector<MorphKey>& MorphKeys() const
	{
		return m_morphKeys;
	}
	const std::vector<BoneTrack>& BoneTracks() const
	{
		return m_boneTracks;
	}
	const std::vector<MorphTrack>& MorphTracks() const
	{
		return m_morphTracks;
	}

	uint32_t MaxFrame() const
	{
		return m_maxFrame;
	}

private:
	void BuildTracks();
	std::filesystem::path m_path;
	std::vector<BoneKey> m_boneKeys;
	std::vector<MorphKey> m_morphKeys;

	std::vector<BoneTrack> m_boneTracks;
	std::vector<MorphTrack> m_morphTracks;
	uint32_t m_maxFrame{};
};
