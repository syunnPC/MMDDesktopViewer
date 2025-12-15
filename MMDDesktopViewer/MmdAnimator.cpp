#include "MmdAnimator.hpp"
#include "BoneSolver.hpp"
#include "MmdPhysicsWorld.hpp"
#include <stdexcept>
#include <algorithm>
#include <DirectXMath.h>

namespace
{
	float NormalizeFrame(float frame, float maxFrame)
	{
		if (maxFrame <= 0.0f) return frame;
		float cycle = maxFrame;
		while (frame >= cycle) frame -= cycle;
		while (frame < 0.0f) frame += cycle;
		return frame;
	}

	float EvaluateBezier(float t, float x1, float y1, float x2, float y2)
	{
		auto cubic = [](float p0, float p1, float p2, float p3, float s) {
			float inv = 1.0f - s;
			return inv * inv * inv * p0 + 3.0f * inv * inv * s * p1 + 3.0f * inv * s * s * p2 + s * s * s * p3;
			};

		float low = 0.0f, high = 1.0f, s = t;
		for (int i = 0; i < 15; ++i)
		{
			s = 0.5f * (low + high);
			float x = cubic(0.0f, x1, x2, 1.0f, s);
			if (x < t)
				low = s;
			else
				high = s;
		}

		return cubic(0.0f, y1, y2, 1.0f, s);
	}

	float EvaluateChannelT(const std::uint8_t* interp, float t)
	{
		float x1 = interp[0] / 127.0f;
		float y1 = interp[4] / 127.0f;
		float x2 = interp[8] / 127.0f;
		float y2 = interp[12] / 127.0f;
		return EvaluateBezier(t, x1, y1, x2, y2);
	}
}

MmdAnimator::MmdAnimator()
{
	m_lastUpdate = std::chrono::steady_clock::now();
	DirectX::XMStoreFloat4x4(&m_motionTransform, DirectX::XMMatrixIdentity());
	m_boneSolver = std::make_unique<BoneSolver>();
	m_physicsWorld = std::make_unique<MmdPhysicsWorld>();
}

MmdAnimator::~MmdAnimator() = default;


bool MmdAnimator::LoadModel(const std::filesystem::path& pmx)
{
	auto model = std::make_unique<PmxModel>();
	if (model->Load(pmx))
	{
		m_model = std::move(model);
		m_time = 0.0;
		m_pose = {};
		m_hasSkinnedPose = false;

		// ボーンソルバーを初期化
		m_boneSolver->Initialize(m_model.get());
		if (m_physicsWorld) m_physicsWorld->Reset();

		return true;
	}
	return false;
}

bool MmdAnimator::LoadMotion(const std::filesystem::path& vmd)
{
	auto motion = std::make_unique<VmdMotion>();
	if (motion->Load(vmd))
	{
		m_motion = std::move(motion);
		m_time = 0.0;
		m_pose = {};
		m_paused = false;
		DirectX::XMStoreFloat4x4(&m_motionTransform, DirectX::XMMatrixIdentity());
		if (m_physicsWorld) m_physicsWorld->Reset();
		return true;
	}
	return false;
}

void MmdAnimator::ClearMotion()
{
	m_motion.reset();
	m_time = 0.0;
	m_pose = {};
	m_paused = false;
	m_hasSkinnedPose = false;
	DirectX::XMStoreFloat4x4(&m_motionTransform, DirectX::XMMatrixIdentity());
	if (m_physicsWorld) m_physicsWorld->Reset();
}

void MmdAnimator::StopMotion()
{
	ClearMotion();
}

void MmdAnimator::Update()
{
	auto now = std::chrono::steady_clock::now();

	if (m_firstUpdate)
	{
		m_lastUpdate = now;
		m_firstUpdate = false;
		return;
	}

	double dt = std::chrono::duration<double>(now - m_lastUpdate).count();
	m_lastUpdate = now;

	if (dt > 0.1)
	{
		dt = 0.1;
	}

	Tick(dt);
}

void MmdAnimator::Tick(double dtSeconds)
{
	if (m_paused) return;
	m_time += dtSeconds;

	if (!m_model)
	{
		m_hasSkinnedPose = false;
		return;
	}

	const VmdMotion* motion = m_motion.get();
	const float currentFrameRaw = static_cast<float>(m_time * m_fps);
	float currentFrame = 0.0f;
	if (motion)
	{
		const float maxFrame = static_cast<float>(motion->MaxFrame() + 1);
		currentFrame = NormalizeFrame(currentFrameRaw, maxFrame);
	}

	m_pose.boneTranslations.clear();
	m_pose.boneRotations.clear();
	m_pose.morphWeights.clear();
	m_pose.frame = currentFrame;

	bool resetPhysics = false;

	// ループ検知: 前フレームより明確に戻った（例: max付近→0付近）
	if (motion && m_prevFrameForPhysicsValid)
	{
		// 0.5f はノイズ閾値
		if (currentFrame + 0.5f < m_prevFrameForPhysics)
			resetPhysics = true;

		// ついでに「大ジャンプ」もReset（シークやdtスパイク対策）
		if (std::abs(currentFrame - m_prevFrameForPhysics) > 10.0f)
			resetPhysics = true;
	}

	if (resetPhysics && m_physicsWorld)
	{
		m_physicsWorld->Reset(); // 次の Step() で BuildFromModel される
	}

	auto sampleBone = [&](const VmdMotion::BoneTrack& track) {
		if (track.keys.empty()) return;
		const auto& keys = track.keys;

		const VmdMotion::BoneKey* k0 = &keys.front();
		const VmdMotion::BoneKey* k1 = &keys.back();

		for (size_t i = 0; i + 1 < keys.size(); ++i)
		{
			if (keys[i + 1].frame >= currentFrame)
			{
				k0 = &keys[i];
				k1 = &keys[i + 1];
				break;
			}
		}

		float t = 0.0f;
		if (k1->frame != k0->frame)
		{
			t = (currentFrame - static_cast<float>(k0->frame)) /
				static_cast<float>(k1->frame - k0->frame);
			t = std::clamp(t, 0.0f, 1.0f);
		}

		const std::uint8_t* base = k0->interp;
		float txT = EvaluateChannelT(base + 0, t);
		float tyT = EvaluateChannelT(base + 16, t);
		float tzT = EvaluateChannelT(base + 32, t);
		float rotT = EvaluateChannelT(base + 48, t);

		auto lerp = [](float a, float b, float s) { return a + (b - a) * s; };

		DirectX::XMFLOAT3 trans{
			lerp(k0->tx, k1->tx, txT),
			lerp(k0->ty, k1->ty, tyT),
			lerp(k0->tz, k1->tz, tzT)
		};

		DirectX::XMVECTOR q0 = DirectX::XMQuaternionNormalize(
			DirectX::XMVectorSet(k0->qx, k0->qy, k0->qz, k0->qw));
		DirectX::XMVECTOR q1 = DirectX::XMQuaternionNormalize(
			DirectX::XMVectorSet(k1->qx, k1->qy, k1->qz, k1->qw));
		DirectX::XMVECTOR q = DirectX::XMQuaternionSlerp(q0, q1, rotT);
		DirectX::XMFLOAT4 rot;
		DirectX::XMStoreFloat4(&rot, q);

		m_pose.boneTranslations[track.name] = trans;
		m_pose.boneRotations[track.name] = rot;
		};

	if (motion)
	{
		for (const auto& track : motion->BoneTracks())
		{
			sampleBone(track);
		}
	}

	auto sampleMorph = [&](const VmdMotion::MorphTrack& track) {
		if (track.keys.empty()) return;
		const auto& keys = track.keys;
		const VmdMotion::MorphKey* k0 = &keys.front();
		const VmdMotion::MorphKey* k1 = &keys.back();
		for (size_t i = 0; i + 1 < keys.size(); ++i)
		{
			if (keys[i + 1].frame >= currentFrame)
			{
				k0 = &keys[i];
				k1 = &keys[i + 1];
				break;
			}
		}

		float t = 0.0f;
		if (k1->frame != k0->frame)
		{
			t = (currentFrame - static_cast<float>(k0->frame)) /
				static_cast<float>(k1->frame - k0->frame);
			t = std::clamp(t, 0.0f, 1.0f);
		}
		float w = k0->weight + (k1->weight - k0->weight) * t;
		m_pose.morphWeights[track.name] = w;
		};

	if (motion)
	{
		for (const auto& track : motion->MorphTracks())
		{
			sampleMorph(track);
		}
	}

	// ボーンソルバーでスキニング行列を計算
	m_boneSolver->ApplyPose(m_pose);
	m_boneSolver->UpdateMatrices();

	// 見た目寄りの最小物理 (剛体/ジョイントがあるモデルのみ)
	if (m_physicsEnabled && m_physicsWorld && m_model && !m_model->RigidBodies().empty())
	{
		if (!m_physicsWorld->IsBuilt() || m_physicsWorld->BuiltRevision() != m_model->Revision())
		{
			m_physicsWorld->BuildFromModel(*m_model, *m_boneSolver);
		}

		if (m_physicsWorld->IsBuilt())
		{
			m_physicsWorld->Step(dtSeconds, *m_model, *m_boneSolver);
			// 物理の書き戻し後にスキニング行列だけ更新(IKは回さない)
			m_boneSolver->UpdateMatricesNoIK();
		}
	}

	m_hasSkinnedPose = true;
	m_prevFrameForPhysics = currentFrame;
	m_prevFrameForPhysicsValid = true;

	DirectX::XMStoreFloat4x4(&m_motionTransform, DirectX::XMMatrixIdentity());
}

const std::vector<DirectX::XMFLOAT4X4>& MmdAnimator::GetSkinningMatrices() const
{
	return m_boneSolver->GetSkinningMatrices();
}

size_t MmdAnimator::GetBoneCount() const
{
	return m_boneSolver->BoneCount();
}

bool MmdAnimator::LoadModel(const std::filesystem::path& pmx, std::function<void(float, const wchar_t*)> onProgress)
{
	auto model = std::make_unique<PmxModel>();
	// コールバックを渡す
	if (model->Load(pmx, onProgress))
	{
		SetModel(std::move(model));
		return true;
	}
	return false;
}

void MmdAnimator::SetModel(std::unique_ptr<PmxModel> model)
{
	m_model = std::move(model);
	m_time = 0.0;
	m_pose = {};
	m_hasSkinnedPose = false;
	m_boneSolver->Initialize(m_model.get());
	if (m_physicsWorld) m_physicsWorld->Reset();
}

void MmdAnimator::GetBounds(float& minx, float& miny, float& minz, float& maxx, float& maxy, float& maxz) const
{
	if (m_hasSkinnedPose && m_boneSolver)
	{
		DirectX::XMFLOAT3 mn, mx;
		m_boneSolver->GetBoneBounds(mn, mx);
		minx = mn.x; miny = mn.y; minz = mn.z;
		maxx = mx.x; maxy = mx.y; maxz = mx.z;
	}
	else if (m_model)
	{
		// 初期状態のバウンディングボックス
		m_model->GetBounds(minx, miny, minz, maxx, maxy, maxz);
	}
	else
	{
		minx = miny = minz = -1.0f;
		maxx = maxy = maxz = 1.0f;
	}
}