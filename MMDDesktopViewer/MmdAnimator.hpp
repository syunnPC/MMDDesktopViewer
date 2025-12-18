#pragma once
#include <filesystem>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <DirectXMath.h>
#include "PmxModel.hpp"
#include "VmdMotion.hpp"
#include "BoneSolver.hpp"

class MmdPhysicsWorld;

class MmdAnimator
{
public:
	using Pose = BonePose;

	MmdAnimator();
	~MmdAnimator();

	bool LoadModel(const std::filesystem::path& pmx);
	bool LoadModel(const std::filesystem::path& pmx, std::function<void(float, const wchar_t*)> onProgress);
	void SetModel(std::unique_ptr<PmxModel> model);

	bool LoadMotion(const std::filesystem::path& vmd);
	void ClearMotion();
	void StopMotion();

	void Tick(double dtSeconds);
	void Update();

	bool IsPaused() const
	{
		return m_paused;
	}
	void SetPaused(bool paused)
	{
		m_paused = paused;
	}
	void TogglePause()
	{
		m_paused = !m_paused;
	}

	bool PhysicsEnabled() const
	{
		return m_physicsEnabled;
	}
	void SetPhysicsEnabled(bool enabled)
	{
		m_physicsEnabled = enabled;
	}
	void TogglePhysics()
	{
		m_physicsEnabled = !m_physicsEnabled;
	}

	const PmxModel* Model() const
	{
		return m_model.get();
	}
	const VmdMotion* Motion() const
	{
		return m_motion.get();
	}

	const Pose& CurrentPose() const
	{
		return m_pose;
	}
	const DirectX::XMFLOAT4X4& MotionTransform() const
	{
		return m_motionTransform;
	}

	double TimeSeconds() const
	{
		return m_time;
	}
	uint64_t ModelRevision() const
	{
		return m_model ? m_model->Revision() : 0;
	}

	const std::vector<DirectX::XMFLOAT4X4>& GetSkinningMatrices() const;
	size_t GetBoneCount() const;
	bool HasSkinnedPose() const
	{
		return m_hasSkinnedPose;
	}

	void GetBounds(float& minx, float& miny, float& minz, float& maxx, float& maxy, float& maxz) const;

private:
	std::unique_ptr<PmxModel> m_model;
	std::unique_ptr<VmdMotion> m_motion;
	std::unique_ptr<BoneSolver> m_boneSolver;

	std::unique_ptr<MmdPhysicsWorld> m_physicsWorld;
	bool m_physicsEnabled{ true };

	double m_time{};
	double m_fps{ 30.0 };

	std::chrono::steady_clock::time_point m_lastUpdate;
	bool m_paused{ false };
	bool m_firstUpdate{ true };
	bool m_hasSkinnedPose{ false };

	Pose m_pose{};
	DirectX::XMFLOAT4X4 m_motionTransform{};

	float m_prevFrameForPhysics{ 0.0f };
	bool  m_prevFrameForPhysicsValid{ false };

	// キャッシュ用メンバ変数
	const VmdMotion* m_cachedMotionPtr = nullptr;
	std::vector<int> m_boneTrackToBoneIndex;     // トラック番号 -> モデルのボーンIndex
	std::vector<int> m_morphTrackToMorphIndex;   // トラック番号 -> モデルのモーフIndex
	std::vector<size_t> m_boneKeyCursors;        // キーフレーム探索用カーソル
	std::vector<size_t> m_morphKeyCursors;       // モーフキー探索用カーソル

	// 内部メソッド定義
	void UpdateMotionCache(const VmdMotion* motion);
};