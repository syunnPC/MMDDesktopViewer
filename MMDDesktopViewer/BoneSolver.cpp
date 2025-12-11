#include "BoneSolver.hpp"
#include <algorithm>
#include <functional>
#include <cmath>

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
				// 強制的に 0 にする（軸ブレ防止の核心）
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

		XMVECTOR grantRotScaled = XMQuaternionSlerp(XMQuaternionIdentity(), grantRot, bone.grantWeight);

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
	for (size_t idx : m_sortedBoneOrder)
	{
		if (m_bones[idx].IsIK())
		{
			SolveIKBone(idx);
		}
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
			XMVECTOR grantRotScaled = XMQuaternionSlerp(XMQuaternionIdentity(), grantRot, bone.grantWeight);
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

	XMMATRIX ikGlobal = XMLoadFloat4x4(&m_boneStates[boneIndex].globalMatrix);
	XMVECTOR destPos = ikGlobal.r[3];

	float limitAngle = MaybeDegreesToRadians(ikBone.ikLimitAngle);
	if (limitAngle <= 0.0f) limitAngle = DirectX::XM_PI;
	const int loopCount = ikBone.ikLoopCount;

	for (int loop = 0; loop < loopCount; ++loop)
	{
		for (size_t linkIdx = 0; linkIdx < ikBone.ikLinks.size(); ++linkIdx)
		{
			const auto& link = ikBone.ikLinks[linkIdx];
			if (link.boneIndex < 0) continue;

			const size_t currIdx = static_cast<size_t>(link.boneIndex);
			if (currIdx >= m_bones.size()) continue;

			XMMATRIX targetGlobal = XMLoadFloat4x4(&m_boneStates[targetIdx].globalMatrix);
			XMVECTOR currPos = targetGlobal.r[3];

			XMMATRIX linkGlobal = XMLoadFloat4x4(&m_boneStates[currIdx].globalMatrix);
			XMVECTOR linkPos = linkGlobal.r[3];

			XMVECTOR toDest = XMVectorSubtract(destPos, linkPos);
			XMVECTOR toCurr = XMVectorSubtract(currPos, linkPos);

			float distDest = XMVectorGetX(XMVector3Length(toDest));
			float distCurr = XMVectorGetX(XMVector3Length(toCurr));

			if (distDest < 1e-4f || distCurr < 1e-4f) continue;

			toDest = XMVectorScale(toDest, 1.0f / distDest);
			toCurr = XMVectorScale(toCurr, 1.0f / distCurr);

			float dot = XMVectorGetX(XMVector3Dot(toDest, toCurr));
			dot = std::clamp(dot, -1.0f, 1.0f);

			float angle = std::acos(dot);
			if (angle < 1e-4f) continue;
			if (angle > limitAngle) angle = limitAngle;

			XMVECTOR axis = XMVector3Cross(toCurr, toDest);
			if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-6f) continue;
			axis = XMVector3Normalize(axis);

			XMMATRIX parentGlobal = XMMatrixIdentity();
			int parentIndex = m_bones[currIdx].parentIndex;
			if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < m_bones.size())
			{
				parentGlobal = XMLoadFloat4x4(&m_boneStates[parentIndex].globalMatrix);
			}

			XMMATRIX parentInv = XMMatrixInverse(nullptr, parentGlobal);
			XMVECTOR localAxis = XMVector3TransformNormal(axis, parentInv);

			if (XMVectorGetX(XMVector3LengthSq(localAxis)) < 1e-8f) continue;
			localAxis = XMVector3Normalize(localAxis);

			XMVECTOR deltaRot = XMQuaternionRotationAxis(localAxis, angle);

			auto& linkState = m_boneStates[currIdx];
			XMVECTOR currentRot = XMLoadFloat4(&linkState.localRotation);

			currentRot = XMQuaternionMultiply(currentRot, deltaRot);
			currentRot = XMQuaternionNormalize(currentRot);

			if (link.hasLimit)
			{
				currentRot = ClampIKRotationRobust(currentRot, link.limitMin, link.limitMax);
			}

			XMStoreFloat4(&linkState.localRotation, currentRot);

			UpdateGlobalMatrixRecursive(currIdx);

			targetGlobal = XMLoadFloat4x4(&m_boneStates[targetIdx].globalMatrix);
			currPos = targetGlobal.r[3];

			float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(destPos, currPos)));
			if (dist < 1e-3f)
			{
				loop = loopCount;
				break;
			}
		}
	}
}

void BoneSolver::UpdateMatrices()
{
	for (size_t idx : m_sortedBoneOrder)
		UpdateBoneTransform(idx);

	SolveIK();

	for (size_t idx : m_sortedBoneOrder)
		UpdateBoneTransform(idx);

	for (size_t i = 0; i < m_bones.size(); ++i)
		CalculateSkinningMatrix(i);
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