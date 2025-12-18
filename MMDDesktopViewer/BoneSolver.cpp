#include "BoneSolver.hpp"
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <cmath>
#include <string>

#ifndef BONESOLVER_DISABLE_FOOT_IK
#define BONESOLVER_DISABLE_FOOT_IK 0
#endif

#ifndef BONESOLVER_DISABLE_TOE_IK
#define BONESOLVER_DISABLE_TOE_IK 0
#endif


#ifndef BONESOLVER_MAX_IK_STEP_RAD
// 1ステップの回転上限（rad）。小さすぎると膝が浅くなりやすい。
#define BONESOLVER_MAX_IK_STEP_RAD 0.35f
#endif

#ifndef BONESOLVER_MAX_KNEE_DELTA_PER_FRAME_RAD
// 膝(1軸制限リンク)のフレーム間の角度変化上限（rad）。
#define BONESOLVER_MAX_KNEE_DELTA_PER_FRAME_RAD 0.65f
#endif

using namespace DirectX;

namespace
{
	// クォータニオン -> オイラー角 (XYZ順)
	DirectX::XMFLOAT3 QuaternionToEulerXYZ(DirectX::FXMVECTOR q)
	{
		DirectX::XMFLOAT4 f{};
		DirectX::XMStoreFloat4(&f, q);

		// X-axis rotation
		float sinr_cosp = 2.0f * (f.w * f.x + f.y * f.z);
		float cosr_cosp = 1.0f - 2.0f * (f.x * f.x + f.y * f.y);
		float rx = std::atan2(sinr_cosp, cosr_cosp);

		// Y-axis rotation
		float sinp = 2.0f * (f.w * f.y - f.z * f.x);
		float ry = 0.0f;
		if (std::abs(sinp) >= 1.0f)
			ry = std::copysign(DirectX::XM_PIDIV2, sinp);
		else
			ry = std::asin(sinp);

		// Z-axis rotation
		float siny_cosp = 2.0f * (f.w * f.z + f.x * f.y);
		float cosy_cosp = 1.0f - 2.0f * (f.y * f.y + f.z * f.z);
		float rz = std::atan2(siny_cosp, cosy_cosp);

		return { rx, ry, rz };
	}

	// オイラー角 (XYZ順) -> クォータニオン
	DirectX::XMVECTOR EulerXYZToQuaternion(DirectX::FXMVECTOR euler)
	{
		DirectX::XMFLOAT3 e;
		DirectX::XMStoreFloat3(&e, euler);

		const float hx = e.x * 0.5f;
		const float hy = e.y * 0.5f;
		const float hz = e.z * 0.5f;

		const float cx = std::cos(hx);
		const float sx = std::sin(hx);
		const float cy = std::cos(hy);
		const float sy = std::sin(hy);
		const float cz = std::cos(hz);
		const float sz = std::sin(hz);

		DirectX::XMFLOAT4 q{};
		q.w = cx * cy * cz + sx * sy * sz;
		q.x = sx * cy * cz - cx * sy * sz;
		q.y = cx * sy * cz + sx * cy * sz;
		q.z = cx * cy * sz - sx * sy * cz;

		return DirectX::XMLoadFloat4(&q);
	}

	// 角度 angle を、reference から ±π 以内の表現に巻き戻す
	float WrapAngleNear(float angle, float reference)
	{
		const float twoPi = DirectX::XM_2PI;
		float diff = angle - reference;

		diff = std::fmod(diff + DirectX::XM_PI, twoPi);
		if (diff < 0.0f) diff += twoPi;
		diff -= DirectX::XM_PI;

		return reference + diff;
	}

	// PMXの角度値（度数法かもしれない）をラジアンに正規化
	DirectX::XMFLOAT3 MaybeDegreesToRadians(const DirectX::XMFLOAT3& v)
	{
		float ax = std::fabs(v.x);
		float ay = std::fabs(v.y);
		float az = std::fabs(v.z);
		float maxAbs = std::max({ ax, ay, az });

		// 値が大きすぎる場合は度数法とみなして変換
		if (maxAbs > DirectX::XM_PI * 2.2f && maxAbs < DirectX::XM_PI * 360.0f)
		{
			const float s = DirectX::XM_PI / 180.0f;
			return { v.x * s, v.y * s, v.z * s };
		}
		return v;
	}

	float MaybeDegreesToRadians(float v)
	{
		float av = std::fabs(v);
		if (av == 0.0f) return v;
		if (av > DirectX::XM_PI * 2.2f && av < DirectX::XM_PI * 360.0f)
		{
			return v * (DirectX::XM_PI / 180.0f);
		}
		return v;
	}

	// 0.5rad(28.6度) を超える値は「度」とみなしてラジアンに変換する（既にラジアンの小値はそのまま）。
	float NormalizeIkUnitAngle(float v)
	{
		float av = std::fabs(v);
		if (!(av > 0.0f) || !std::isfinite(av)) return 0.0f;

		// PMX内部はラジアンが基本（表示は度のことが多い）。
		// ただし一部ツールが「度の数値」をそのまま格納するケースがあるため、
		// 2π を大きく超える値だけ度→ラジアンへ変換する（2.0 や 4.0 はラジアンとして扱う）。
		if (av > DirectX::XM_2PI && av <= 360.0f)
		{
			av = av * (DirectX::XM_PI / 180.0f);
		}



		// 1ステップ角を上限で抑える（過大だとスナップしやすい）
		av = std::min(av, (float)BONESOLVER_MAX_IK_STEP_RAD);
		return av;
	}

	inline bool IsToeIKName(const std::wstring& name)
	{
		return (name.find(L"つま先ＩＫ") != std::wstring::npos) || (name.find(L"つま先IK") != std::wstring::npos);
	}
	inline bool IsFootIKName(const std::wstring& name)
	{
		if (IsToeIKName(name)) return false;
		return (name.find(L"足ＩＫ") != std::wstring::npos) || (name.find(L"足IK") != std::wstring::npos);
	}

	// クォータニオンの指数（回転を weight 倍する）。Slerp(identity, q, t) は t<0 で崩れるためこちらを使う。
	DirectX::XMVECTOR QuaternionPow(DirectX::FXMVECTOR qIn, float t)
	{
		using namespace DirectX;

		XMVECTOR q = XMQuaternionNormalize(qIn);

		// 同一回転の符号反転を正規化（w>=0）して連続性を上げる
		if (XMVectorGetW(q) < 0.0f)
		{
			q = XMVectorNegate(q);
		}

		// q = [v*sin(a), cos(a)]  (a = θ/2)
		float w = XMVectorGetW(q);
		w = std::clamp(w, -1.0f, 1.0f);

		const float a = std::acos(w);
		const float sinA = std::sin(a);

		// ほぼ単位（角度0）
		if (std::fabs(sinA) < 1.0e-8f)
		{
			return XMQuaternionIdentity();
		}

		XMVECTOR v = XMVectorSet(XMVectorGetX(q), XMVectorGetY(q), XMVectorGetZ(q), 0.0f);
		XMVECTOR axis = XMVectorScale(v, 1.0f / sinA); // 正規化軸

		const float a2 = a * t;
		const float sinA2 = std::sin(a2);
		const float cosA2 = std::cos(a2);

		XMVECTOR out = XMVectorSet(
			XMVectorGetX(axis) * sinA2,
			XMVectorGetY(axis) * sinA2,
			XMVectorGetZ(axis) * sinA2,
			cosA2);

		return XMQuaternionNormalize(out);
	}


	inline DirectX::XMVECTOR ClampIKRotationRobust(
		DirectX::XMVECTOR q,
		const DirectX::XMFLOAT3& limitMinIn,
		const DirectX::XMFLOAT3& limitMaxIn)
	{
		DirectX::XMFLOAT3 limitMin = MaybeDegreesToRadians(limitMinIn);
		DirectX::XMFLOAT3 limitMax = MaybeDegreesToRadians(limitMaxIn);

		// 現在の回転を XYZオイラー角に変換
		DirectX::XMFLOAT3 e = QuaternionToEulerXYZ(q);

		// 各軸ごとにクランプ処理を行う
		auto processAxis = [&](float angle, float minLim, float maxLim) -> float {
			if (std::abs(maxLim - minLim) < 1.0e-3f && std::abs(minLim) < 1.0e-3f)
			{
				// 強制的に 0 にする（軸ブレ防止）
				return 0.0f;
			}

			// それ以外は、範囲の中心に近い位相を選んでクランプ
			float center = (minLim + maxLim) * 0.5f;
			float a = WrapAngleNear(angle, center);
			return std::clamp(a, minLim, maxLim);
			};

		e.x = processAxis(e.x, limitMin.x, limitMax.x);
		e.y = processAxis(e.y, limitMin.y, limitMax.y);
		e.z = processAxis(e.z, limitMin.z, limitMax.z);

		// 再びクォータニオンに戻す
		return DirectX::XMQuaternionNormalize(EulerXYZToQuaternion(DirectX::XMLoadFloat3(&e)));
	}

	// axisXOnly 制限向け: クォータニオンからX軸ツイスト角を抽出（[-π, π]）
	float ExtractTwistAngleX(DirectX::FXMVECTOR q)
	{
		DirectX::XMFLOAT4 f{};
		DirectX::XMStoreFloat4(&f, DirectX::XMQuaternionNormalize(q));

		// X軸まわり成分のみ（ツイスト）を取り出す
		DirectX::XMVECTOR twist = DirectX::XMVectorSet(f.x, 0.0f, 0.0f, f.w);
		twist = DirectX::XMQuaternionNormalize(twist);

		DirectX::XMFLOAT4 t{};
		DirectX::XMStoreFloat4(&t, twist);

		return 2.0f * std::atan2(t.x, t.w);
	}

}

void BoneSolver::Initialize(const PmxModel* model)
{
	m_model = model;

	m_bones.clear();
	m_boneStates.clear();
	m_skinningMatrices.clear();
	m_inverseBindMatrices.clear();
	m_boneNameToIndex.clear();
	m_sortedBoneOrder.clear();

	m_lastIkDominantEuler.clear();
	m_hasLastIkDominantEuler.clear();
	m_lastIkLimitedEuler.clear();
	m_hasLastIkLimitedEuler.clear();

	if (!model) return;

	m_bones = model->Bones();

	const size_t n = m_bones.size();

	m_boneStates.resize(n);
	m_skinningMatrices.resize(n);
	m_inverseBindMatrices.resize(n);

	m_lastIkDominantEuler.assign(n, { 0.0f, 0.0f, 0.0f });
	m_hasLastIkDominantEuler.assign(n, 0);
	m_lastIkLimitedEuler.assign(n, { 0.0f, 0.0f, 0.0f });
	m_hasLastIkLimitedEuler.assign(n, 0);

	m_boneNameToIndex.reserve(n);
	for (size_t i = 0; i < n; ++i)
	{
		m_boneNameToIndex[m_bones[i].name] = i;

		m_boneStates[i].localTranslation = { 0.0f, 0.0f, 0.0f };
		m_boneStates[i].localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };

		DirectX::XMStoreFloat4x4(&m_boneStates[i].localMatrix, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&m_boneStates[i].globalMatrix, DirectX::XMMatrixIdentity());

		DirectX::XMStoreFloat4x4(&m_skinningMatrices[i], DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&m_inverseBindMatrices[i], DirectX::XMMatrixIdentity());
	}

	BuildSortedBoneOrder();
	ComputeBindPoseMatrices();
}

void BoneSolver::BuildSortedBoneOrder()
{
	m_sortedBoneOrder.clear();
	m_sortedBoneOrder.reserve(m_bones.size());

	std::vector<size_t> indices(m_bones.size());
	for (size_t i = 0; i < m_bones.size(); ++i) indices[i] = i;

	std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
		if (m_bones[a].layer != m_bones[b].layer)
		{
			return m_bones[a].layer < m_bones[b].layer;
		}
		return a < b;
					 });

	m_sortedBoneOrder = std::move(indices);
}

void BoneSolver::ApplyPose(const BonePose& pose)
{
	for (size_t i = 0; i < m_boneStates.size(); ++i)
	{
		m_boneStates[i].localTranslation = { 0.0f, 0.0f, 0.0f };
		m_boneStates[i].localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	}

	for (const auto& [name, trans] : pose.boneTranslations)
	{
		auto it = m_boneNameToIndex.find(name);
		if (it != m_boneNameToIndex.end())
		{
			m_boneStates[it->second].localTranslation = trans;
		}
	}

	for (const auto& [name, rot] : pose.boneRotations)
	{
		auto it = m_boneNameToIndex.find(name);
		if (it != m_boneNameToIndex.end())
		{
			m_boneStates[it->second].localRotation = rot;
		}
	}
}

void BoneSolver::CalculateLocalMatrix(size_t boneIndex)
{
	auto& state = m_boneStates[boneIndex];

	XMVECTOR trans = XMLoadFloat3(&state.localTranslation);
	XMVECTOR rot = XMLoadFloat4(&state.localRotation);
	rot = XMQuaternionNormalize(rot);

	XMMATRIX localMat = XMMatrixRotationQuaternion(rot) *
		XMMatrixTranslationFromVector(trans);

	XMStoreFloat4x4(&state.localMatrix, localMat);
}

void BoneSolver::CalculateGlobalMatrix(size_t boneIndex)
{
	const auto& bone = m_bones[boneIndex];
	auto& state = m_boneStates[boneIndex];

	XMVECTOR bonePos = XMLoadFloat3(&bone.position);
	XMMATRIX localMat = XMLoadFloat4x4(&state.localMatrix);

	XMMATRIX globalMat;

	if (bone.parentIndex >= 0 &&
		bone.parentIndex < static_cast<int32_t>(m_bones.size()))
	{
		const auto& parentBone = m_bones[bone.parentIndex];
		const auto& parentState = m_boneStates[bone.parentIndex];

		XMVECTOR parentPos = XMLoadFloat3(&parentBone.position);
		XMVECTOR relativePos = XMVectorSubtract(bonePos, parentPos);
		XMMATRIX parentGlobal = XMLoadFloat4x4(&parentState.globalMatrix);

		globalMat =
			localMat *
			XMMatrixTranslationFromVector(relativePos) *
			parentGlobal;
	}
	else
	{
		globalMat =
			localMat *
			XMMatrixTranslationFromVector(bonePos);
	}

	XMStoreFloat4x4(&state.globalMatrix, globalMat);
}

void BoneSolver::UpdateGlobalMatrixRecursive(size_t boneIndex)
{
	UpdateBoneTransform(boneIndex);

	for (size_t i = 0; i < m_bones.size(); ++i)
	{
		if (m_bones[i].parentIndex == static_cast<int32_t>(boneIndex))
		{
			UpdateGlobalMatrixRecursive(i);
		}
	}
}

void BoneSolver::CalculateSkinningMatrix(size_t boneIndex)
{
	auto& state = m_boneStates[boneIndex];

	XMMATRIX globalMat = XMLoadFloat4x4(&state.globalMatrix);
	XMMATRIX bindInverse = XMLoadFloat4x4(&m_inverseBindMatrices[boneIndex]);

	XMMATRIX skinningMat = bindInverse * globalMat;
	XMStoreFloat4x4(&state.skinningMatrix, skinningMat);
	XMStoreFloat4x4(&m_skinningMatrices[boneIndex], skinningMat);
}

void BoneSolver::ComputeBindPoseMatrices()
{
	for (size_t i = 0; i < m_bones.size(); ++i)
	{
		XMStoreFloat4x4(&m_boneStates[i].localMatrix, XMMatrixIdentity());
	}

	for (size_t idx : m_sortedBoneOrder)
	{
		CalculateGlobalMatrix(idx);
	}

	for (size_t i = 0; i < m_bones.size(); ++i)
	{
		XMMATRIX bindMat = XMLoadFloat4x4(&m_boneStates[i].globalMatrix);
		XMMATRIX inv = XMMatrixInverse(nullptr, bindMat);
		XMStoreFloat4x4(&m_inverseBindMatrices[i], inv);
	}
}

void BoneSolver::ApplyGrantToBone(size_t boneIndex)
{
	const auto& bone = m_bones[boneIndex];
	if (!bone.HasRotationGrant() && !bone.HasTranslationGrant()) return;

	if (bone.grantParentIndex < 0 || bone.grantParentIndex >= static_cast<int32_t>(m_bones.size())) return;
	if (bone.grantParentIndex == static_cast<int32_t>(boneIndex)) return;

	auto& state = m_boneStates[boneIndex];
	const auto& grantState = m_boneStates[bone.grantParentIndex];

	if (bone.HasRotationGrant())
	{
		XMVECTOR myRot = XMLoadFloat4(&state.localRotation);
		XMVECTOR grantRot = XMLoadFloat4(&grantState.localRotation);

		XMVECTOR grantRotScaled = QuaternionPow(grantRot, bone.grantWeight);

		myRot = XMQuaternionMultiply(myRot, grantRotScaled);
		myRot = XMQuaternionNormalize(myRot);

		XMStoreFloat4(&state.localRotation, myRot);
	}

	if (bone.HasTranslationGrant())
	{
		XMVECTOR myTrans = XMLoadFloat3(&state.localTranslation);
		XMVECTOR grantTrans = XMLoadFloat3(&grantState.localTranslation);

		XMVECTOR grantTransScaled = XMVectorScale(grantTrans, bone.grantWeight);
		myTrans = XMVectorAdd(myTrans, grantTransScaled);

		XMStoreFloat3(&state.localTranslation, myTrans);
	}

	CalculateLocalMatrix(boneIndex);
}

void BoneSolver::SolveIK()
{
	// IKを依存関係と並列化の可否に基づいて分類
	std::vector<size_t> footIKs;
	std::vector<size_t> toeIKs;
	std::vector<size_t> otherIKs;

	footIKs.reserve(2);
	toeIKs.reserve(2);
	otherIKs.reserve(8);

	// m_sortedBoneOrder（親順）を走査して分類
	// これにより、各リスト内でも基本的な親順（ID順）が保たれる
	for (size_t idx : m_sortedBoneOrder)
	{
		const auto& bone = m_bones[idx];
		if (!bone.IsIK()) continue;

		if (IsToeIKName(bone.name))
		{
			toeIKs.push_back(idx);
		}
		else if (IsFootIKName(bone.name))
		{
			footIKs.push_back(idx);
		}
		else
		{
			otherIKs.push_back(idx);
		}
	}

#if BONESOLVER_DISABLE_FOOT_IK
	footIKs.clear();
#endif
#if BONESOLVER_DISABLE_TOE_IK
	toeIKs.clear();
#endif

	for (size_t idx : otherIKs)
	{
		SolveIKBone(idx);
	}

	int nFoot = (int)footIKs.size();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) if(nFoot > 1)
#endif
	for (int i = 0; i < nFoot; ++i)
	{
		SolveIKBone(footIKs[i]);
	}

	int nToe = (int)toeIKs.size();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) if(nToe > 1)
#endif
	for (int i = 0; i < nToe; ++i)
	{
		SolveIKBone(toeIKs[i]);
	}
}

XMVECTOR BoneSolver::ClampAngle(XMVECTOR euler,
								const XMFLOAT3& minAngle,
								const XMFLOAT3& maxAngle)
{
	DirectX::XMFLOAT3 limitMin = MaybeDegreesToRadians(minAngle);
	DirectX::XMFLOAT3 limitMax = MaybeDegreesToRadians(maxAngle);

	DirectX::XMFLOAT3 e;
	DirectX::XMStoreFloat3(&e, euler);

	auto processAxis = [&](float angle, float minLim, float maxLim) -> float {
		if (std::abs(maxLim - minLim) < 1.0e-3f && std::abs(minLim) < 1.0e-3f) return 0.0f;
		float center = (minLim + maxLim) * 0.5f;
		float a = WrapAngleNear(angle, center);
		return std::clamp(a, minLim, maxLim);
		};

	e.x = processAxis(e.x, limitMin.x, limitMax.x);
	e.y = processAxis(e.y, limitMin.y, limitMax.y);
	e.z = processAxis(e.z, limitMin.z, limitMax.z);

	return XMLoadFloat3(&e);
}

void BoneSolver::UpdateBoneTransform(size_t boneIndex)
{
	const auto& bone = m_bones[boneIndex];
	auto& state = m_boneStates[boneIndex];

	XMVECTOR translation = XMLoadFloat3(&state.localTranslation);
	XMVECTOR rotation = XMLoadFloat4(&state.localRotation);

	if ((bone.HasRotationGrant() || bone.HasTranslationGrant()) &&
		bone.grantParentIndex >= 0 &&
		bone.grantParentIndex < static_cast<int32_t>(m_bones.size()) &&
		bone.grantParentIndex != static_cast<int32_t>(boneIndex))
	{
		const auto& grantState = m_boneStates[bone.grantParentIndex];

		if (bone.HasRotationGrant())
		{
			XMVECTOR grantRot = XMLoadFloat4(&grantState.localRotation);
			XMVECTOR grantRotScaled = QuaternionPow(grantRot, bone.grantWeight);
			rotation = XMQuaternionMultiply(rotation, grantRotScaled);
			rotation = XMQuaternionNormalize(rotation);
		}

		if (bone.HasTranslationGrant())
		{
			XMVECTOR grantTrans = XMLoadFloat3(&grantState.localTranslation);
			XMVECTOR grantTransScaled = XMVectorScale(grantTrans, bone.grantWeight);
			translation = XMVectorAdd(translation, grantTransScaled);
		}
	}

	XMMATRIX localMat = XMMatrixRotationQuaternion(rotation) *
		XMMatrixTranslationFromVector(translation);
	XMStoreFloat4x4(&state.localMatrix, localMat);

	XMMATRIX globalMat;
	if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(m_bones.size()))
	{
		const auto& parentState = m_boneStates[bone.parentIndex];
		const auto& parentBone = m_bones[bone.parentIndex];

		XMVECTOR bonePos = XMLoadFloat3(&bone.position);
		XMVECTOR parentPos = XMLoadFloat3(&parentBone.position);
		XMVECTOR relativePos = XMVectorSubtract(bonePos, parentPos);

		XMMATRIX parentGlobal = XMLoadFloat4x4(&parentState.globalMatrix);

		globalMat = localMat * XMMatrixTranslationFromVector(relativePos) * parentGlobal;
	}
	else
	{
		XMVECTOR bonePos = XMLoadFloat3(&bone.position);
		globalMat = localMat * XMMatrixTranslationFromVector(bonePos);
	}

	XMStoreFloat4x4(&state.globalMatrix, globalMat);
}

void BoneSolver::SolveIKBone(size_t boneIndex)
{
	const auto& ikBone = m_bones[boneIndex];
	if (!ikBone.IsIK() || ikBone.ikTargetIndex < 0) return;

	const size_t targetIdx = static_cast<size_t>(ikBone.ikTargetIndex);
	if (targetIdx >= m_bones.size()) return;

	// IKループ設定
	float limitAngle = NormalizeIkUnitAngle(ikBone.ikLimitAngle);
	if (limitAngle <= 0.0f) limitAngle = DirectX::XM_PI;
	const int loopCount = ikBone.ikLoopCount;

	// [最適化] 更新チェーン探索用バッファ（スタック確保で高速化）
	// 通常の人型モデルのIKチェーンは短いため16階層あれば十分
	int chainBuffer[16];

	for (int loop = 0; loop < loopCount; ++loop)
	{
		for (size_t linkIdx = 0; linkIdx < ikBone.ikLinks.size(); ++linkIdx)
		{
			const auto& link = ikBone.ikLinks[linkIdx];
			if (link.boneIndex < 0) continue;

			const size_t currIdx = static_cast<size_t>(link.boneIndex);
			if (currIdx >= m_bones.size()) continue;

			// --- 現在の状態取得 ---
			XMMATRIX destGlobal = XMLoadFloat4x4(&m_boneStates[boneIndex].globalMatrix);
			XMVECTOR destPos = destGlobal.r[3];

			XMMATRIX targetGlobal = XMLoadFloat4x4(&m_boneStates[targetIdx].globalMatrix);
			XMVECTOR currPos = targetGlobal.r[3];

			XMMATRIX linkGlobal = XMLoadFloat4x4(&m_boneStates[currIdx].globalMatrix);
			XMVECTOR linkPos = linkGlobal.r[3];

			XMMATRIX parentGlobal = XMMatrixIdentity();
			int parentIndex = m_bones[currIdx].parentIndex;
			if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < m_bones.size())
			{
				parentGlobal = XMLoadFloat4x4(&m_boneStates[parentIndex].globalMatrix);
			}
			XMMATRIX parentInv = XMMatrixInverse(nullptr, parentGlobal);

			// --- 1軸制限（膝など）の判定 ---
			bool hasLimit = link.hasLimit;
			XMFLOAT3 limMin = link.limitMin;
			XMFLOAT3 limMax = link.limitMax;
			const float eps = 1.0e-3f;

			// X軸のみに制限がある関節か判定
			bool isAxisXOnly = hasLimit &&
				(std::abs(limMin.y) < eps && std::abs(limMax.y) < eps) &&
				(std::abs(limMin.z) < eps && std::abs(limMax.z) < eps);

			if (isAxisXOnly)
			{
				XMVECTOR toDest = XMVectorSubtract(destPos, linkPos);
				XMVECTOR toCurr = XMVectorSubtract(currPos, linkPos);

				// 親空間（ボーンローカル空間）へ変換
				XMVECTOR localDirDest = XMVector3TransformNormal(toDest, parentInv);
				XMVECTOR localDirCurr = XMVector3TransformNormal(toCurr, parentInv);

				// X成分を無視してYZ平面に投影し、正規化
				localDirDest = XMVectorSetX(localDirDest, 0.0f);
				localDirCurr = XMVectorSetX(localDirCurr, 0.0f);

				{
					// 伸び切り付近では投影ベクトルの長さが小さくなりやすい。
					// 正規化するとノイズが増幅して角度が跳ぶので、正規化せずに atan2 で角度を得る。
					const float lenDestSq = XMVectorGetX(XMVector3LengthSq(localDirDest));
					const float lenCurrSq = XMVectorGetX(XMVector3LengthSq(localDirCurr));
					if (lenDestSq <= 1.0e-12f || lenCurrSq <= 1.0e-12f)
					{
						// ここで全方向IKに落とすと不要な回転が混入してスパイクになりやすいので、リンク回転をスキップする。
						continue;
					}

					// Cross積(X成分) = Y*Z' - Z*Y'
					float crossX = XMVectorGetX(XMVector3Cross(localDirCurr, localDirDest));
					float dot = XMVectorGetX(XMVector3Dot(localDirCurr, localDirDest));
					float deltaAngle = std::atan2(crossX, dot);
					deltaAngle = std::clamp(deltaAngle, -limitAngle, limitAngle);

					// 2. 現在の角度(X軸)を取得 (スカラー)
					auto& linkState = m_boneStates[currIdx];
					XMVECTOR currentQ = XMLoadFloat4(&linkState.localRotation);
					float currentAngle = ExtractTwistAngleX(currentQ);

					// 3. 角度のUnwrapping 
					float minRad = MaybeDegreesToRadians(limMin.x);
					float maxRad = MaybeDegreesToRadians(limMax.x);
					if (minRad > maxRad) std::swap(minRad, maxRad);

					float center = (minRad + maxRad) * 0.5f;
					float ref = center;
					if (m_hasLastIkLimitedEuler[currIdx]) ref = m_lastIkLimitedEuler[currIdx].x;
					currentAngle = WrapAngleNear(currentAngle, ref);

					// 4. 足し算して次の角度を求める
					float targetAngle = currentAngle + deltaAngle;

					// 5. 制限範囲でクランプ
					targetAngle = std::clamp(targetAngle, minRad, maxRad);
					// フレーム間のスパイク抑制
					if (m_hasLastIkLimitedEuler[currIdx])
					{
						const float prev = m_lastIkLimitedEuler[currIdx].x;
						float ta = WrapAngleNear(targetAngle, prev);
						const float err = std::sqrt(std::max(0.0f, XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(destPos, currPos)))));
						float maxDelta = (float)BONESOLVER_MAX_KNEE_DELTA_PER_FRAME_RAD;
						if (err > 0.05f) maxDelta = DirectX::XM_PI;
						else if (err > 0.02f) maxDelta = 1.2f;
						else if (err > 0.01f) maxDelta = 0.9f;
						float d = ta - prev;
						d = std::clamp(d, -maxDelta, +maxDelta);
						targetAngle = std::clamp(prev + d, minRad, maxRad);
					}

					// 6. 新しい角度からクォータニオンを作成して適用
					XMVECTOR newRot = XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), targetAngle);
					XMStoreFloat4(&linkState.localRotation, newRot);
					m_lastIkLimitedEuler[currIdx].x = targetAngle;
					m_hasLastIkLimitedEuler[currIdx] = 1;

					// --- [最適化] 行列更新 ---
					// 全子孫を再帰更新する UpdateGlobalMatrixRecursive(currIdx) を廃止し、
					// 影響のあるチェーン（現在リンク～エフェクタ）のみを更新する。
					{
						// 1. 自分自身の行列更新
						UpdateBoneTransform(currIdx);

						// 2. エフェクタ(targetIdx)までの経路を探索
						int childCursor = m_bones[targetIdx].parentIndex;
						int chainCount = 0;
						bool connected = false;

						// targetIdx から親を辿って currIdx に到達するか確認
						while (childCursor >= 0 && chainCount < 16)
						{
							if (childCursor == (int)currIdx)
							{
								connected = true;
								break;
							}
							chainBuffer[chainCount++] = childCursor;
							childCursor = m_bones[childCursor].parentIndex;
						}

						if (connected)
						{
							// 経路上のボーンを親側(currIdx直下)から順に更新
							for (int i = chainCount - 1; i >= 0; --i)
							{
								UpdateBoneTransform(chainBuffer[i]);
							}
							// 最後にエフェクタを更新
							UpdateBoneTransform(targetIdx);
						}
						else
						{
							// 経路が繋がっていない場合(通常ありえない)はエフェクタのみ強制更新
							UpdateBoneTransform(targetIdx);
						}
					}

					// 収束判定
					targetGlobal = XMLoadFloat4x4(&m_boneStates[targetIdx].globalMatrix);
					currPos = targetGlobal.r[3];
					if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(destPos, currPos))) < 1.0e-6f)
					{
						loop = loopCount;
						break;
					}

					continue;
				}

			}
			// --- 通常の全方向IK (標準ロジック) ---
			XMVECTOR toDest = XMVectorSubtract(destPos, linkPos);
			XMVECTOR toCurr = XMVectorSubtract(currPos, linkPos);

			float distDest = XMVectorGetX(XMVector3Length(toDest));
			float distCurr = XMVectorGetX(XMVector3Length(toCurr));

			if (distDest < 1e-4f || distCurr < 1e-4f) continue;

			toDest = XMVectorScale(toDest, 1.0f / distDest);
			toCurr = XMVectorScale(toCurr, 1.0f / distCurr);

			float dot = XMVectorGetX(XMVector3Dot(toDest, toCurr));
			dot = std::clamp(dot, -1.0f, 1.0f);

			XMVECTOR axis = XMVector3Cross(toCurr, toDest);
			const float axisLenSq = XMVectorGetX(XMVector3LengthSq(axis));
			const float axisLen = std::sqrt(std::max(axisLenSq, 0.0f));
			float angle = std::atan2(axisLen, dot);

			if (angle < 1e-4f) continue;
			if (angle > limitAngle) angle = limitAngle;

			if (axisLenSq < 1.0e-10f)
			{
				if (dot > 0.0f) continue; // ほぼ同方向

				XMVECTOR base = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
				if (std::fabs(XMVectorGetX(XMVector3Dot(toCurr, base))) > 0.99f)
				{
					base = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				}

				axis = XMVector3Cross(toCurr, base);
				if (XMVectorGetX(XMVector3LengthSq(axis)) < 1.0e-12f) continue;
				axis = XMVector3Normalize(axis);
			}
			else
			{
				axis = XMVector3Normalize(axis);
			}
			XMVECTOR localAxis = XMVector3TransformNormal(axis, parentInv);
			if (XMVectorGetX(XMVector3LengthSq(localAxis)) < 1e-8f) continue;
			localAxis = XMVector3Normalize(localAxis);

			XMVECTOR deltaRot = XMQuaternionRotationAxis(localAxis, angle);

			// --- 回転の適用と制限 ---
			auto& linkState = m_boneStates[currIdx];
			XMVECTOR currentRot = XMLoadFloat4(&linkState.localRotation);

			currentRot = XMQuaternionMultiply(currentRot, deltaRot);
			currentRot = XMQuaternionNormalize(currentRot);

			if (hasLimit)
			{
				currentRot = ClampIKRotationRobust(currentRot, link.limitMin, link.limitMax);
			}

			XMStoreFloat4(&linkState.localRotation, currentRot);

			// --- [最適化] 行列更新 ---
			// 上記と同じ最適化ロジック
			{
				// 1. 自分自身の行列更新
				UpdateBoneTransform(currIdx);

				// 2. エフェクタ(targetIdx)までの経路を探索
				int childCursor = m_bones[targetIdx].parentIndex;
				int chainCount = 0;
				bool connected = false;

				while (childCursor >= 0 && chainCount < 16)
				{
					if (childCursor == (int)currIdx)
					{
						connected = true;
						break;
					}
					chainBuffer[chainCount++] = childCursor;
					childCursor = m_bones[childCursor].parentIndex;
				}

				if (connected)
				{
					// 経路上のボーンを親側(currIdx直下)から順に更新
					for (int i = chainCount - 1; i >= 0; --i)
					{
						UpdateBoneTransform(chainBuffer[i]);
					}
					// 最後にエフェクタを更新
					UpdateBoneTransform(targetIdx);
				}
				else
				{
					UpdateBoneTransform(targetIdx);
				}
			}

			// 収束判定
			targetGlobal = XMLoadFloat4x4(&m_boneStates[targetIdx].globalMatrix);
			currPos = targetGlobal.r[3];
			if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(destPos, currPos))) < 1.0e-6f)
			{
				loop = loopCount;
				break;
			}
		}
	}
}

void BoneSolver::UpdateChainGlobalMatrix(size_t boneIndex)
{
	// 再帰的に子を更新せず、このボーンのグローバル行列だけを再計算する
	// (親のグローバル行列は計算済みであるという前提で動作する)

	// 1. ローカル行列更新
	UpdateBoneTransform(boneIndex);

	// 2. グローバル行列更新 (CalculateGlobalMatrixの内容を展開して最適化)
	const auto& bone = m_bones[boneIndex];
	auto& state = m_boneStates[boneIndex];
	XMMATRIX localMat = XMLoadFloat4x4(&state.localMatrix);
	XMMATRIX globalMat;

	if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(m_bones.size()))
	{
		const auto& parentBone = m_bones[bone.parentIndex];
		const auto& parentState = m_boneStates[bone.parentIndex];

		// 親の行列は「最新である」と信頼する
		XMVECTOR parentPos = XMLoadFloat3(&parentBone.position);
		XMVECTOR bonePos = XMLoadFloat3(&bone.position);
		XMVECTOR relativePos = XMVectorSubtract(bonePos, parentPos);
		XMMATRIX parentGlobal = XMLoadFloat4x4(&parentState.globalMatrix);

		globalMat = localMat * XMMatrixTranslationFromVector(relativePos) * parentGlobal;
	}
	else
	{
		XMVECTOR bonePos = XMLoadFloat3(&bone.position);
		globalMat = localMat * XMMatrixTranslationFromVector(bonePos);
	}

	XMStoreFloat4x4(&state.globalMatrix, globalMat);
}

void BoneSolver::UpdateMatrices()
{
	UpdateMatrices(true);
}

void BoneSolver::UpdateMatrices(bool solveIK)
{
	for (size_t idx : m_sortedBoneOrder) UpdateBoneTransform(idx);

	if (solveIK)
	{
		SolveIK();

		for (size_t idx : m_sortedBoneOrder) UpdateBoneTransform(idx);
	}

	const int n = static_cast<int>(m_bones.size());
#pragma omp parallel for schedule(static) if(n >= 256)
	for (int i = 0; i < n; ++i) CalculateSkinningMatrix(i);
}

void BoneSolver::UpdateMatricesNoIK()
{
	for (size_t idx : m_sortedBoneOrder)
	{
		UpdateBoneTransform(idx);
	}

	const size_t n = m_bones.size();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 256)
#endif
	for (int i = 0; i < (int)n; ++i)
	{
		CalculateSkinningMatrix(i);
	}
}

const DirectX::XMFLOAT4X4& BoneSolver::GetBoneGlobalMatrix(size_t boneIndex) const
{
	if (boneIndex >= m_boneStates.size())
	{
		throw std::out_of_range("Bone index out of range");
	}
	return m_boneStates[boneIndex].globalMatrix;
}

const DirectX::XMFLOAT4X4& BoneSolver::GetBoneLocalMatrix(size_t boneIndex) const
{
	if (boneIndex >= m_boneStates.size())
	{
		throw std::out_of_range("BoneSolver::GetBoneLocalMatrix: boneIndex out of range");
	}
	return m_boneStates[boneIndex].localMatrix;
}

void BoneSolver::SetBoneLocalPose(size_t boneIndex,
								  const DirectX::XMFLOAT3& translation,
								  const DirectX::XMFLOAT4& rotation)
{
	if (boneIndex >= m_boneStates.size())
	{
		throw std::out_of_range("Bone index out of range");
	}

	auto& s = m_boneStates[boneIndex];
	s.localTranslation = translation;
	s.localRotation = rotation;
}

void BoneSolver::GetBoneBounds(DirectX::XMFLOAT3& outMin, DirectX::XMFLOAT3& outMax) const
{
	float minx = std::numeric_limits<float>::max();
	float miny = std::numeric_limits<float>::max();
	float minz = std::numeric_limits<float>::max();
	float maxx = std::numeric_limits<float>::lowest();
	float maxy = std::numeric_limits<float>::lowest();
	float maxz = std::numeric_limits<float>::lowest();

	bool hasBone = false;
	for (const auto& state : m_boneStates)
	{
		float x = state.globalMatrix._41;
		float y = state.globalMatrix._42;
		float z = state.globalMatrix._43;

		if (x < minx) minx = x;
		if (x > maxx) maxx = x;
		if (y < miny) miny = y;
		if (y > maxy) maxy = y;
		if (z < minz) minz = z;
		if (z > maxz) maxz = z;
		hasBone = true;
	}

	if (!hasBone)
	{
		outMin = { 0, 0, 0 };
		outMax = { 0, 0, 0 };
	}
	else
	{
		outMin = { minx, miny, minz };
		outMax = { maxx, maxy, maxz };
	}
}