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
		SetModel(std::move(model));
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

void MmdAnimator::UpdateMotionCache(const VmdMotion* motion)
{
	// キャッシュが有効なら何もしない
	if (motion && m_cachedMotionPtr == motion && m_model &&
		m_boneTrackToBoneIndex.size() == motion->BoneTracks().size() &&
		m_morphTrackToMorphIndex.size() == motion->MorphTracks().size())
	{
		return;
	}

	m_cachedMotionPtr = motion;
	m_boneTrackToBoneIndex.clear();
	m_morphTrackToMorphIndex.clear();
	m_boneKeyCursors.clear();
	m_morphKeyCursors.clear();

	if (!motion || !m_model) return;

	// --- ボーンのマッピング ---
	const auto& boneTracks = motion->BoneTracks();
	const auto& bones = m_model->Bones();

	m_boneTrackToBoneIndex.resize(boneTracks.size(), -1);
	m_boneKeyCursors.resize(boneTracks.size(), 0);

	// 名前検索用マップを作成 (O(N) + O(M))
	std::unordered_map<std::wstring, int> boneMap;
	boneMap.reserve(bones.size());
	for (int i = 0; i < (int)bones.size(); ++i)
	{
		boneMap[bones[i].name] = i;
	}

	for (size_t i = 0; i < boneTracks.size(); ++i)
	{
		auto it = boneMap.find(boneTracks[i].name);
		if (it != boneMap.end())
		{
			m_boneTrackToBoneIndex[i] = it->second;
		}
	}

	const auto& morphTracks = motion->MorphTracks();
	m_morphTrackToMorphIndex.resize(morphTracks.size(), -1);
	m_morphKeyCursors.resize(morphTracks.size(), 0);
}

void MmdAnimator::CacheLookAtBones()
{
	m_boneIdxHead = -1;
	m_boneIdxNeck = -1;
	m_boneIdxEyeL = -1;
	m_boneIdxEyeR = -1;

	if (!m_model) return;

	const auto& bones = m_model->Bones();
	for (int i = 0; i < (int)bones.size(); ++i)
	{
		const auto& name = bones[i].name;
		if (name == L"頭") m_boneIdxHead = i;
		else if (name == L"首") m_boneIdxNeck = i;
		else if (name == L"左目") m_boneIdxEyeL = i;
		else if (name == L"右目") m_boneIdxEyeR = i;
	}
}

void MmdAnimator::Tick(double dtSeconds)
{
	if (!m_paused)
	{
		m_time += dtSeconds;
	}

	if (!m_model)
	{
		m_hasSkinnedPose = false;
		return;
	}

	const VmdMotion* motion = m_motion.get();

	// キャッシュ更新
	UpdateMotionCache(motion);

	const float currentFrameRaw = static_cast<float>(m_time * m_fps);
	float currentFrame = 0.0f;
	if (motion)
	{
		const float maxFrame = static_cast<float>(motion->MaxFrame() + 1);
		currentFrame = NormalizeFrame(currentFrameRaw, maxFrame);
	}

	// 物理リセット判定
	if (motion && m_prevFrameForPhysicsValid)
	{
		if (currentFrame + 0.5f < m_prevFrameForPhysics ||
			std::abs(currentFrame - m_prevFrameForPhysics) > 10.0f)
		{
			if (m_physicsWorld) m_physicsWorld->Reset();
		}
	}

	// ポーズ初期化
	m_pose.boneTranslations.clear();
	m_pose.boneRotations.clear();
	m_pose.morphWeights.clear();
	m_pose.frame = currentFrame;

	if (motion)
	{
		// --- ボーンアニメーション適用 ---
		const auto& boneTracks = motion->BoneTracks();
		const size_t numBoneTracks = boneTracks.size();

		for (size_t i = 0; i < numBoneTracks; ++i)
		{
			if (m_boneTrackToBoneIndex[i] == -1) continue;

			const auto& track = boneTracks[i];
			const auto& keys = track.keys;
			if (keys.empty()) continue;

			size_t kIdx = m_boneKeyCursors[i];
			if (kIdx >= keys.size() - 1) kIdx = 0;
			if (keys[kIdx].frame > currentFrame) kIdx = 0;

			while (kIdx + 1 < keys.size() && keys[kIdx + 1].frame <= currentFrame)
			{
				kIdx++;
			}
			m_boneKeyCursors[i] = kIdx;

			const auto& k0 = keys[kIdx];
			const auto* k1 = (kIdx + 1 < keys.size()) ? &keys[kIdx + 1] : &k0;

			float t = 0.0f;
			if (k1->frame != k0.frame)
			{
				t = (currentFrame - static_cast<float>(k0.frame)) /
					static_cast<float>(k1->frame - k0.frame);
				t = std::clamp(t, 0.0f, 1.0f);
			}

			const std::uint8_t* base = k0.interp;
			float txT = EvaluateChannelT(base + 0, t);
			float tyT = EvaluateChannelT(base + 16, t);
			float tzT = EvaluateChannelT(base + 32, t);
			float rotT = EvaluateChannelT(base + 48, t);

			auto lerp = [](float a, float b, float s) { return a + (b - a) * s; };

			DirectX::XMFLOAT3 trans{
				lerp(k0.tx, k1->tx, txT),
				lerp(k0.ty, k1->ty, tyT),
				lerp(k0.tz, k1->tz, tzT)
			};

			using namespace DirectX;
			XMVECTOR q0 = XMQuaternionNormalize(XMVectorSet(k0.qx, k0.qy, k0.qz, k0.qw));
			XMVECTOR q1 = XMQuaternionNormalize(XMVectorSet(k1->qx, k1->qy, k1->qz, k1->qw));
			XMVECTOR q = XMQuaternionSlerp(q0, q1, rotT);
			XMFLOAT4 rot;
			XMStoreFloat4(&rot, q);

			if (track.name == L"全ての親")
			{
				trans = { 0.0f, 0.0f, 0.0f };
			}
			else if (track.name == L"センター" || track.name == L"グルーブ")
			{
				trans.x = 0.0f; trans.z = 0.0f;
			}

			m_pose.boneTranslations[track.name] = trans;
			m_pose.boneRotations[track.name] = rot;
		}

		// --- モーフアニメーション適用 ---
		const auto& morphTracks = motion->MorphTracks();
		const size_t numMorphTracks = morphTracks.size();

		for (size_t i = 0; i < numMorphTracks; ++i)
		{
			const auto& track = morphTracks[i];
			const auto& keys = track.keys;
			if (keys.empty()) continue;

			size_t kIdx = m_morphKeyCursors[i];
			if (kIdx >= keys.size() - 1) kIdx = 0;
			if (keys[kIdx].frame > currentFrame) kIdx = 0;

			while (kIdx + 1 < keys.size() && keys[kIdx + 1].frame <= currentFrame)
			{
				kIdx++;
			}
			m_morphKeyCursors[i] = kIdx;

			const auto& k0 = keys[kIdx];
			const auto* k1 = (kIdx + 1 < keys.size()) ? &keys[kIdx + 1] : &k0;

			float t = 0.0f;
			if (k1->frame != k0.frame)
			{
				t = (currentFrame - static_cast<float>(k0.frame)) /
					static_cast<float>(k1->frame - k0.frame);
				t = std::clamp(t, 0.0f, 1.0f);
			}

			float w = k0.weight + (k1->weight - k0.weight) * t;
			m_pose.morphWeights[track.name] = w;
		}
	}

	if (m_lookAtEnabled && m_model)
	{
		using namespace DirectX;

		// 基本設定値 (ラジアン)
		const float baseDeadZone = XMConvertToRadians(15.0f);   // 通常のデッドゾーン
		const float deadZoneUp = XMConvertToRadians(5.0f);     // 上向き時のデッドゾーン（狭める＝すぐ首が動く）

		const float maxNeckYaw = XMConvertToRadians(50.0f);     // 首の横回転制限
		const float maxNeckPitchUp = XMConvertToRadians(25.0f);   // 上向き首制限 (破綻防止のため少し控えめに)
		const float maxNeckPitchDown = XMConvertToRadians(35.0f); // 下向き首制限
		const float maxEye = XMConvertToRadians(20.0f);         // 目の回転制限

		// ヘルパー: 目と首の配分計算
		auto ComputeBoneAngles = [&](float target, float deadZone, float maxNeck, float& outNeck, float& outEye)
			{
				if (std::abs(target) <= deadZone)
				{
					outNeck = 0.0f;
					outEye = target;
				}
				else
				{
					float sign = (target >= 0.0f) ? 1.0f : -1.0f;
					float excess = target - (sign * deadZone);
					float neck = std::clamp(excess, -maxNeck, maxNeck);
					outNeck = neck;
					outEye = std::clamp(target - neck, -maxEye, maxEye);
				}
			};

		float neckYaw, eyeYaw;
		ComputeBoneAngles(m_lookAtYaw, baseDeadZone, maxNeckYaw, neckYaw, eyeYaw);

		// 上向き判定 (App.cppの実装依存：pitch正が上向きと仮定)
		bool isUpward = (m_lookAtPitch > 0.0f);

		float currentDeadZone = isUpward ? deadZoneUp : baseDeadZone;
		float currentMaxNeckPitch = isUpward ? maxNeckPitchUp : maxNeckPitchDown;

		float neckPitch, eyePitch;
		ComputeBoneAngles(m_lookAtPitch, currentDeadZone, currentMaxNeckPitch, neckPitch, eyePitch);

		// 首と頭で半分ずつ負担
		XMVECTOR qHeadNeck = XMQuaternionRotationRollPitchYaw(neckPitch * 0.5f, neckYaw * 0.5f, 0.0f);
		XMVECTOR qEyes = XMQuaternionRotationRollPitchYaw(eyePitch, eyeYaw, 0.0f);

		auto ApplyRot = [&](int32_t idx, XMVECTOR qOffset) {
			if (idx < 0) return;
			const auto& name = m_model->Bones()[idx].name;

			XMVECTOR current = XMQuaternionIdentity();
			if (m_pose.boneRotations.count(name))
			{
				current = XMLoadFloat4(&m_pose.boneRotations[name]);
			}
			XMVECTOR next = XMQuaternionMultiply(current, qOffset);
			XMStoreFloat4(&m_pose.boneRotations[name], next);
			};

		ApplyRot(m_boneIdxNeck, qHeadNeck);
		ApplyRot(m_boneIdxHead, qHeadNeck);
		ApplyRot(m_boneIdxEyeL, qEyes);
		ApplyRot(m_boneIdxEyeR, qEyes);
	}

	// 行列更新 (FK)
	m_boneSolver->ApplyPose(m_pose);
	m_boneSolver->UpdateMatrices();

	// 物理演算
	if (m_physicsEnabled && m_physicsWorld && m_model && !m_model->RigidBodies().empty())
	{
		if (!m_physicsWorld->IsBuilt() || m_physicsWorld->BuiltRevision() != m_model->Revision())
		{
			m_physicsWorld->BuildFromModel(*m_model, *m_boneSolver);
		}

		if (m_physicsWorld->IsBuilt())
		{
			m_physicsWorld->Step(dtSeconds, *m_model, *m_boneSolver);
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
	CacheLookAtBones();
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
		m_model->GetBounds(minx, miny, minz, maxx, maxy, maxz);
	}
	else
	{
		minx = miny = minz = -1.0f;
		maxx = maxy = maxz = 1.0f;
	}
}

void MmdAnimator::SetLookAtState(bool enabled, float yaw, float pitch)
{
	m_lookAtEnabled = enabled;
	m_lookAtYaw = yaw;
	m_lookAtPitch = pitch;

	const float limit = DirectX::XMConvertToRadians(90.0f);
	m_lookAtYaw = std::clamp(m_lookAtYaw, -limit, limit);
	m_lookAtPitch = std::clamp(m_lookAtPitch, -limit, limit);
}

DirectX::XMFLOAT3 MmdAnimator::GetBoneGlobalPosition(const std::wstring& boneName) const
{
	if (!m_boneSolver || !m_model) return { 0,0,0 };

	int idx = -1;
	if (boneName == L"頭") idx = m_boneIdxHead;
	else
	{
		const auto& bones = m_model->Bones();
		for (size_t i = 0; i < bones.size(); ++i)
		{
			if (bones[i].name == boneName)
			{
				idx = (int)i; break;
			}
		}
	}

	if (idx >= 0 && idx < (int)m_boneSolver->BoneCount())
	{
		const auto& mat = m_boneSolver->GetBoneGlobalMatrix(idx);
		return { mat._41, mat._42, mat._43 };
	}
	return { 0,0,0 };
}