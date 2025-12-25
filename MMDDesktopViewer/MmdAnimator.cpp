#include "MmdAnimator.hpp"
#include "BoneSolver.hpp"
#include "MmdPhysicsWorld.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <DirectXMath.h>

namespace
{
	float NormalizeFrame(float frame, float maxFrame)
	{
		if (maxFrame <= 0.0f) return frame;
		const float cycle = maxFrame;
		frame = std::fmod(frame, cycle);
		if (frame < 0.0f) frame += cycle;
		return frame;
	}

	float EvaluateBezier(float t, float x1, float y1, float x2, float y2)
	{
		if (t <= 0.0f) return 0.0f;
		if (t >= 1.0f) return 1.0f;

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

	float ComputeBreathFactor(double t, double period)
	{
		const double PI = 3.141592653589793;
		// 基本周期 (0.0 ~ 1.0)
		double phase = std::fmod(t, period) / period;

		// sin波を少し歪ませて、吸う(早め)・吐く(ゆっくり)のリズムを作る
		// sin(x + sin(x)) のような合成で非線形にする
		double val = std::sin(phase * 2.0 * PI);

		// 0.0 ~ 1.0 に正規化せず、-1.0 ~ 1.0 の変動として返す
		return static_cast<float>(val);
	}
}

MmdAnimator::MmdAnimator()
{
	m_lastUpdate = std::chrono::steady_clock::now();
	DirectX::XMStoreFloat4x4(&m_motionTransform, DirectX::XMMatrixIdentity());
	m_boneSolver = std::make_unique<BoneSolver>();
	m_physicsWorld = std::make_unique<MmdPhysicsWorld>();

	// まばたき間隔の初期化 (2.0 ~ 6.0秒)
	m_nextBlinkInterval = 2.0f + static_cast<float>(rand()) / RAND_MAX * 4.0f;
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

void MmdAnimator::SetPhysicsSettings(const PhysicsSettings& settings)
{
	if (!m_physicsWorld) return;

	m_physicsWorld->GetSettings() = settings;
	m_physicsWorld->Reset();
}

const PhysicsSettings& MmdAnimator::GetPhysicsSettings() const
{
	static PhysicsSettings fallback{};
	return m_physicsWorld ? m_physicsWorld->GetSettings() : fallback;
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

	bool isMotionActive = (motion != nullptr && !m_paused);

	if (motion)
	{
		// --- ボーンアニメーション適用 ---
		const auto& boneTracks = motion->BoneTracks();
		const size_t numBoneTracks = boneTracks.size();
		m_pose.boneTranslations.reserve(numBoneTracks);
		m_pose.boneRotations.reserve(numBoneTracks);
		auto& poseTranslations = m_pose.boneTranslations;
		auto& poseRotations = m_pose.boneRotations;

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

			poseTranslations.insert_or_assign(track.name, trans);
			poseRotations.insert_or_assign(track.name, rot);
		}

		// --- モーフアニメーション適用 ---
		const auto& morphTracks = motion->MorphTracks();
		const size_t numMorphTracks = morphTracks.size();
		m_pose.morphWeights.reserve(numMorphTracks);
		auto& poseMorphs = m_pose.morphWeights;

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
			poseMorphs.insert_or_assign(track.name, w);
		}
	}

	// --- 自動まばたき処理 ---
	if (m_autoBlinkEnabled)
	{
		// モーション再生中（モーションがあり、かつ一時停止していない）かどうか
		bool isPlaying = (motion != nullptr && !m_paused);

		if (!isPlaying)
		{
			// 一時停止中 または モーション無し の場合は自動まばたきを適用
			UpdateAutoBlink(dtSeconds);

			// 既存のモーフ値(一時停止中のポーズなど)と比較し、目が閉じている度合いが大きい方を採用する
			float currentW = m_pose.morphWeights[L"まばたき"];
			m_pose.morphWeights[L"まばたき"] = std::max(currentW, m_blinkWeight);
		}
		else
		{
			// 再生中は干渉しないように状態をリセット（目は開けておく）
			m_blinkState = 0; // Open
			m_blinkTimer = 0.0f;
			m_blinkWeight = 0.0f;
		}
	}
	// ----------------------

	if (!isMotionActive && m_breathingEnabled)
	{
		UpdateBreath(dtSeconds);
	}
	else if (isMotionActive)
	{
		//何かしてもいいけど
	}

	ApplyAudioReactive(dtSeconds, isMotionActive);

	if (m_lookAtEnabled && m_model)
	{
		using namespace DirectX;

		const float maxNeckYaw = XMConvertToRadians(50.0f);
		const float maxNeckPitchUp = XMConvertToRadians(25.0f);
		const float maxNeckPitchDown = XMConvertToRadians(35.0f);

		const float maxEyeYaw = XMConvertToRadians(20.0f);
		const float maxEyePitch = XMConvertToRadians(5.0f);

		const float deadZoneYaw = maxEyeYaw;
		const float deadZonePitchUp = maxEyePitch;
		const float deadZonePitchDown = maxEyePitch;

		const float pitchNeckGain = 1.25f;

		// ヘルパー: 目と首の配分計算
		auto ComputeBoneAngles = [&](float target, float deadZone, float maxNeck, float maxEyeLocal, float neckGain, float& outNeck, float& outEye)
			{
				if (std::abs(target) <= deadZone)
				{
					outNeck = 0.0f;
					outEye = std::clamp(target, -maxEyeLocal, maxEyeLocal);
				}
				else
				{
					float sign = (target >= 0.0f) ? 1.0f : -1.0f;
					float excess = target - (sign * deadZone);

					// neckGain を上げるほど、早めに首/頭が動く
					float neck = std::clamp(excess * neckGain, -maxNeck, maxNeck);
					outNeck = neck;

					// 目は残差だけ担当（ただし可動範囲でクランプ）
					outEye = std::clamp(target - neck, -maxEyeLocal, maxEyeLocal);
				}
			};

		float neckYaw, eyeYaw;
		ComputeBoneAngles(m_lookAtYaw, deadZoneYaw, maxNeckYaw, maxEyeYaw, 1.0f, neckYaw, eyeYaw);

		bool isUpward = (m_lookAtPitch > 0.0f);
		float currentDeadZonePitch = isUpward ? deadZonePitchUp : deadZonePitchDown;
		float currentMaxNeckPitch = isUpward ? maxNeckPitchUp : maxNeckPitchDown;

		float neckPitch, eyePitch;
		ComputeBoneAngles(m_lookAtPitch, currentDeadZonePitch, currentMaxNeckPitch, maxEyePitch, pitchNeckGain, neckPitch, eyePitch);

		const float neckYawW = 0.45f;
		const float headYawW = 0.55f;
		const float neckPitchW = 0.30f;
		const float headPitchW = 0.70f;

		XMVECTOR qNeck = XMQuaternionRotationRollPitchYaw(neckPitch * neckPitchW, neckYaw * neckYawW, 0.0f);
		XMVECTOR qHead = XMQuaternionRotationRollPitchYaw(neckPitch * headPitchW, neckYaw * headYawW, 0.0f);
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

		ApplyRot(m_boneIdxNeck, qNeck);
		ApplyRot(m_boneIdxHead, qHead);
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

void MmdAnimator::SetLookAtTarget(bool enabled, const DirectX::XMFLOAT3& targetPos)
{
	using namespace DirectX;

	m_lookAtEnabled = enabled;
	if (!enabled)
	{
		m_lookAtYaw = 0.0f;
		m_lookAtPitch = 0.0f;
		return;
	}

	if (!m_boneSolver || !m_model) return;

	// 参照ボーン（首があれば首、無ければ頭）
	int32_t refIdx = (m_boneIdxNeck >= 0) ? m_boneIdxNeck : m_boneIdxHead;
	if (refIdx < 0) return;

	const auto& refM = m_boneSolver->GetBoneGlobalMatrix(refIdx);
	XMVECTOR refPos = XMVectorSet(refM._41, refM._42, refM._43, 1.0f);

	XMVECTOR target = XMLoadFloat3(&targetPos);
	XMVECTOR dir = XMVectorSubtract(target, refPos);
	float dirLenSq = XMVectorGetX(XMVector3LengthSq(dir));
	if (dirLenSq < 1e-8f)
	{
		m_lookAtYaw = 0.0f; m_lookAtPitch = 0.0f; return;
	}
	dir = XMVector3Normalize(dir);

	// ref のローカル基底（行ベクトル想定）
	XMVECTOR right = XMVector3Normalize(XMVectorSet(refM._11, refM._12, refM._13, 0.0f));
	XMVECTOR up = XMVector3Normalize(XMVectorSet(refM._21, refM._22, refM._23, 0.0f));
	XMVECTOR fwd = XMVector3Normalize(XMVectorSet(refM._31, refM._32, refM._33, 0.0f));

	// 「顔の正面」が -Z のモデル対策（目ボーンの位置で符号判定）
	if (m_boneIdxEyeL >= 0 && m_boneIdxEyeR >= 0)
	{
		const auto& mL = m_boneSolver->GetBoneGlobalMatrix(m_boneIdxEyeL);
		const auto& mR = m_boneSolver->GetBoneGlobalMatrix(m_boneIdxEyeR);

		XMVECTOR eyeMid = XMVectorScale(
			XMVectorAdd(
				XMVectorSet(mL._41, mL._42, mL._43, 1.0f),
				XMVectorSet(mR._41, mR._42, mR._43, 1.0f)
			),
			0.5f
		);

		XMVECTOR faceDir = XMVectorSubtract(eyeMid, refPos);
		float faceLenSq = XMVectorGetX(XMVector3LengthSq(faceDir));
		if (faceLenSq > 1e-8f)
		{
			faceDir = XMVector3Normalize(faceDir);
			float sign = XMVectorGetX(XMVector3Dot(faceDir, fwd));
			if (sign < 0.0f)
			{
				// face が -fwd 側 → bone(+Z) を -dir に向けると face(-Z) が target に向く
				dir = XMVectorNegate(dir);
			}
		}
	}

	float x = XMVectorGetX(XMVector3Dot(dir, right));
	float y = XMVectorGetX(XMVector3Dot(dir, up));
	float z = XMVectorGetX(XMVector3Dot(dir, fwd));

	float yaw = std::atan2(x, z);
	float pitch = std::atan2(y, z);

	const float limit = XMConvertToRadians(90.0f);
	m_lookAtYaw = std::clamp(yaw, -limit, limit);
	m_lookAtPitch = std::clamp(pitch, -limit, limit);
}

void MmdAnimator::UpdateAutoBlink(double dt)
{
	const float closeSpeed = 0.1f;  // 閉じるのにかかる時間
	const float keepClosed = 0.05f; // 閉じている時間
	const float openSpeed = 0.15f;  // 開くのにかかる時間

	m_blinkTimer += static_cast<float>(dt);

	switch (m_blinkState)
	{
		case 0: // Open (待機中)
			if (m_blinkTimer >= m_nextBlinkInterval)
			{
				m_blinkState = 1;
				m_blinkTimer = 0.0f;
			}
			m_blinkWeight = 0.0f;
			break;

		case 1: // Closing
		{
			float t = m_blinkTimer / closeSpeed;
			if (t >= 1.0f)
			{
				t = 1.0f;
				m_blinkState = 2;
				m_blinkTimer = 0.0f;
			}
			m_blinkWeight = t;
			break;
		}

		case 2: // Closed (維持)
			if (m_blinkTimer >= keepClosed)
			{
				m_blinkState = 3;
				m_blinkTimer = 0.0f;
			}
			m_blinkWeight = 1.0f;
			break;

		case 3: // Opening
		{
			float t = m_blinkTimer / openSpeed;
			if (t >= 1.0f)
			{
				t = 1.0f;
				m_blinkState = 0;
				m_blinkTimer = 0.0f;
				// 次のまばたき時間をランダムに決定 (2.0秒 ～ 6.0秒)
				m_nextBlinkInterval = 2.0f + static_cast<float>(rand()) / RAND_MAX * 4.0f;
			}
			m_blinkWeight = 1.0f - t;
			break;
		}
	}
}

DirectX::XMFLOAT4X4 MmdAnimator::GetBoneGlobalMatrix(const std::wstring& boneName) const
{
	if (!m_boneSolver || !m_model)
	{
		DirectX::XMFLOAT4X4 identity;
		DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
		return identity;
	}

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
		return m_boneSolver->GetBoneGlobalMatrix(idx);
	}

	DirectX::XMFLOAT4X4 identity;
	DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
	return identity;
}

void MmdAnimator::UpdateBreath(double dt)
{
	m_breathTime += dt;

	// パラメータ設定 (高品質な挙動のための定数)
	// 呼吸の基本周期: 約3.5秒
	const double mainPeriod = 3.5;
	// ゆらぎ周期: 約13秒 (大きくゆっくりした変化)
	const double slowPeriod = 13.0;

	using namespace DirectX;

	// 1. 基本の呼吸波形 (胸の上下)
	// sin^3 を使うことで、「吸って、止めて、吐いて、止めて」の緩急をつける
	double phase = m_breathTime * (2.0 * XM_PI / mainPeriod);
	float baseWave = std::pow(std::sin(phase), 3.0f); // -1.0 ~ 1.0

	// 2. ゆらぎ成分 (1/fゆらぎ的なゆっくりした変化)
	float slowWave = std::sin(m_breathTime * (2.0 * XM_PI / slowPeriod));

	// 3. 最終的な強度係数
	// ゆらぎを少し混ぜて、機械的な繰り返し感を消す
	float intensity = (baseWave + slowWave * 0.2f) * 0.5f;

	// 各ボーンへの適用
	auto ApplyBoneRot = [&](const std::wstring& name, float pitch, float yaw, float roll)
		{
			// 既存の回転を取得 (LookAtなどで既に設定されている場合があるため合成する)
			XMVECTOR currentQ = XMQuaternionIdentity();
			if (m_pose.boneRotations.count(name))
			{
				currentQ = XMLoadFloat4(&m_pose.boneRotations[name]);
			}

			// オイラー角から追加回転を作成 (ラジアン)
			XMVECTOR addQ = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);

			// 現在の回転に適用
			XMVECTOR nextQ = XMQuaternionMultiply(currentQ, addQ);
			XMStoreFloat4(&m_pose.boneRotations[name], nextQ);
		};

	// ボーンごとの微調整 (モデルに合わせて微調整してください)
	// 上半身: 呼吸のメイン。前後にわずかに揺れる (Pitch)
	// 吸うとき(intensity > 0)に少し反り、吐くときに戻る
	ApplyBoneRot(L"上半身", intensity * XMConvertToRadians(1.5f), 0.0f, 0.0f);

	// 上半身2: 上半身の動きを増幅または遅延させる
	// 少し位相をずらすとより有機的になりますが、ここでは単純な連動とします
	ApplyBoneRot(L"上半身2", intensity * XMConvertToRadians(1.8f), 0.0f, 0.0f);

	// 首・頭: 体の動きに対して少し遅れてバランスを取る (逆位相気味に)
	// 体が反ると顎を引くような動きを入れると視線が安定する
	ApplyBoneRot(L"首", intensity * XMConvertToRadians(-0.8f), 0.0f, 0.0f);
	ApplyBoneRot(L"頭", intensity * XMConvertToRadians(-0.5f), 0.0f, 0.0f);

	// 肩: 吸うときにわずかに上がる (Roll) - Z軸
	// 左肩(Z+) 右肩(Z-)
	ApplyBoneRot(L"左肩", 0.0f, 0.0f, intensity * XMConvertToRadians(1.0f));
	ApplyBoneRot(L"右肩", 0.0f, 0.0f, intensity * XMConvertToRadians(-1.0f));
}

void MmdAnimator::ApplyAudioReactive(double dt, bool isMotionActive)
{
	auto smoothTowards = [dt](float current, float target, float rate)
		{
			const float alpha = 1.0f - std::exp(-rate * static_cast<float>(dt));
			return current + (target - current) * alpha;
		};

	if (!m_audioReactiveEnabled || !m_audioState.active)
	{
		m_audioBeatPhase = 0.0f;
		m_audioPhaseSpeed = smoothTowards(m_audioPhaseSpeed, 0.0f, 6.0f);
		m_audioStrengthFiltered = smoothTowards(m_audioStrengthFiltered, 0.0f, 6.0f);
		m_audioMouthFiltered = smoothTowards(m_audioMouthFiltered, 0.0f, 10.0f);
		return;
	}

	float targetBpm = m_audioState.bpm;
	if (targetBpm < 1.0f)
	{
		targetBpm = 120.0f;
	}
	targetBpm = std::clamp(targetBpm, 60.0f, 180.0f);

	if (m_audioBpmFiltered <= 0.0f)
	{
		m_audioBpmFiltered = targetBpm;
	}
	m_audioBpmFiltered = smoothTowards(m_audioBpmFiltered, targetBpm, 2.5f);

	const float targetPhaseSpeed = (m_audioBpmFiltered / 60.0f) * DirectX::XM_2PI;
	m_audioPhaseSpeed = smoothTowards(m_audioPhaseSpeed, targetPhaseSpeed, 3.5f);

	m_audioBeatPhase += static_cast<float>(dt) * m_audioPhaseSpeed;
	if (m_audioBeatPhase > DirectX::XM_2PI)
	{
		m_audioBeatPhase = std::fmod(m_audioBeatPhase, DirectX::XM_2PI);
	}

	m_audioStrengthFiltered = smoothTowards(
		m_audioStrengthFiltered,
		std::clamp(m_audioState.beatStrength, 0.0f, 1.0f),
		5.0f);

	const float mouthTarget = std::clamp(m_audioState.mouthOpen, 0.0f, 1.0f);
	const float mouthRate = mouthTarget > m_audioMouthFiltered ? 14.0f : 9.0f;
	m_audioMouthFiltered = smoothTowards(m_audioMouthFiltered, mouthTarget, mouthRate);
	const float shapedMouth = std::pow(std::clamp(m_audioMouthFiltered, 0.0f, 1.0f), 0.92f);

	const float motionScale = isMotionActive ? 0.25f : 0.65f;
	ApplyLipSync(shapedMouth);

	const float expressiveStrength = std::clamp((m_audioStrengthFiltered * 0.85f) + (m_audioMouthFiltered * 0.35f), 0.0f, 1.0f);
	ApplySway(m_audioBeatPhase, expressiveStrength, motionScale);
}

void MmdAnimator::ApplyLipSync(float weight)
{
	float w = std::clamp(weight * 1.1f, 0.0f, 1.0f);
	w = std::clamp(w * (0.65f + 0.35f * w), 0.0f, 1.0f);
	auto applyMorph = [&](const std::wstring& name, float value)
		{
			float current = m_pose.morphWeights[name];
			m_pose.morphWeights[name] = std::max(current, value);
		};

	applyMorph(L"あ", w);
	applyMorph(L"い", w * 0.35f);
	applyMorph(L"う", w * 0.55f);
	applyMorph(L"え", w * 0.2f);
	applyMorph(L"お", w * 0.6f);
	applyMorph(L"口開け", w);
	applyMorph(L"口開き", w);
}

void MmdAnimator::ApplySway(float phase, float strength, float motionScale)
{
	const float easedStrength = std::clamp(strength, 0.0f, 1.0f) * (0.6f + 0.4f * std::clamp(strength, 0.0f, 1.0f));
	const float amplitude = easedStrength * motionScale;
	if (amplitude <= 0.001f) return;

	const float pitch = DirectX::XMConvertToRadians(10.0f) * std::sin(phase) * amplitude;

	const float yaw = DirectX::XMConvertToRadians(1.5f) * std::sin(phase * 0.5f) * amplitude;

	const float roll = DirectX::XMConvertToRadians(1.5f) * std::cos(phase * 0.5f) * amplitude;

	auto applyRotation = [&](const std::wstring& boneName, float pitchRad, float yawRad, float rollRad, float weight)
		{
			using namespace DirectX;
			XMVECTOR delta = XMQuaternionRotationRollPitchYaw(pitchRad * weight, yawRad * weight, rollRad * weight);

			XMVECTOR base = XMQuaternionIdentity();
			auto it = m_pose.boneRotations.find(boneName);
			if (it != m_pose.boneRotations.end())
			{
				base = XMLoadFloat4(&it->second);
			}
			XMVECTOR combined = XMQuaternionNormalize(XMQuaternionMultiply(base, delta));
			XMFLOAT4 out{};
			XMStoreFloat4(&out, combined);
			m_pose.boneRotations.insert_or_assign(boneName, out);
		};

	applyRotation(L"頭", pitch * 1.2f, yaw * 0.6f, roll * 0.4f, 1.0f);
	applyRotation(L"首", pitch * 0.8f, yaw * 0.5f, roll * 0.5f, 1.0f);

	// 上半身は逆位相にしたり、遅らせたりすると自然
	applyRotation(L"上半身", pitch * 0.25f, yaw * 0.2f, roll * 0.25f, 1.0f);
	applyRotation(L"上半身2", pitch * 0.18f, yaw * 0.16f, roll * 0.2f, 1.0f);

	const float shoulderRoll = roll * 0.35f + pitch * 0.12f;
	applyRotation(L"左肩", 0.0f, 0.0f, shoulderRoll, 1.0f);
	applyRotation(L"右肩", 0.0f, 0.0f, -shoulderRoll, 1.0f);
}