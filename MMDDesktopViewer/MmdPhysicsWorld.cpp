#include "MmdPhysicsWorld.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <span>

using namespace DirectX;

namespace
{
	static constexpr float kEps = 1.0e-6f;
	static constexpr float kBigEps = 1.0e-4f;

	// Scratch types for broadphase (kept in anonymous namespace)
	struct SapNode
	{
		float minX; int index;
	};

	struct SapPair
	{
		int a; int b;
	};

	inline static float Length3(XMVECTOR v)
	{
		return XMVectorGetX(XMVector3Length(v));
	}

	inline static bool IsVectorFinite3(XMVECTOR v)
	{
		XMFLOAT3 f; XMStoreFloat3(&f, v);
		return std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z);
	}

	inline static bool IsVectorFinite4(XMVECTOR v)
	{
		XMFLOAT4 f; XMStoreFloat4(&f, v);
		return std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z) && std::isfinite(f.w);
	}

	inline static XMVECTOR SafeNormalize3(XMVECTOR v)
	{
		float len = Length3(v);
		if (len < kBigEps || !std::isfinite(len)) return XMVectorZero();
		return XMVectorScale(v, 1.0f / len);
	}

	inline static int PopCount16(uint16_t x)
	{
		return std::popcount(x);
	}

	inline static XMVECTOR QuaternionFromAngularVelocity(FXMVECTOR w, float dt)
	{
		XMVECTOR axisLen = XMVector3Length(w);
		float angle = XMVectorGetX(axisLen) * dt;
		if (angle < kEps || !std::isfinite(angle)) return XMQuaternionIdentity();

		XMVECTOR axis = XMVector3Normalize(w);
		if (!IsVectorFinite3(axis)) return XMQuaternionIdentity();

		return XMQuaternionRotationAxis(axis, angle);
	}

	inline static XMVECTOR QuaternionFromRotationVector(FXMVECTOR rv)
	{
		float angle = Length3(rv);
		if (angle < kEps || !std::isfinite(angle)) return XMQuaternionIdentity();

		XMVECTOR axis = XMVectorScale(rv, 1.0f / angle);
		if (!IsVectorFinite3(axis)) return XMQuaternionIdentity();

		return XMQuaternionRotationAxis(axis, angle);
	}

	inline static XMVECTOR SafeQuaternionRotationAxis(FXMVECTOR axis, float angle)
	{
		if (!std::isfinite(angle) || std::abs(angle) < kEps) return XMQuaternionIdentity();
		float len = Length3(axis);
		if (!std::isfinite(len) || len < kEps) return XMQuaternionIdentity();
		XMVECTOR nAxis = XMVectorScale(axis, 1.0f / len);
		return XMQuaternionRotationAxis(nAxis, angle);
	}

	inline static XMVECTOR QuaternionDeltaToAngularVelocity(FXMVECTOR dqIn, float dt)
	{
		dt = std::max(dt, kEps);
		XMVECTOR dq = dqIn;
		if (!IsVectorFinite4(dq)) return XMVectorZero();

		if (XMVectorGetW(dq) < 0.0f) dq = XMVectorNegate(dq);

		float w = std::clamp(XMVectorGetW(dq), -1.0f, 1.0f);
		float angle = 2.0f * std::acos(w);
		if (!std::isfinite(angle)) angle = 0.0f;
		if (angle > XM_PI) angle -= XM_2PI;

		float s = std::sqrt(std::max(0.0f, 1.0f - w * w));
		XMVECTOR axis;
		if (s < 1.0e-5f || std::abs(angle) < 1.0e-5f)
		{
			axis = XMVectorZero();
			angle = 0.0f;
		}
		else
		{
			axis = XMVectorScale(XMVectorSet(XMVectorGetX(dq), XMVectorGetY(dq), XMVectorGetZ(dq), 0.0f), 1.0f / s);
			axis = XMVector3Normalize(axis);
		}

		if (!IsVectorFinite3(axis)) return XMVectorZero();

		XMVECTOR omega = XMVectorScale(axis, angle / dt);
		return omega;
	}

	inline static XMVECTOR RotateVector(FXMVECTOR v, FXMVECTOR q)
	{
		return XMVector3Rotate(v, q);
	}

	inline static float Dot3(XMVECTOR a, XMVECTOR b)
	{
		return XMVectorGetX(XMVector3Dot(a, b));
	}

	inline static float Clamp01(float v)
	{
		return std::min(1.0f, std::max(0.0f, v));
	}

	inline static XMFLOAT3 QuaternionToEulerXYZ(FXMVECTOR q)
	{
		XMFLOAT4 f;
		XMStoreFloat4(&f, q);
		float sinr_cosp = 2.0f * (f.w * f.x + f.y * f.z);
		float cosr_cosp = 1.0f - 2.0f * (f.x * f.x + f.y * f.y);
		float rx = std::atan2(sinr_cosp, cosr_cosp);

		float sinp = 2.0f * (f.w * f.y - f.z * f.x);
		float ry = 0.0f;
		if (std::abs(sinp) >= 1.0f) ry = std::copysign(XM_PIDIV2, sinp);
		else ry = std::asin(sinp);

		float siny_cosp = 2.0f * (f.w * f.z + f.x * f.y);
		float cosy_cosp = 1.0f - 2.0f * (f.y * f.y + f.z * f.z);
		float rz = std::atan2(siny_cosp, cosy_cosp);

		return { rx, ry, rz };
	}

	inline static XMVECTOR EulerXYZToQuaternion(float x, float y, float z)
	{
		return XMQuaternionRotationRollPitchYaw(x, y, z);
	}

	inline static void ClosestPtSegmentSegment(
		FXMVECTOR p1, FXMVECTOR q1,
		FXMVECTOR p2, FXMVECTOR q2,
		XMVECTOR& outC1, XMVECTOR& outC2)
	{
		XMVECTOR d1 = XMVectorSubtract(q1, p1);
		XMVECTOR d2 = XMVectorSubtract(q2, p2);
		XMVECTOR r = XMVectorSubtract(p1, p2);

		float a = Dot3(d1, d1);
		float e = Dot3(d2, d2);
		float f = Dot3(d2, r);

		float s = 0.0f, t = 0.0f;

		if (a <= kEps && e <= kEps)
		{
			outC1 = p1; outC2 = p2; return;
		}

		if (a <= kEps)
		{
			s = 0.0f;
			t = Clamp01(f / std::max(e, kEps));
		}
		else
		{
			float c = Dot3(d1, r);
			if (e <= kEps)
			{
				t = 0.0f;
				s = Clamp01(-c / std::max(a, kEps));
			}
			else
			{
				float b = Dot3(d1, d2);
				float denom = a * e - b * b;
				s = (std::abs(denom) > kEps) ? Clamp01((b * f - c * e) / denom) : 0.0f;
				float tnom = b * s + f;
				if (tnom <= 0.0f)
				{
					t = 0.0f; s = Clamp01(-c / std::max(a, kEps));
				}
				else if (tnom >= e)
				{
					t = 1.0f; s = Clamp01((b - c) / std::max(a, kEps));
				}
				else
				{
					t = tnom / e;
				}
			}
		}
		outC1 = XMVectorAdd(p1, XMVectorScale(d1, s));
		outC2 = XMVectorAdd(p2, XMVectorScale(d2, t));
	}


	// --- Box/OBB collision helpers ---
	inline static XMVECTOR ClosestPointOnAABB(FXMVECTOR p, float ex, float ey, float ez)
	{
		float x = std::clamp(XMVectorGetX(p), -ex, ex);
		float y = std::clamp(XMVectorGetY(p), -ey, ey);
		float z = std::clamp(XMVectorGetZ(p), -ez, ez);
		return XMVectorSet(x, y, z, 0.0f);
	}

	inline static bool IsInsideAABB(FXMVECTOR p, float ex, float ey, float ez)
	{
		const float x = XMVectorGetX(p);
		const float y = XMVectorGetY(p);
		const float z = XMVectorGetZ(p);
		const float eps = 1.0e-6f;
		return (std::abs(x) <= ex + eps) && (std::abs(y) <= ey + eps) && (std::abs(z) <= ez + eps);
	}

	static void ClosestPointsSegmentAABB_Local(
		FXMVECTOR s0, FXMVECTOR s1,
		float ex, float ey, float ez,
		XMVECTOR& outSeg, XMVECTOR& outBox)
	{
		const int N = 17;
		float bestD2 = std::numeric_limits<float>::max();

		for (int k = 0; k < N; ++k)
		{
			const float t = (N == 1) ? 0.0f : (static_cast<float>(k) / static_cast<float>(N - 1));
			XMVECTOR p = XMVectorLerp(s0, s1, t);
			XMVECTOR q = ClosestPointOnAABB(p, ex, ey, ez);

			if (IsInsideAABB(p, ex, ey, ez))
			{
				const float px = XMVectorGetX(p);
				const float py = XMVectorGetY(p);
				const float pz = XMVectorGetZ(p);

				const float dx = ex - std::abs(px);
				const float dy = ey - std::abs(py);
				const float dz = ez - std::abs(pz);

				if (dx <= dy && dx <= dz)
				{
					const float sx = (px >= 0.0f) ? ex : -ex;
					q = XMVectorSet(sx, std::clamp(py, -ey, ey), std::clamp(pz, -ez, ez), 0.0f);
				}
				else if (dy <= dz)
				{
					const float sy = (py >= 0.0f) ? ey : -ey;
					q = XMVectorSet(std::clamp(px, -ex, ex), sy, std::clamp(pz, -ez, ez), 0.0f);
				}
				else
				{
					const float sz = (pz >= 0.0f) ? ez : -ez;
					q = XMVectorSet(std::clamp(px, -ex, ex), std::clamp(py, -ey, ey), sz, 0.0f);
				}
			}

			XMVECTOR v = XMVectorSubtract(p, q);
			const float d2 = XMVectorGetX(XMVector3LengthSq(v));
			if (d2 < bestD2)
			{
				bestD2 = d2;
				outSeg = p;
				outBox = q;
			}
		}
	}

	inline static XMVECTOR SupportPointOBB(
		FXMVECTOR c, FXMVECTOR q,
		float ex, float ey, float ez,
		FXMVECTOR dirWorld)
	{
		XMVECTOR invQ = XMQuaternionConjugate(q);
		XMVECTOR dL = XMVector3Rotate(dirWorld, invQ);

		const float sx = (XMVectorGetX(dL) >= 0.0f) ? ex : -ex;
		const float sy = (XMVectorGetY(dL) >= 0.0f) ? ey : -ey;
		const float sz = (XMVectorGetZ(dL) >= 0.0f) ? ez : -ez;

		XMVECTOR pL = XMVectorSet(sx, sy, sz, 0.0f);
		return XMVectorAdd(c, XMVector3Rotate(pL, q));
	}

	static bool ContactOBB_OBB(
		FXMVECTOR cA, FXMVECTOR qA, float exA, float eyA, float ezA,
		FXMVECTOR cB, FXMVECTOR qB, float exB, float eyB, float ezB,
		XMVECTOR& outN, float& outPen,
		XMVECTOR& outPA, XMVECTOR& outPB)
	{
		XMVECTOR Aaxis[3] = {
			XMVector3Rotate(XMVectorSet(1.0f,0.0f,0.0f,0.0f), qA),
			XMVector3Rotate(XMVectorSet(0.0f,1.0f,0.0f,0.0f), qA),
			XMVector3Rotate(XMVectorSet(0.0f,0.0f,1.0f,0.0f), qA),
		};
		XMVECTOR Baxis[3] = {
			XMVector3Rotate(XMVectorSet(1.0f,0.0f,0.0f,0.0f), qB),
			XMVector3Rotate(XMVectorSet(0.0f,1.0f,0.0f,0.0f), qB),
			XMVector3Rotate(XMVectorSet(0.0f,0.0f,1.0f,0.0f), qB),
		};

		float Rm[3][3]{};
		float ARm[3][3]{};

		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Rm[i][j] = Dot3(Aaxis[i], Baxis[j]);
				ARm[i][j] = std::abs(Rm[i][j]) + 1.0e-6f;
			}
		}

		XMVECTOR tV = XMVectorSubtract(cB, cA);
		float tA[3] = { Dot3(tV, Aaxis[0]), Dot3(tV, Aaxis[1]), Dot3(tV, Aaxis[2]) };
		float tB[3] = {
			tA[0] * Rm[0][0] + tA[1] * Rm[1][0] + tA[2] * Rm[2][0],
			tA[0] * Rm[0][1] + tA[1] * Rm[1][1] + tA[2] * Rm[2][1],
			tA[0] * Rm[0][2] + tA[1] * Rm[1][2] + tA[2] * Rm[2][2],
		};

		const float a[3] = { exA, eyA, ezA };
		const float b[3] = { exB, eyB, ezB };

		float minOverlap = std::numeric_limits<float>::max();
		XMVECTOR bestAxis = XMVectorZero();

		auto record = [&](XMVECTOR axisN, float overlap, float signVal)
			{
				if (overlap < minOverlap)
				{
					minOverlap = overlap;
					if (signVal < 0.0f) axisN = XMVectorNegate(axisN); // A->B
					bestAxis = axisN;
				}
			};

		for (int i = 0; i < 3; ++i)
		{
			float ra = a[i];
			float rb = b[0] * ARm[i][0] + b[1] * ARm[i][1] + b[2] * ARm[i][2];
			float dist = std::abs(tA[i]);
			if (dist > ra + rb) return false;
			record(Aaxis[i], (ra + rb) - dist, tA[i]);
		}

		for (int j = 0; j < 3; ++j)
		{
			float ra = a[0] * ARm[0][j] + a[1] * ARm[1][j] + a[2] * ARm[2][j];
			float rb = b[j];
			float dist = std::abs(tB[j]);
			if (dist > ra + rb) return false;
			record(Baxis[j], (ra + rb) - dist, tB[j]);
		}

		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				XMVECTOR axis = XMVector3Cross(Aaxis[i], Baxis[j]);
				float axisLen = Length3(axis);
				if (axisLen < 1.0e-5f) continue;

				const int i1 = (i + 1) % 3; const int i2 = (i + 2) % 3;
				const int j1 = (j + 1) % 3; const int j2 = (j + 2) % 3;

				float distValSigned = tA[i2] * Rm[i1][j] - tA[i1] * Rm[i2][j];
				float distVal = std::abs(distValSigned);
				float ra = a[i1] * ARm[i2][j] + a[i2] * ARm[i1][j];
				float rb = b[j1] * ARm[i][j2] + b[j2] * ARm[i][j1];

				if (distVal > ra + rb) return false;
				float overlap = (ra + rb) - distVal;
				XMVECTOR axisN = XMVectorScale(axis, 1.0f / axisLen);
				record(axisN, overlap / axisLen, distValSigned);
			}
		}

		outN = SafeNormalize3(bestAxis);
		if (XMVector3Equal(outN, XMVectorZero())) outN = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		outPen = minOverlap;
		outPA = SupportPointOBB(cA, qA, exA, eyA, ezA, outN);
		outPB = SupportPointOBB(cB, qB, exB, eyB, ezB, XMVectorNegate(outN));
		return (outPen > 0.0f);
	}

	static bool ContactCapsule_OBB(
		FXMVECTOR capS0, FXMVECTOR capS1, float capRadiusPlusMargin,
		FXMVECTOR boxC, FXMVECTOR boxQ, float ex, float ey, float ez,
		XMVECTOR& outN, float& outPen,
		XMVECTOR& outCapPoint, XMVECTOR& outBoxPoint)
	{
		XMVECTOR invQ = XMQuaternionConjugate(boxQ);
		XMVECTOR s0L = XMVector3Rotate(XMVectorSubtract(capS0, boxC), invQ);
		XMVECTOR s1L = XMVector3Rotate(XMVectorSubtract(capS1, boxC), invQ);

		XMVECTOR segL, boxL;
		ClosestPointsSegmentAABB_Local(s0L, s1L, ex, ey, ez, segL, boxL);

		XMVECTOR segW = XMVectorAdd(boxC, XMVector3Rotate(segL, boxQ));
		XMVECTOR boxW = XMVectorAdd(boxC, XMVector3Rotate(boxL, boxQ));

		XMVECTOR d = XMVectorSubtract(boxW, segW);
		float dist = Length3(d);

		if (dist > kEps)
		{
			outN = XMVectorScale(d, 1.0f / dist);
		}
		else
		{
			XMVECTOR dc = XMVectorSubtract(boxC, segW);
			outN = SafeNormalize3(dc);
			if (XMVector3Equal(outN, XMVectorZero())) outN = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			dist = 0.0f;
		}

		float pen = capRadiusPlusMargin - dist;
		if (pen <= 0.0f) return false;

		outPen = pen;
		outCapPoint = segW;
		outBoxPoint = boxW;
		return true;
	}

	inline static XMVECTOR QuaternionConjugate(FXMVECTOR q)
	{
		return XMQuaternionConjugate(q);
	}

	inline static DirectX::XMMATRIX MatrixRotationEulerXYZ(float rx, float ry, float rz)
	{
		// v * (Rx*Ry*Rz) が X→Y→Z の適用
		return DirectX::XMMatrixRotationX(rx)
			* DirectX::XMMatrixRotationY(ry)
			* DirectX::XMMatrixRotationZ(rz);
	}
}

void MmdPhysicsWorld::Reset()
{
	m_isBuilt = false;
	m_builtRevision = 0;
	m_accumulator = 0.0;
	m_bodies.clear();
	m_joints.clear();
	m_jointAdjacency.clear();
}

void MmdPhysicsWorld::BuildFromModel(const PmxModel& model, const BoneSolver& bones)
{
	Reset();

	const auto& rbDefs = model.RigidBodies();

	if (rbDefs.empty() && !m_settings.generateBodyCollidersIfMissing)
	{
		m_isBuilt = true;
		m_builtRevision = model.Revision();
		return;
	}

	const auto& bonesDef = model.Bones();

	// Collision group mask semantics
	{
		const int sem = m_settings.collisionGroupMaskSemantics;
		if (sem == 1) m_groupMaskIsCollisionMask = false;
		else if (sem == 2) m_groupMaskIsCollisionMask = true;
		else
		{
			int countZero = 0;
			int countAll = 0;
			for (const auto& d : rbDefs)
			{
				const uint16_t mask = d.ignoreCollisionGroup;
				if (mask == 0) ++countZero;
				else if (mask == 0xFFFFu) ++countAll;
			}
			m_groupMaskIsCollisionMask = (countAll > countZero);
		}
	}
	// Detect group index base
	{
		int minG = 100;
		int maxG = -100;
		bool anyZero = false;
		for (const auto& d : rbDefs)
		{
			minG = std::min(minG, static_cast<int>(d.groupIndex));
			maxG = std::max(maxG, static_cast<int>(d.groupIndex));
			if (d.groupIndex == 0) anyZero = true;
		}
		m_groupIndexIsOneBased = (!anyZero && minG >= 1 && maxG <= 16);
	}

	std::vector<DirectX::XMFLOAT4X4> bindGlobals(bonesDef.size());
	std::vector<uint8_t> bindDone(bonesDef.size(), 0);

	auto GetBindGlobal = [&](auto&& self, int idx) -> DirectX::XMMATRIX
		{
			if (idx < 0 || idx >= static_cast<int>(bonesDef.size())) return DirectX::XMMatrixIdentity();
			if (bindDone[static_cast<size_t>(idx)])
			{
				return DirectX::XMLoadFloat4x4(&bindGlobals[static_cast<size_t>(idx)]);
			}

			const auto& b = bonesDef[static_cast<size_t>(idx)];

			DirectX::XMMATRIX parentG = DirectX::XMMatrixIdentity();
			if (b.parentIndex >= 0)
			{
				parentG = self(self, b.parentIndex);
			}

			DirectX::XMVECTOR rel;
			if (b.parentIndex >= 0)
			{
				rel = DirectX::XMVectorSubtract(
					Load3(b.position),
					Load3(bonesDef[static_cast<size_t>(b.parentIndex)].position));
			}
			else
			{
				rel = Load3(b.position);
			}

			DirectX::XMMATRIX g = DirectX::XMMatrixTranslationFromVector(rel) * parentG;
			DirectX::XMStoreFloat4x4(&bindGlobals[static_cast<size_t>(idx)], g);
			bindDone[static_cast<size_t>(idx)] = 1;
			return g;
		};

	m_bodies.reserve(rbDefs.size() + static_cast<size_t>(m_settings.maxGeneratedBodyColliders));
	m_bodies.resize(rbDefs.size());

	// [FIX] 既に剛体が割り当てられているボーンを追跡するためのフラグ配列
	std::vector<bool> boneHasBody(bonesDef.size(), false);

	for (size_t i = 0; i < rbDefs.size(); ++i)
	{
		const auto& def = rbDefs[i];
		Body b{};
		b.defIndex = static_cast<int>(i);
		b.boneIndex = def.boneIndex;
		b.operation = def.operation;
		b.shapeType = def.shapeType;
		b.shapeSize = def.shapeSize;

		// [FIX] 剛体を持つボーンをマーク
		if (def.boneIndex >= 0 && def.boneIndex < static_cast<int>(bonesDef.size()))
		{
			boneHasBody[static_cast<size_t>(def.boneIndex)] = true;
		}

		if (b.shapeType == PmxModel::RigidBody::ShapeType::Box)
		{
			float hx = def.shapeSize.x * 0.5f;
			float hy = def.shapeSize.y * 0.5f;
			float hz = def.shapeSize.z * 0.5f;

			float longHalf = hy;
			float o1 = hx, o2 = hz;
			b.capsuleLocalAxis = { 0.0f, 1.0f, 0.0f };

			if (hx >= hy && hx >= hz)
			{
				longHalf = hx; o1 = hy; o2 = hz;
				b.capsuleLocalAxis = { 1.0f, 0.0f, 0.0f };
			}
			else if (hz >= hy && hz >= hx)
			{
				longHalf = hz; o1 = hx; o2 = hy;
				b.capsuleLocalAxis = { 0.0f, 0.0f, 1.0f };
			}

			float radius = std::sqrt(o1 * o1 + o2 * o2);

			b.capsuleRadius = std::max(radius, 1.0e-4f);
			b.capsuleHalfHeight = std::max(0.0f, longHalf - b.capsuleRadius);
		}
		else if (b.shapeType == PmxModel::RigidBody::ShapeType::Capsule)
		{
			b.capsuleLocalAxis = { 0.0f, 1.0f, 0.0f };
			b.capsuleRadius = def.shapeSize.x;
			b.capsuleHalfHeight = def.shapeSize.y * 0.5f;
		}
		else
		{
			b.capsuleLocalAxis = { 0.0f, 1.0f, 0.0f };
			b.capsuleRadius = def.shapeSize.x;
			b.capsuleHalfHeight = 0.0f;
		}

		b.linearDamping = def.linearDamping;
		b.angularDamping = def.angularDamping;
		b.group = def.groupIndex;
		b.groupMask = def.ignoreCollisionGroup;
		b.friction = def.friction;
		b.restitution = def.restitution;
		b.linearVelocity = { 0.0f, 0.0f, 0.0f };
		b.angularVelocity = { 0.0f, 0.0f, 0.0f };

		const bool dynamic = (def.operation != PmxModel::RigidBody::OperationType::Static);
		if (dynamic && def.mass > 0.0f)
		{
			b.invMass = 1.0f / def.mass;
			if (m_settings.maxInvMass > 0.0f) b.invMass = std::min(b.invMass, m_settings.maxInvMass);

			float effR = std::max(b.capsuleRadius, b.capsuleRadius + b.capsuleHalfHeight);
			effR = std::max(effR, 0.05f);

			float I = 0.4f * def.mass * effR * effR;
			float invI = (I > 0.0f) ? (1.0f / I) : 0.0f;
			if (invI > m_settings.maxInvInertia) invI = m_settings.maxInvInertia;
			b.invInertia = { invI, invI, invI };
		}

		DirectX::XMMATRIX rb0 =
			MatrixRotationEulerXYZ(def.rotation.x, def.rotation.y, def.rotation.z) *
			DirectX::XMMatrixTranslation(def.position.x, def.position.y, def.position.z);

		DirectX::XMMATRIX localFromBone = DirectX::XMMatrixIdentity();
		if (def.boneIndex >= 0 && def.boneIndex < static_cast<int>(bonesDef.size()))
		{
			DirectX::XMMATRIX bindBoneG = GetBindGlobal(GetBindGlobal, def.boneIndex);
			DirectX::XMMATRIX invBind = DirectX::XMMatrixInverse(nullptr, bindBoneG);
			localFromBone = invBind * rb0;
		}
		DirectX::XMStoreFloat4x4(&b.localFromBone, localFromBone);

		DecomposeTR(rb0, b.position, b.rotation);
		b.prevPosition = b.position;
		b.prevRotation = b.rotation;
		b.kinematicStartPos = b.position;
		b.kinematicStartRot = b.rotation;
		b.kinematicTargetPos = b.position;
		b.kinematicTargetRot = b.rotation;

		m_bodies[i] = b;
	}

	// Auto-generate kinematic body colliders
	if (m_settings.generateBodyCollidersIfMissing)
	{
		// [FIX] 既存剛体数のチェックを削除し、常に生成処理へ進みます。
		// 代わりにループ内で boneHasBody を確認して重複を避けます。
		if (true)
		{
			// Build child lists
			std::vector<std::vector<int>> children(bonesDef.size());
			for (size_t bi = 0; bi < bonesDef.size(); ++bi)
			{
				const int p = bonesDef[bi].parentIndex;
				if (p >= 0 && p < static_cast<int>(bonesDef.size()))
				{
					children[static_cast<size_t>(p)].push_back(static_cast<int>(bi));
				}
			}

			// Skeleton centroid
			XMVECTOR sum = XMVectorZero();
			int cnt = 0;
			for (const auto& bd : bonesDef)
			{
				sum = XMVectorAdd(sum, Load3(bd.position));
				++cnt;
			}
			XMVECTOR centroid = (cnt > 0) ? XMVectorScale(sum, 1.0f / static_cast<float>(cnt)) : XMVectorZero();

			// [FIX] Outlier判定を実質無効化（無限大）にして、手足などの遠いボーンもカリングされないようにします。
			constexpr float outlierR = std::numeric_limits<float>::max();

			struct Edge
			{
				int parent;
				int child;
				float len;
				float depth;
			};

			std::vector<Edge> edges;
			edges.reserve(bonesDef.size());

			const float minLen = std::max(0.0f, m_settings.generatedBodyColliderMinBoneLength);
			for (int p = 0; p < static_cast<int>(bonesDef.size()); ++p)
			{
				// [FIX] 既に剛体を持っているボーンには、追加のコライダーを生成しない
				if (boneHasBody[static_cast<size_t>(p)]) continue;

				const XMVECTOR pp = Load3(bonesDef[static_cast<size_t>(p)].position);

				// Outlier check removed (always passes with max outlierR)

				for (int c : children[static_cast<size_t>(p)])
				{
					const XMVECTOR pc = Load3(bonesDef[static_cast<size_t>(c)].position);
					// Outlier check removed

					const float len = Length3(XMVectorSubtract(pc, pp));
					if (len < minLen || !std::isfinite(len)) continue;

					edges.push_back({ p, c, len, ComputeDepth(bonesDef, p) });
				}
			}

			std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
					  {
						  if (a.len != b.len) return a.len > b.len;
						  return a.depth < b.depth;
					  });

			const int maxGen = std::clamp(m_settings.maxGeneratedBodyColliders, 0, 512);
			const int want = std::min<int>(maxGen, static_cast<int>(edges.size()));

			auto quatFromTo = [&](XMVECTOR from, XMVECTOR to) -> XMVECTOR
				{
					from = SafeNormalize3(from);
					to = SafeNormalize3(to);
					if (XMVectorGetX(XMVector3LengthSq(from)) < kEps || XMVectorGetX(XMVector3LengthSq(to)) < kEps)
						return XMQuaternionIdentity();

					float dot = XMVectorGetX(XMVector3Dot(from, to));
					dot = std::clamp(dot, -1.0f, 1.0f);

					if (dot > 0.9999f) return XMQuaternionIdentity();
					if (dot < -0.9999f)
					{
						XMVECTOR axis = XMVector3Cross(from, XMVectorSet(1, 0, 0, 0));
						if (XMVectorGetX(XMVector3LengthSq(axis)) < 1.0e-6f)
							axis = XMVector3Cross(from, XMVectorSet(0, 0, 1, 0));
						axis = SafeNormalize3(axis);
						return XMQuaternionRotationAxis(axis, XM_PI);
					}

					XMVECTOR axis = SafeNormalize3(XMVector3Cross(from, to));
					const float angle = std::acos(dot);
					return XMQuaternionRotationAxis(axis, angle);
				};

			const int genGroup = m_groupIndexIsOneBased ? 1 : 0;
			const uint16_t genMask = m_groupMaskIsCollisionMask ? 0xFFFFu : 0u;

			for (int ei = 0; ei < want; ++ei)
			{
				const Edge& e = edges[static_cast<size_t>(ei)];

				const XMVECTOR p0 = Load3(bonesDef[static_cast<size_t>(e.parent)].position);
				const XMVECTOR p1 = Load3(bonesDef[static_cast<size_t>(e.child)].position);

				XMVECTOR d = XMVectorSubtract(p1, p0);
				const float len = Length3(d);
				if (len < minLen) continue;
				XMVECTOR dir = XMVectorScale(d, 1.0f / std::max(len, kEps));

				const float rRatio = std::max(0.0f, m_settings.generatedBodyColliderRadiusRatio);
				float radius = len * rRatio;
				// [FIX] 設定値（MaxRadius）を利用して、MMDスケールに合った太さを許可します
				radius = std::clamp(radius, m_settings.generatedBodyColliderMinRadius, m_settings.generatedBodyColliderMaxRadius);
				if (!std::isfinite(radius)) radius = m_settings.generatedBodyColliderMinRadius;

				float hh = 0.5f * len - radius;
				if (hh < 0.0f) hh = 0.0f;

				XMVECTOR center = XMVectorScale(XMVectorAdd(p0, p1), 0.5f);
				XMVECTOR q = quatFromTo(XMVectorSet(0, 1, 0, 0), dir);

				XMFLOAT3 t{}; Store3(t, center);
				XMFLOAT4 r{}; Store4(r, q);

				Body b{};
				b.defIndex = -1; // 自動生成マーク
				b.boneIndex = e.parent;
				b.operation = PmxModel::RigidBody::OperationType::Static;
				b.shapeType = PmxModel::RigidBody::ShapeType::Capsule;
				b.shapeSize = { radius, 2.0f * hh, 0.0f };
				b.capsuleLocalAxis = { 0.0f, 1.0f, 0.0f };
				b.capsuleRadius = radius;
				b.capsuleHalfHeight = hh;

				b.invMass = 0.0f;
				b.invInertia = { 0.0f, 0.0f, 0.0f };

				b.group = genGroup;
				b.groupMask = genMask;
				b.friction = m_settings.generatedBodyColliderFriction;
				b.restitution = m_settings.generatedBodyColliderRestitution;

				// Bind-space transform -> localFromBone
				XMMATRIX rb0 = MatrixFromTR(t, r);
				XMMATRIX bindBoneG = GetBindGlobal(GetBindGlobal, b.boneIndex);
				XMMATRIX invBind = XMMatrixInverse(nullptr, bindBoneG);
				XMMATRIX localFromBone = invBind * rb0;
				XMStoreFloat4x4(&b.localFromBone, localFromBone);

				DecomposeTR(rb0, b.position, b.rotation);
				b.prevPosition = b.position;
				b.prevRotation = b.rotation;
				b.kinematicStartPos = b.position;
				b.kinematicStartRot = b.rotation;
				b.kinematicTargetPos = b.position;
				b.kinematicTargetRot = b.rotation;

				m_bodies.push_back(b);
			}
		}
	}

	BuildConstraints(model);

	const int n = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 512)
#endif
	for (int i = 0; i < n; ++i)
	{
		Body& b = m_bodies[i];

		DirectX::XMMATRIX rbCurrent = MatrixFromTR(b.position, b.rotation);

		const int boneIndex = b.boneIndex;
		if (boneIndex >= 0 && boneIndex < static_cast<int>(bonesDef.size()))
		{
			const auto& boneGlobalF = bones.GetBoneGlobalMatrix(static_cast<size_t>(boneIndex));
			DirectX::XMMATRIX boneG = DirectX::XMLoadFloat4x4(&boneGlobalF);
			DirectX::XMMATRIX localFromBone = DirectX::XMLoadFloat4x4(&b.localFromBone);
			rbCurrent = boneG * localFromBone;
		}

		DecomposeTR(rbCurrent, b.position, b.rotation);
		b.prevPosition = b.position;
		b.prevRotation = b.rotation;
		b.kinematicStartPos = b.position;
		b.kinematicStartRot = b.rotation;
		b.kinematicTargetPos = b.position;
		b.kinematicTargetRot = b.rotation;
	}

	m_isBuilt = true;
	m_builtRevision = model.Revision();
}

void MmdPhysicsWorld::BuildConstraints(const PmxModel& model)
{
	m_joints.clear();
	m_jointAdjacency.assign(m_bodies.size(), {});

	const auto& joints = model.Joints();
	if (joints.empty()) return;
	m_joints.reserve(joints.size());

	for (const auto& j : joints)
	{
		if (j.rigidBodyA < 0 || j.rigidBodyB < 0) continue;
		if (j.rigidBodyA >= static_cast<int>(m_bodies.size())) continue;
		if (j.rigidBodyB >= static_cast<int>(m_bodies.size())) continue;

		const Body& a0 = m_bodies[static_cast<size_t>(j.rigidBodyA)];
		const Body& b0 = m_bodies[static_cast<size_t>(j.rigidBodyB)];

		XMMATRIX Ta0 = MatrixFromTR(a0.position, a0.rotation);
		XMMATRIX Tb0 = MatrixFromTR(b0.position, b0.rotation);

		XMMATRIX Tj0 = MatrixRotationEulerXYZ(j.rotation.x, j.rotation.y, j.rotation.z) *
			XMMatrixTranslation(j.position.x, j.position.y, j.position.z);

		XMMATRIX invTa0 = XMMatrixInverse(nullptr, Ta0);
		XMMATRIX invTb0 = XMMatrixInverse(nullptr, Tb0);

		XMMATRIX J_in_A = Tj0 * invTa0;
		XMMATRIX J_in_B = Tj0 * invTb0;

		XMFLOAT3 tJA, tJB;
		XMFLOAT4 rJA, rJB;
		DecomposeTR(J_in_A, tJA, rJA);
		DecomposeTR(J_in_B, tJB, rJB);

		JointConstraint c{};
		c.bodyA = j.rigidBodyA;
		c.bodyB = j.rigidBodyB;
		c.localAnchorA = tJA;
		c.localAnchorB = tJB;
		c.rotAtoJ = rJA;
		c.rotBtoJ = rJB;

		c.posLower = j.positionLower;
		c.posUpper = j.positionUpper;
		c.rotLower = j.rotationLower;
		c.rotUpper = j.rotationUpper;
		c.positionSpring = j.springPosition;
		c.rotationSpring = j.springRotation;
		c.lambdaPos = 0.0f;

		m_joints.push_back(c);

		m_jointAdjacency[static_cast<size_t>(c.bodyA)].push_back(static_cast<uint32_t>(c.bodyB));
		m_jointAdjacency[static_cast<size_t>(c.bodyB)].push_back(static_cast<uint32_t>(c.bodyA));
	}

	// [OPT] Speed up IsJointConnected(): sort adjacency lists once here.
	for (auto& adj : m_jointAdjacency)
	{
		std::sort(adj.begin(), adj.end());
		adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
	}

}

bool MmdPhysicsWorld::IsJointConnected(uint32_t a, uint32_t b) const
{
	if (a >= m_jointAdjacency.size()) return false;
	const auto& adj = m_jointAdjacency[a];
	return std::binary_search(adj.begin(), adj.end(), b);
}

void MmdPhysicsWorld::Step(double dtSeconds, const PmxModel& model, BoneSolver& bones)
{
	if (!m_isBuilt || m_builtRevision != model.Revision())
	{
		BuildFromModel(model, bones);
	}
	if (m_bodies.empty()) return;

	m_accumulator += dtSeconds;
	const double maxAcc = m_settings.fixedTimeStep * static_cast<double>(m_settings.maxCatchUpSteps);
	if (m_accumulator > maxAcc) m_accumulator = maxAcc;

	int stepCount = 0;
	while (m_accumulator >= m_settings.fixedTimeStep && stepCount < m_settings.maxCatchUpSteps)
	{
		PrecomputeKinematicTargets(model, bones);

		if (ShouldSkipPhysicsTick())
		{
			m_accumulator -= m_settings.fixedTimeStep;
			++stepCount;
			continue;
		}

		const int subSteps = std::max(1, m_settings.maxSubSteps);
		const float subStepDt = m_settings.fixedTimeStep / static_cast<float>(subSteps);

		for (int sub = 0; sub < subSteps; ++sub)
		{
			BeginSubStep();

			float t = static_cast<float>(sub + 1) / static_cast<float>(subSteps);
			InterpolateKinematicBodies(t);

			Integrate(subStepDt, model);


			for (int it = 0; it < m_settings.solverIterations; ++it)
			{
				SolveJoints(subStepDt);
			}

			if (m_settings.collisionIterations > 0)
			{
				// Body-body collisions: SAP broadphase once, then iterate m_settings.collisionIterations internally.
				SolveBodyCollisions(subStepDt);

				// Ground contacts: keep the same iteration count as before for stability.
				for (int it = 0; it < m_settings.collisionIterations; ++it)
				{
					SolveGround(subStepDt, model);
				}
			}

			EndSubStep(subStepDt, model);
		}

		m_accumulator -= m_settings.fixedTimeStep;
		++stepCount;
	}

	WriteBackBones(model, bones);
}

void MmdPhysicsWorld::PrecomputeKinematicTargets(const PmxModel& model, const BoneSolver& bones)
{
	const auto& bonesDef = model.Bones();

	m_anyKinematicMovedThisTick = false;

	for (size_t i = 0; i < m_bodies.size(); ++i)
	{
		Body& b = m_bodies[i];

		if (b.invMass > 0.0f) continue;

		const int boneIndex = b.boneIndex;
		if (boneIndex < 0 || boneIndex >= static_cast<int>(bonesDef.size())) continue;

		b.kinematicStartPos = b.position;
		b.kinematicStartRot = b.rotation;

		const auto& boneGlobalF = bones.GetBoneGlobalMatrix(static_cast<size_t>(boneIndex));
		DirectX::XMMATRIX boneG = DirectX::XMLoadFloat4x4(&boneGlobalF);
		DirectX::XMMATRIX localFromBone = DirectX::XMLoadFloat4x4(&b.localFromBone);
		DirectX::XMMATRIX rbG = boneG * localFromBone;

		DirectX::XMFLOAT3 prevT = b.kinematicTargetPos;
		DirectX::XMFLOAT4 prevR = b.kinematicTargetRot;

		DecomposeTR(rbG, b.kinematicTargetPos, b.kinematicTargetRot);

		const float dx = b.kinematicTargetPos.x - prevT.x;
		const float dy = b.kinematicTargetPos.y - prevT.y;
		const float dz = b.kinematicTargetPos.z - prevT.z;
		const float dp2 = dx * dx + dy * dy + dz * dz;

		const float dot =
			std::fabs(prevR.x * b.kinematicTargetRot.x +
					  prevR.y * b.kinematicTargetRot.y +
					  prevR.z * b.kinematicTargetRot.z +
					  prevR.w * b.kinematicTargetRot.w);

		if (dp2 > 1.0e-10f || (1.0f - dot) > 1.0e-6f)
			m_anyKinematicMovedThisTick = true;
	}
}

bool MmdPhysicsWorld::ShouldSkipPhysicsTick()
{
	const float vThr = m_settings.sleepLinearSpeed;
	const float wThr = m_settings.sleepAngularSpeed;

	if (vThr <= 0.0f && wThr <= 0.0f)
	{
		m_sleepCounter = 0;
		m_worldSleeping = false;
		return false;
	}

	// 骨が動いたら即起床
	if (m_anyKinematicMovedThisTick)
	{
		m_sleepCounter = 0;
		m_worldSleeping = false;
		return false;
	}

	const float v2Thr = (vThr > 0.0f) ? (vThr * vThr) : 0.0f;
	const float w2Thr = (wThr > 0.0f) ? (wThr * wThr) : 0.0f;

	bool anyMoving = false;
	for (const Body& b : m_bodies)
	{
		if (b.invMass <= 0.0f) continue; // dynamicのみ

		const float vx = b.linearVelocity.x, vy = b.linearVelocity.y, vz = b.linearVelocity.z;
		const float wx = b.angularVelocity.x, wy = b.angularVelocity.y, wz = b.angularVelocity.z;

		const float v2 = vx * vx + vy * vy + vz * vz;
		const float w2 = wx * wx + wy * wy + wz * wz;

		if ((v2Thr > 0.0f && v2 > v2Thr) || (w2Thr > 0.0f && w2 > w2Thr))
		{
			anyMoving = true;
			break;
		}
	}

	if (anyMoving)
	{
		m_sleepCounter = 0;
		m_worldSleeping = false;
		return false;
	}

	// しばらく静止したらsleep
	if (++m_sleepCounter >= 10)
		m_worldSleeping = true;

	return m_worldSleeping;
}

void MmdPhysicsWorld::InterpolateKinematicBodies(float t)
{
	const int n = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 512)
#endif
	for (int i = 0; i < n; ++i)
	{
		Body& b = m_bodies[static_cast<size_t>(i)];
		// Only strictly interpolate Kinematic bodies (Static/Kinematic type)
		if (b.invMass > 0.0f) continue;

		XMVECTOR p0 = Load3(b.kinematicStartPos);
		XMVECTOR p1 = Load3(b.kinematicTargetPos);
		XMVECTOR q0 = Load4(b.kinematicStartRot);
		XMVECTOR q1 = Load4(b.kinematicTargetRot);

		XMVECTOR p = XMVectorLerp(p0, p1, t);
		XMVECTOR q = XMQuaternionSlerp(q0, q1, t);

		Store3(b.position, p);
		Store4(b.rotation, q);
	}
}

void MmdPhysicsWorld::BeginSubStep()
{
	float ws = std::clamp(m_settings.jointWarmStart, 0.0f, 1.0f);
	for (auto& c : m_joints)
	{
		c.lambdaPos *= ws;
	}

	const int nb = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(nb >= 256)
#endif
	for (int i = 0; i < nb; ++i)
	{
		Body& b = m_bodies[static_cast<size_t>(i)];
		b.prevPosition = b.position;
		b.prevRotation = b.rotation;
	}
}

void MmdPhysicsWorld::Integrate(float dt, const PmxModel& model)
{
	XMVECTOR g = XMVectorSet(m_settings.gravity.x, m_settings.gravity.y, m_settings.gravity.z, 0.0f);

	const int n = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 512)
#endif
	for (int i = 0; i < n; ++i)
	{
		Body& b = m_bodies[static_cast<size_t>(i)];
		if (b.invMass <= 0.0f) continue;

		if (!IsVectorFinite3(Load3(b.position))) continue;

		XMVECTOR v = Load3(b.linearVelocity);
		v = XMVectorAdd(v, XMVectorScale(g, dt));

		XMVECTOR p = Load3(b.position);
		p = XMVectorAdd(p, XMVectorScale(v, dt));
		Store3(b.position, p);
		Store3(b.linearVelocity, v);

		XMVECTOR q = Load4(b.rotation);
		XMVECTOR w = Load3(b.angularVelocity);
		XMVECTOR dq = QuaternionFromAngularVelocity(w, dt);
		q = XMQuaternionNormalize(XMQuaternionMultiply(dq, q));
		Store4(b.rotation, q);
	}
}

void MmdPhysicsWorld::SolveXPBD(float dt, const PmxModel& model)
{
	SolveJoints(dt);
	SolveBodyCollisions(dt);
	SolveGround(dt, model);
}

void MmdPhysicsWorld::SolveBodyCollisions(float dt)
{
	if (!m_settings.enableRigidBodyCollisions) return;
	const size_t bodyCount = m_bodies.size();
	if (bodyCount < 2) return;
	const int bodyCountInt = static_cast<int>(bodyCount);

	// --- 1. 形状キャッシュの更新 (並列化) ---
	// ワールド座標系の形状データを一括計算し、後の判定ループでの計算コストを削減

	if (m_shapeCache.size() < m_bodies.size())
	{
		m_shapeCache.resize(m_bodies.size());
	}

	// SAP用バッファのリサイズ
	if (m_axisList.size() < m_bodies.size())
	{
		m_axisList.resize(m_bodies.size());
		m_radii.resize(m_bodies.size());
		m_maxXs.resize(m_bodies.size());
	}

	std::span<CollisionShapeCache> shapeCache{ m_shapeCache.data(), bodyCount };
	std::span<SapNode> axisList{ m_axisList.data(), bodyCount };
	std::span<float> radii{ m_radii.data(), bodyCount };
	std::span<float> maxXs{ m_maxXs.data(), bodyCount };

	const float radiusScale = m_settings.collisionRadiusScale;
	const float collisionMargin = m_settings.collisionMargin;
	const float kPhantomMargin = std::max(0.0f, m_settings.phantomMargin);

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(bodyCount >= 512)
#endif
	for (int i = 0; i < bodyCountInt; ++i)
	{
		const Body& b = m_bodies[i];
		CollisionShapeCache& cache = shapeCache[static_cast<size_t>(i)];

		using namespace DirectX;
		XMVECTOR c = XMLoadFloat3(&b.position);
		XMVECTOR q = XMLoadFloat4(&b.rotation);

		const float extra = (b.invMass <= 0.0f) ? kPhantomMargin : 0.0f;

		cache.isBox = (b.shapeType == PmxModel::RigidBody::ShapeType::Box);

		float minX, maxX;

		if (cache.isBox)
		{
			cache.p0 = c; // Box Center
			cache.rotation = q;
			cache.ex = std::max(kEps, b.shapeSize.x * radiusScale * 0.5f);
			cache.ey = std::max(kEps, b.shapeSize.y * radiusScale * 0.5f);
			cache.ez = std::max(kEps, b.shapeSize.z * radiusScale * 0.5f);
			cache.radius = 0.0f; // Box自体には使わない

			// AABB (X軸) の計算 for SAP
			const XMVECTOR ux = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), q);
			const XMVECTOR uy = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);
			const XMVECTOR uz = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);

			const float ax = std::abs(XMVectorGetX(ux));
			const float ay = std::abs(XMVectorGetX(uy));
			const float az = std::abs(XMVectorGetX(uz));

			float extX = ax * cache.ex + ay * cache.ey + az * cache.ez;
			extX += (collisionMargin + extra);

			const float cx = XMVectorGetX(c);
			minX = cx - extX;
			maxX = cx + extX;

			// バウンディング球半径 (保守的)
			radii[static_cast<size_t>(i)] = std::sqrt(cache.ex * cache.ex + cache.ey * cache.ey + cache.ez * cache.ez)
				+ collisionMargin + extra;
		}
		else
		{
			// Capsule / Sphere
			cache.radius = b.capsuleRadius * radiusScale;
			float hh = b.capsuleHalfHeight;
			if (hh > kEps)
			{
				XMVECTOR axisL = XMLoadFloat3(&b.capsuleLocalAxis);
				XMVECTOR axisW = XMVector3Rotate(axisL, q);
				XMVECTOR o = XMVectorScale(axisW, hh);
				cache.p0 = XMVectorSubtract(c, o);
				cache.p1 = XMVectorAdd(c, o);
			}
			else
			{
				cache.p0 = c;
				cache.p1 = c;
			}

			float rTotal = cache.radius + collisionMargin + extra;
			float x0 = XMVectorGetX(cache.p0);
			float x1 = XMVectorGetX(cache.p1);
			minX = std::min(x0, x1) - rTotal;
			maxX = std::max(x0, x1) + rTotal;

			radii[static_cast<size_t>(i)] = b.capsuleHalfHeight + cache.radius + collisionMargin + extra;
		}

		axisList[static_cast<size_t>(i)] = { minX, i };
		maxXs[static_cast<size_t>(i)] = maxX;
	}

	auto shouldCollide = [&](int i, int j) -> bool
		{
			if (!m_settings.respectCollisionGroups) return true;
			const Body& A = m_bodies[static_cast<size_t>(i)];
			const Body& B = m_bodies[static_cast<size_t>(j)];

			uint16_t maskA = A.groupMask;
			uint16_t maskB = B.groupMask;

			int groupA = A.group;
			int groupB = B.group;
			if (m_groupIndexIsOneBased)
			{
				groupA -= 1; groupB -= 1;
			}

			if (groupA < 0 || groupA >= 16 || groupB < 0 || groupB >= 16) return true;

			const uint16_t bitA = static_cast<uint16_t>(1u << groupA);
			const uint16_t bitB = static_cast<uint16_t>(1u << groupB);

			if (m_groupMaskIsCollisionMask)
			{
				// mask が「当たる相手」ビット
				return ((maskA & bitB) != 0) && ((maskB & bitA) != 0);
			}
			else
			{
				// mask が「当たらない相手」ビット
				return ((maskA & bitB) == 0) && ((maskB & bitA) == 0);
			}
		};

	// --- 2. Broadphase (SAP) ---
	// 前フレームからほぼ整列している前提で挿入ソートを使用し、O(N) に近いコストで更新する。
	auto insertionSort = [](std::span<SapNode> nodes)
		{
			for (size_t i = 1; i < nodes.size(); ++i)
			{
				SapNode key = nodes[i];
				size_t j = i;
				while (j > 0 && key.minX < nodes[j - 1].minX)
				{
					nodes[j] = nodes[j - 1];
					--j;
				}
				nodes[j] = key;
			}
		};
	insertionSort(axisList);

	m_candidates.clear();
	// 適切な予約サイズ確保 (前回のサイズ等を参考にするとより良い)
	const size_t candidateHint = bodyCount * 4;
	if (m_candidates.capacity() < candidateHint)
	{
		m_candidates.reserve(bodyCount * 8);
	}

	for (int i = 0; i < bodyCountInt; ++i)
	{
		const int idxA = axisList[static_cast<size_t>(i)].index;
		const float maxX_A = maxXs[static_cast<size_t>(idxA)];

		// 静的オブジェクト同士の衝突を除外するためのフラグ
		const bool isStaticA = (m_bodies[idxA].invMass <= 0.0f);
		using namespace DirectX;
		XMVECTOR posA = XMLoadFloat3(&m_bodies[idxA].position);

		for (int j = i + 1; j < bodyCountInt; ++j)
		{
			if (axisList[static_cast<size_t>(j)].minX > maxX_A) break;

			const int idxB = axisList[static_cast<size_t>(j)].index;

			// 両方Staticなら無視
			if (isStaticA && m_bodies[idxB].invMass <= 0.0f) continue;

			// ジョイント接続チェック
			if (!m_settings.collideJointConnectedBodies && IsJointConnected(idxA, idxB)) continue;

			// グループフィルタ
			if (!shouldCollide(idxA, idxB)) continue; // shouldCollideはラムダとして定義するかメンバ関数化が必要

			// Sphere Check (保守的判定)
			XMVECTOR posB = XMLoadFloat3(&m_bodies[idxB].position);
			float rSum = radii[static_cast<size_t>(idxA)] + radii[static_cast<size_t>(idxB)];
			XMVECTOR dp = XMVectorSubtract(posB, posA);
			if (XMVectorGetX(XMVector3LengthSq(dp)) > rSum * rSum) continue;

			m_candidates.push_back({ idxA, idxB });
		}
	}

	if (m_candidates.empty()) return;

	// --- 3. Narrow Phase & Solver ---
	const float slop = std::max(0.0f, m_settings.contactSlop);
	const float alpha = m_settings.contactCompliance / (dt * dt);
	const float maxDist = (m_settings.maxDepenetrationVelocity > 0.0f)
		? (m_settings.maxDepenetrationVelocity * dt)
		: std::numeric_limits<float>::max();

	for (int iter = 0; iter < m_settings.collisionIterations; ++iter)
	{
		for (const auto& pair : m_candidates)
		{
			int idxA = pair.a;
			int idxB = pair.b;
			Body& A = m_bodies[idxA];
			Body& B = m_bodies[idxB];
			const CollisionShapeCache& cA = m_shapeCache[idxA];
			const CollisionShapeCache& cB = m_shapeCache[idxB];

			using namespace DirectX;

			XMVECTOR n = XMVectorZero();
			float penetration = 0.0f;
			XMVECTOR posA = XMVectorZero();
			XMVECTOR posB = XMVectorZero();
			bool hit = false;

			const bool isMixed = ((A.invMass <= 0.0f) != (B.invMass <= 0.0f));
			const float extraA = (isMixed && (A.invMass <= 0.0f)) ? kPhantomMargin : 0.0f;
			const float extraB = (isMixed && (B.invMass <= 0.0f)) ? kPhantomMargin : 0.0f;
			const float marginTotal = collisionMargin + extraA + extraB;

			// 衝突判定: 事前計算キャッシュを利用して高速化
			if (!cA.isBox && !cB.isBox) // Capsule vs Capsule
			{
				const float minDist = cA.radius + cB.radius + marginTotal;
				XMVECTOR segC1, segC2;
				ClosestPtSegmentSegment(cA.p0, cA.p1, cB.p0, cB.p1, segC1, segC2);

				XMVECTOR d = XMVectorSubtract(segC2, segC1);
				float dist = XMVectorGetX(XMVector3Length(d));

				if (dist < minDist)
				{
					float p = minDist - dist;
					if (p >= slop)
					{
						penetration = p - slop;
						n = (dist > kEps) ? XMVectorScale(d, 1.0f / dist) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
						posA = segC1;
						posB = segC2;
						hit = true;
					}
				}
			}
			else
			{
				// Box絡みは複雑なので、既存の関数を利用しつつ必要なパラメータをキャッシュから渡す
				// (ここでは簡略化のため、元のロジックと同様の判定を行う関数をインライン展開または呼び出し)

				// Capsule vs Box
				if (!cA.isBox && cB.isBox)
				{
					if (ContactCapsule_OBB(cA.p0, cA.p1, cA.radius + marginTotal,
										   cB.p0, cB.rotation, cB.ex + extraB, cB.ey + extraB, cB.ez + extraB,
										   n, penetration, posA, posB))
					{
						if (penetration >= slop)
						{
							penetration -= slop; hit = true;
						}
					}
				}
				// Box vs Capsule
				else if (cA.isBox && !cB.isBox)
				{
					if (ContactCapsule_OBB(cB.p0, cB.p1, cB.radius + marginTotal,
										   cA.p0, cA.rotation, cA.ex + extraA, cA.ey + extraA, cA.ez + extraA,
										   n, penetration, posB, posA)) // 引数順注意: posB->Cap, posA->Box
					{
						if (penetration >= slop)
						{
							penetration -= slop;
							n = XMVectorNegate(n); // A->Bへ
							hit = true;
							// posA/posBはContactCapsule_OBB内で適切に設定されている前提
						}
					}
				}
				// Box vs Box
				else
				{
					float exA = cA.ex + collisionMargin * 0.5f + extraA;
					float eyA = cA.ey + collisionMargin * 0.5f + extraA;
					float ezA = cA.ez + collisionMargin * 0.5f + extraA;
					float exB = cB.ex + collisionMargin * 0.5f + extraB;
					float eyB = cB.ey + collisionMargin * 0.5f + extraB;
					float ezB = cB.ez + collisionMargin * 0.5f + extraB;

					if (ContactOBB_OBB(cA.p0, cA.rotation, exA, eyA, ezA,
									   cB.p0, cB.rotation, exB, eyB, ezB,
									   n, penetration, posA, posB))
					{
						if (penetration >= slop)
						{
							penetration -= slop; hit = true;
						}
					}
				}
			}

			if (!hit) continue;

			// --- ソルバー適用 (Impulse Apply) ---
			// ※ここは元のロジックと同一だが、計算済みの値を使用

			float wA = A.invMass;
			float wB = B.invMass;

			XMVECTOR pA0 = XMLoadFloat3(&A.position);
			XMVECTOR pB0 = XMLoadFloat3(&B.position);
			XMVECTOR leverA = XMVectorSubtract(posA, pA0);
			XMVECTOR leverB = XMVectorSubtract(posB, pB0);

			float wAngA = 0.0f, wAngB = 0.0f;
			if (wA > 0.0f)
			{
				XMVECTOR rxn = XMVector3Cross(leverA, n);
				wAngA = XMVectorGetX(XMVector3Dot(XMVectorMultiply(XMLoadFloat3(&A.invInertia), rxn), rxn));
			}
			if (wB > 0.0f)
			{
				XMVECTOR rxn = XMVector3Cross(leverB, n);
				wAngB = XMVectorGetX(XMVector3Dot(XMVectorMultiply(XMLoadFloat3(&B.invInertia), rxn), rxn));
			}

			// Stabilization
			XMVECTOR vA_cur = XMVectorSubtract(pA0, XMLoadFloat3(&A.prevPosition));
			XMVECTOR vB_cur = XMVectorSubtract(pB0, XMLoadFloat3(&B.prevPosition));
			float vn = XMVectorGetX(XMVector3Dot(XMVectorSubtract(vA_cur, vB_cur), n));

			float currentAlpha = alpha;
			if (std::abs(vn) < 0.2f * dt) currentAlpha *= 10.0f;

			float wTotal = wA + wB + wAngA + wAngB + currentAlpha;
			if (wTotal < kEps) continue;

			float dLambda = penetration / wTotal;
			dLambda = std::min(dLambda, maxDist);

			XMVECTOR dp = XMVectorScale(n, dLambda);
			XMVECTOR frictionImpulse = XMVectorZero();

			// Friction
			float mu = A.friction * B.friction;
			if (mu > 0.0f)
			{
				XMVECTOR vrel = XMVectorSubtract(vA_cur, vB_cur);
				XMVECTOR vnVec = XMVectorScale(n, XMVectorGetX(XMVector3Dot(vrel, n)));
				XMVECTOR vt = XMVectorSubtract(vrel, vnVec);
				float vtLen = XMVectorGetX(XMVector3Length(vt));

				if (vtLen > kEps)
				{
					if (vtLen < 0.05f * dt) mu *= 2.0f;
					float mag = std::min(vtLen, dLambda * mu);
					frictionImpulse = XMVectorScale(vt, -mag / vtLen);
				}
			}

			// Apply
			auto applyImpulse = [&](Body& body, XMVECTOR imp, XMVECTOR lever) {
				if (body.invMass <= 0.0f) return;
				XMVECTOR p = XMLoadFloat3(&body.position);
				p = XMVectorAdd(p, XMVectorScale(imp, body.invMass));
				XMStoreFloat3(&body.position, p);

				XMVECTOR T = XMVector3Cross(lever, imp);
				XMVECTOR dTheta = XMVectorMultiply(XMLoadFloat3(&body.invInertia), T);
				if (!IsVectorFinite3(dTheta)) return;
				float maxTheta = std::max(0.0f, m_settings.maxAngularSpeed) * dt;
				float theta = XMVectorGetX(XMVector3Length(dTheta));
				if (maxTheta > 0.0f && theta > maxTheta)
				{
					dTheta = XMVectorScale(dTheta, maxTheta / std::max(theta, kEps));
					theta = maxTheta;
				}

				if (theta > kEps)
				{
					XMVECTOR dq = QuaternionFromRotationVector(dTheta);
					if (!IsVectorFinite4(dq)) return;

					XMVECTOR q = Load4(body.rotation);
					if (!IsVectorFinite4(q)) return;

					XMStoreFloat4(&body.rotation, XMQuaternionNormalize(XMQuaternionMultiply(dq, q)));
				}
			};

			applyImpulse(A, XMVectorAdd(XMVectorNegate(dp), frictionImpulse), leverA);
			applyImpulse(B, XMVectorSubtract(dp, frictionImpulse), leverB);
		}
	}
}

void MmdPhysicsWorld::SolveJoints(float dt)
{
	if (m_joints.empty()) return;
	const float alphaPos = m_settings.jointCompliance / (std::max(dt, kEps) * std::max(dt, kEps));

	for (auto& c : m_joints)
	{
		Body& A = m_bodies[static_cast<size_t>(c.bodyA)];
		Body& B = m_bodies[static_cast<size_t>(c.bodyB)];
		float wA = A.invMass, wB = B.invMass;
		if (wA + wB <= 0.0f) continue;

		XMVECTOR qA = Load4(A.rotation);
		XMVECTOR qB = Load4(B.rotation);
		XMVECTOR pA = Load3(A.position);
		XMVECTOR pB = Load3(B.position);

		if (!IsVectorFinite3(pA) || !IsVectorFinite3(pB)) continue;

		XMVECTOR qJA = Load4(c.rotAtoJ);
		XMVECTOR qJB = Load4(c.rotBtoJ);
		XMVECTOR qJ_WorldA = XMQuaternionMultiply(qJA, qA);
		XMVECTOR qJ_WorldB = XMQuaternionMultiply(qJB, qB);
		XMVECTOR qDiff = XMQuaternionMultiply(XMQuaternionConjugate(qJ_WorldA), qJ_WorldB);

		XMFLOAT3 euler = QuaternionToEulerXYZ(qDiff);

		auto WrapPi = [](float a) -> float {
			a = std::fmod(a + DirectX::XM_PI, DirectX::XM_2PI);
			if (a < 0.0f) a += DirectX::XM_2PI;
			return a - DirectX::XM_PI;
			};
		euler.x = WrapPi(euler.x); euler.y = WrapPi(euler.y); euler.z = WrapPi(euler.z);

		bool clamped = false;
		auto clampAxis = [&](float& val, float minV, float maxV) {
			if (val < minV)
			{
				val += (minV - val) * 0.8f; clamped = true;
			}
			else if (val > maxV)
			{
				val -= (val - maxV) * 0.8f; clamped = true;
			}
			};

		clampAxis(euler.x, c.rotLower.x, c.rotUpper.x);
		clampAxis(euler.y, c.rotLower.y, c.rotUpper.y);
		clampAxis(euler.z, c.rotLower.z, c.rotUpper.z);

		if (!clamped)
		{
			float sx = c.rotationSpring.x * m_settings.springStiffnessScale;
			float sy = c.rotationSpring.y * m_settings.springStiffnessScale;
			float sz = c.rotationSpring.z * m_settings.springStiffnessScale;

			float stiffness = std::max({ sx, sy, sz });
			if (stiffness > 0.0f)
			{
				float factor = std::clamp(stiffness * dt, 0.0f, m_settings.maxSpringCorrectionRate);
				euler.x *= (1.0f - factor); euler.y *= (1.0f - factor); euler.z *= (1.0f - factor);
				clamped = true;
			}
		}

		if (clamped)
		{
			XMVECTOR qDiffNew = EulerXYZToQuaternion(euler.x, euler.y, euler.z);
			XMVECTOR qJ_WorldB_Target = XMQuaternionMultiply(qJ_WorldA, qDiffNew);
			XMVECTOR qB_Target = XMQuaternionMultiply(XMQuaternionConjugate(qJB), qJ_WorldB_Target);
			XMVECTOR qDelta = XMQuaternionMultiply(qB_Target, XMQuaternionConjugate(qB));
			qDelta = XMQuaternionNormalize(qDelta);

			float totalW = wA + wB;
			float ratioB = wB / totalW;
			float ratioA = wA / totalW;

			XMVECTOR axis; float ang;
			XMQuaternionToAxisAngle(&axis, &ang, qDelta);

			if (ang > XM_PI) ang -= XM_2PI; else if (ang < -XM_PI) ang += XM_2PI;

			XMVECTOR dqB = SafeQuaternionRotationAxis(axis, ang * ratioB);
			XMVECTOR dqA = SafeQuaternionRotationAxis(axis, -ang * ratioA);
			qB = XMQuaternionNormalize(XMQuaternionMultiply(dqB, qB));
			qA = XMQuaternionNormalize(XMQuaternionMultiply(dqA, qA));
			Store4(A.rotation, qA); Store4(B.rotation, qB);
		}

		XMVECTOR rA = RotateVector(Load3(c.localAnchorA), qA);
		XMVECTOR rB = RotateVector(Load3(c.localAnchorB), qB);
		XMVECTOR ancA = XMVectorAdd(pA, rA);
		XMVECTOR ancB = XMVectorAdd(pB, rB);

		XMVECTOR distVec = XMVectorSubtract(ancA, ancB);
		float dist = Length3(distVec);
		if (dist < kEps) continue;

		XMVECTOR n = XMVectorScale(distVec, 1.0f / dist);

		float wAngA = 0.0f, wAngB = 0.0f;
		if (wA > 0.0f)
		{
			XMVECTOR rxn = XMVector3Cross(rA, n);
			wAngA = Dot3(rxn, XMVectorMultiply(Load3(A.invInertia), rxn));
		}
		if (wB > 0.0f)
		{
			XMVECTOR rxn = XMVector3Cross(rB, n);
			wAngB = Dot3(rxn, XMVectorMultiply(Load3(B.invInertia), rxn));
		}

		float wTot = wA + wB + wAngA + wAngB + alphaPos;
		float dLambda = (-dist - alphaPos * c.lambdaPos) / wTot;
		c.lambdaPos += dLambda;
		XMVECTOR P = XMVectorScale(n, dLambda);

		{
			float maxP = std::max(0.0f, m_settings.maxJointPositionCorrection);
			if (maxP > 0.0f)
			{
				float pLen = Length3(P);
				if (pLen > maxP) P = XMVectorScale(P, maxP / pLen);
			}
		}

		if (wA > 0.0f)
		{
			XMVECTOR dp = XMVectorScale(P, wA);
			Store3(A.position, XMVectorAdd(pA, dp));
			XMVECTOR T = XMVector3Cross(rA, P);
			XMVECTOR dTheta = XMVectorMultiply(Load3(A.invInertia), T);
			float theta = Length3(dTheta);
			float maxTheta = std::max(0.0f, m_settings.maxJointAngularCorrection);
			if (maxTheta > 0.0f && theta > maxTheta) dTheta = XMVectorScale(dTheta, maxTheta / theta);
			XMVECTOR dqRot = QuaternionFromRotationVector(dTheta);
			Store4(A.rotation, XMQuaternionNormalize(XMQuaternionMultiply(dqRot, qA)));
		}
		if (wB > 0.0f)
		{
			XMVECTOR Pneg = XMVectorNegate(P);
			XMVECTOR dp = XMVectorScale(Pneg, wB);
			Store3(B.position, XMVectorAdd(pB, dp));
			XMVECTOR T = XMVector3Cross(rB, Pneg);
			XMVECTOR dTheta = XMVectorMultiply(Load3(B.invInertia), T);
			float theta = Length3(dTheta);
			float maxTheta = std::max(0.0f, m_settings.maxJointAngularCorrection);
			if (maxTheta > 0.0f && theta > maxTheta) dTheta = XMVectorScale(dTheta, maxTheta / theta);
			XMVECTOR dqRot = QuaternionFromRotationVector(dTheta);
			Store4(B.rotation, XMQuaternionNormalize(XMQuaternionMultiply(dqRot, qB)));
		}
	}
}

void MmdPhysicsWorld::SolveGround(float dt, const PmxModel& model)
{
	(void)model;
	const float ground = m_settings.groundY;
	const float alpha = m_settings.contactCompliance / (dt * dt);
	const float maxDist = (m_settings.maxDepenetrationVelocity > 0.0f)
		? (m_settings.maxDepenetrationVelocity * dt)
		: std::numeric_limits<float>::max();

	// [最適化] 剛体ごとの地面判定は独立しているため並列化
	const int n = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 512)
#endif
	for (int i = 0; i < n; ++i)
	{
		// ループ変数をローカル参照で受ける
		auto& b = m_bodies[i];

		if (b.invMass <= 0.0f) continue;
		if (!IsVectorFinite3(Load3(b.position))) continue;

		float r = (b.capsuleRadius * m_settings.collisionRadiusScale) + m_settings.collisionMargin;

		DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&b.position);
		float yMinEnd = DirectX::XMVectorGetY(p);

		if (b.capsuleHalfHeight > kEps)
		{
			DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&b.rotation);
			DirectX::XMVECTOR axisLocal = DirectX::XMVectorSet(b.capsuleLocalAxis.x, b.capsuleLocalAxis.y, b.capsuleLocalAxis.z, 0.0f);
			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(axisLocal)) < kEps)
				axisLocal = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			axisLocal = DirectX::XMVector3Normalize(axisLocal);
			DirectX::XMVECTOR axis = DirectX::XMVector3Rotate(axisLocal, q);
			DirectX::XMVECTOR o = DirectX::XMVectorScale(axis, b.capsuleHalfHeight);
			DirectX::XMVECTOR p0 = DirectX::XMVectorAdd(p, o);
			DirectX::XMVECTOR p1 = DirectX::XMVectorSubtract(p, o);
			yMinEnd = std::min(DirectX::XMVectorGetY(p0), DirectX::XMVectorGetY(p1));
		}

		float target = ground + r;
		float C = target - yMinEnd;
		if (C <= 0.0f) continue;

		float s = b.invMass / (b.invMass + alpha);
		float dy = C * s;
		if (dy > maxDist) dy = maxDist;

		b.position.y += dy;
	}
}

void MmdPhysicsWorld::EndSubStep(float dt, const PmxModel& model)
{
	(void)model;
	float invDt = 1.0f / std::max(dt, kEps);

	// [最適化] 速度更新ループの並列化
	const int n = static_cast<int>(m_bodies.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(n >= 512)
#endif
	for (int i = 0; i < n; ++i)
	{
		Body& b = m_bodies[i];
		if (b.operation == PmxModel::RigidBody::OperationType::Static) continue;
		if (b.invMass <= 0.0f) continue;

		XMVECTOR pCheck = Load3(b.position);
		if (!IsVectorFinite3(pCheck)) continue;

		XMVECTOR p = Load3(b.position);
		XMVECTOR p0 = Load3(b.prevPosition);
		XMVECTOR vInt = Load3(b.linearVelocity);
		XMVECTOR vPos = XMVectorScale(XMVectorSubtract(p, p0), invDt);

		float ld = std::clamp(b.linearDamping, 0.0f, 1.0f);
		ld = std::max(ld, m_settings.minLinearDamping);

		float linScale = 1.0f;
		if (ld >= 1.0f) linScale = 0.0f;
		else
		{
			float oneMinus = std::max(1.0e-6f, 1.0f - ld);
			linScale = std::exp(std::log(oneMinus) * dt);
		}
		vInt = XMVectorScale(vInt, linScale);
		vPos = XMVectorScale(vPos, linScale);

		float a = std::clamp(m_settings.postSolveVelocityBlend, 0.0f, 1.0f);
		XMVECTOR v = XMVectorAdd(XMVectorScale(vInt, 1.0f - a), XMVectorScale(vPos, a));

		{
			float maxV = std::max(0.0f, m_settings.maxLinearSpeed);
			if (maxV > 0.0f)
			{
				float vLen = Length3(v);
				if (vLen > maxV) v = XMVectorScale(v, maxV / vLen);
			}
		}

		if (m_settings.sleepLinearSpeed > 0.0f)
		{
			float vLen = Length3(v);
			if (vLen < m_settings.sleepLinearSpeed) v = XMVectorZero();
		}

		Store3(b.linearVelocity, v);
		XMVECTOR q = Load4(b.rotation);
		XMVECTOR q0 = Load4(b.prevRotation);

		XMVECTOR wInt = Load3(b.angularVelocity);

		XMVECTOR dq = XMQuaternionMultiply(q, QuaternionConjugate(q0));
		XMVECTOR wPos = QuaternionDeltaToAngularVelocity(dq, dt);

		float ad = std::clamp(b.angularDamping, 0.0f, 1.0f);
		ad = std::max(ad, m_settings.minAngularDamping);

		float angScale = 1.0f;
		if (ad >= 1.0f) angScale = 0.0f;
		else
		{
			float oneMinus = std::max(1.0e-6f, 1.0f - ad);
			angScale = std::exp(std::log(oneMinus) * dt);
		}
		wInt = XMVectorScale(wInt, angScale);
		wPos = XMVectorScale(wPos, angScale);

		float aAng = std::clamp(m_settings.postSolveAngularVelocityBlend, 0.0f, 1.0f);
		XMVECTOR w = XMVectorAdd(XMVectorScale(wInt, 1.0f - aAng), XMVectorScale(wPos, aAng));

		{
			float maxW = std::max(0.0f, m_settings.maxAngularSpeed);
			if (maxW > 0.0f)
			{
				float wLen = Length3(w);
				if (wLen > maxW) w = XMVectorScale(w, maxW / wLen);
			}
		}

		if (m_settings.sleepAngularSpeed > 0.0f)
		{
			float wLen = Length3(w);
			if (wLen < m_settings.sleepAngularSpeed) w = XMVectorZero();
		}

		Store3(b.angularVelocity, w);
	}
}

void MmdPhysicsWorld::WriteBackBones(const PmxModel& model, BoneSolver& bones)
{
	const auto& rbDefs = model.RigidBodies();
	const auto& bonesDef = model.Bones();

	std::unordered_map<int, XMFLOAT4X4> desiredGlobals;
	desiredGlobals.reserve(m_bodies.size());

	std::unordered_set<int> keepTranslationBones;
	keepTranslationBones.reserve(m_bodies.size());

	// Detect whether there is any "AfterPhysics" driven bone. If none exists, fall back to a safe mode.
	bool anyAfterPhysics = false;
	if (m_settings.requireAfterPhysicsFlag)
	{
		for (size_t i = 0; i < rbDefs.size(); ++i)
		{
			const auto& def = rbDefs[i];
			if (def.operation == PmxModel::RigidBody::OperationType::Static) continue;
			if (def.boneIndex < 0 || def.boneIndex >= static_cast<int>(bonesDef.size())) continue;

			const Body& b = m_bodies[i];
			if (b.invMass <= 0.0f) continue;

			if (!bonesDef[def.boneIndex].IsAfterPhysics()) continue;

			const XMVECTOR pCheck = Load3(b.position);
			const XMVECTOR qCheck = Load4(b.rotation);
			if (!IsVectorFinite3(pCheck) || !IsVectorFinite4(qCheck)) continue;

			anyAfterPhysics = true;
			break;
		}
	}

	const bool fallbackNoAfterPhysics = (m_settings.requireAfterPhysicsFlag && !anyAfterPhysics);

	auto CollectBone = [&](size_t i)
		{
			// [FIX] Added boundary check for generated bodies
			if (i >= rbDefs.size()) return;

			const auto& def = rbDefs[i];
			if (def.operation == PmxModel::RigidBody::OperationType::Static) return;
			if (def.boneIndex < 0 || def.boneIndex >= static_cast<int>(bonesDef.size())) return;

			const Body& b = m_bodies[i];
			if (b.invMass <= 0.0f) return;

			const XMVECTOR pCheck = Load3(b.position);
			const XMVECTOR qCheck = Load4(b.rotation);
			if (!IsVectorFinite3(pCheck) || !IsVectorFinite4(qCheck)) return;

			if (m_settings.requireAfterPhysicsFlag && !fallbackNoAfterPhysics)
			{
				if (!bonesDef[def.boneIndex].IsAfterPhysics()) return;
			}
			else if (fallbackNoAfterPhysics && m_settings.writebackFallbackPositionAdjustOnly)
			{
				if (def.operation != PmxModel::RigidBody::OperationType::DynamicAndPositionAdjust) return;
			}

			if (def.operation == PmxModel::RigidBody::OperationType::DynamicAndPositionAdjust)
			{
				keepTranslationBones.insert(def.boneIndex);
			}

			const XMMATRIX rbG = MatrixFromTR(b.position, b.rotation);
			const XMMATRIX localFromBone = XMLoadFloat4x4(&b.localFromBone);
			const XMMATRIX invLocalFromBone = XMMatrixInverse(nullptr, localFromBone);
			const XMMATRIX boneG = rbG * invLocalFromBone;

			XMFLOAT4X4 g{};
			XMStoreFloat4x4(&g, boneG);
			desiredGlobals[def.boneIndex] = g;
		};

	for (size_t i = 0; i < m_bodies.size(); ++i) CollectBone(i);

	if (desiredGlobals.empty()) return;

	// Keep original local translations for "DynamicAndPositionAdjust" bones.
	std::vector<XMFLOAT3> originalLocalTranslation;
	if (!keepTranslationBones.empty())
	{
		originalLocalTranslation.resize(bonesDef.size());
		for (size_t bi = 0; bi < bonesDef.size(); ++bi)
		{
			const XMMATRIX lm = XMLoadFloat4x4(&bones.GetBoneLocalMatrix(bi));
			XMFLOAT3 t; XMFLOAT4 r;
			DecomposeTR(lm, t, r);
			originalLocalTranslation[bi] = t;
		}
	}

	// ------------------------------------------------------------
	// Build dependency graph (DAG) between bones that are written back.
	// Dependencies:
	//  1) Skeleton parent dependency (parent must be updated before child)
	//  2) Joint-connected dependency (stable ordering; shallower/ancestor first)
	// ------------------------------------------------------------

	std::vector<int> nodes;
	nodes.reserve(desiredGlobals.size());
	for (const auto& kv : desiredGlobals) nodes.push_back(kv.first);

	std::unordered_map<int, int> nodeId;
	nodeId.reserve(nodes.size() * 2);
	for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
	{
		nodeId[nodes[static_cast<size_t>(i)]] = i;
	}

	// Cache depths (rank) for deterministic ordering and joint edge direction.
	std::vector<int> depthCache(bonesDef.size(), -1);
	auto GetDepth = [&](int boneIndex) -> int
		{
			if (boneIndex < 0 || boneIndex >= static_cast<int>(bonesDef.size())) return 0;
			int& d = depthCache[static_cast<size_t>(boneIndex)];
			if (d >= 0) return d;

			int depth = 0;
			int c = boneIndex;
			int guard = 0;
			while (c >= 0 && c < static_cast<int>(bonesDef.size()) && guard++ < 1000)
			{
				c = bonesDef[static_cast<size_t>(c)].parentIndex;
				++depth;
			}
			d = depth;
			return d;
		};

	auto IsAncestor = [&](int anc, int node) -> bool
		{
			if (anc < 0 || node < 0) return false;
			int c = node;
			int guard = 0;
			while (c >= 0 && c < static_cast<int>(bonesDef.size()) && guard++ < 1000)
			{
				if (c == anc) return true;
				c = bonesDef[static_cast<size_t>(c)].parentIndex;
			}
			return false;
		};

	const int n = static_cast<int>(nodes.size());
	std::vector<std::vector<int>> outEdges(static_cast<size_t>(n));
	std::vector<int> indeg(static_cast<size_t>(n), 0);

	// Deduplicate edges (bone indices are stable and < 2^31).
	std::unordered_set<uint64_t> edgeSet;
	edgeSet.reserve(static_cast<size_t>(n) * 4);

	auto AddEdge = [&](int fromBone, int toBone)
		{
			if (fromBone == toBone) return;
			auto itU = nodeId.find(fromBone);
			auto itV = nodeId.find(toBone);
			if (itU == nodeId.end() || itV == nodeId.end()) return;

			const int u = itU->second;
			const int v = itV->second;

			const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(u)) << 32) | static_cast<uint32_t>(v);
			if (!edgeSet.insert(key).second) return;

			outEdges[static_cast<size_t>(u)].push_back(v);
			++indeg[static_cast<size_t>(v)];
		};

	// (1) Parent dependencies
	for (int boneIndex : nodes)
	{
		const int p = bonesDef[static_cast<size_t>(boneIndex)].parentIndex;
		if (p >= 0 && nodeId.count(p))
		{
			AddEdge(p, boneIndex);
		}
	}

	// (2) Joint-connected dependencies
	for (const auto& jc : m_joints)
	{
		const int a = jc.bodyA;
		const int b = jc.bodyB;
		if (a < 0 || b < 0) continue;
		if (a >= static_cast<int>(m_bodies.size())) continue;
		if (b >= static_cast<int>(m_bodies.size())) continue;

		const int boneA = m_bodies[static_cast<size_t>(a)].boneIndex;
		const int boneB = m_bodies[static_cast<size_t>(b)].boneIndex;
		if (boneA < 0 || boneB < 0) continue;
		if (!nodeId.count(boneA) || !nodeId.count(boneB)) continue;
		if (boneA == boneB) continue;

		int from = boneA;
		int to = boneB;

		// Prefer true ancestry direction when it exists.
		if (IsAncestor(boneA, boneB))
		{
			from = boneA; to = boneB;
		}
		else if (IsAncestor(boneB, boneA))
		{
			from = boneB; to = boneA;
		}
		else
		{
			// Otherwise choose a deterministic direction to keep the graph acyclic:
			// smaller (depth, index) -> larger (depth, index).
			const int dA = GetDepth(boneA);
			const int dB = GetDepth(boneB);
			if (dA < dB || (dA == dB && boneA < boneB))
			{
				from = boneA; to = boneB;
			}
			else
			{
				from = boneB; to = boneA;
			}
		}

		AddEdge(from, to);
	}

	// Kahn topological sort with deterministic tie-break (depth/index).
	struct ReadyItem
	{
		int depth;
		int bone;
		int id;
	};
	struct ReadyLess
	{
		bool operator()(const ReadyItem& a, const ReadyItem& b) const
		{
			if (a.depth != b.depth) return a.depth > b.depth; // min depth first
			return a.bone > b.bone; // min index first
		}
	};

	std::priority_queue<ReadyItem, std::vector<ReadyItem>, ReadyLess> ready;
	ready = {};

	for (int id = 0; id < n; ++id)
	{
		if (indeg[static_cast<size_t>(id)] == 0)
		{
			const int bone = nodes[static_cast<size_t>(id)];
			ready.push({ GetDepth(bone), bone, id });
		}
	}

	std::vector<int> topoOrder;
	topoOrder.reserve(nodes.size());

	std::vector<int> indegWork = indeg;
	while (!ready.empty())
	{
		const ReadyItem cur = ready.top();
		ready.pop();

		topoOrder.push_back(cur.bone);

		for (int v : outEdges[static_cast<size_t>(cur.id)])
		{
			int& deg = indegWork[static_cast<size_t>(v)];
			--deg;
			if (deg == 0)
			{
				const int bone = nodes[static_cast<size_t>(v)];
				ready.push({ GetDepth(bone), bone, v });
			}
		}
	}

	// Safety fallback: if any nodes remain (cycle), append by deterministic rank.
	if (topoOrder.size() != nodes.size())
	{
		std::vector<int> remaining;
		remaining.reserve(nodes.size() - topoOrder.size());

		std::vector<uint8_t> inOrder(bonesDef.size(), 0);
		for (int b : topoOrder)
		{
			if (b >= 0 && b < static_cast<int>(bonesDef.size())) inOrder[static_cast<size_t>(b)] = 1;
		}

		for (int b : nodes)
		{
			if (b >= 0 && b < static_cast<int>(bonesDef.size()) && !inOrder[static_cast<size_t>(b)])
			{
				remaining.push_back(b);
			}
		}

		std::sort(remaining.begin(), remaining.end(), [&](int a, int b)
				  {
					  const int da = GetDepth(a), db = GetDepth(b);
					  if (da != db) return da < db;
					  return a < b;
				  });

		topoOrder.insert(topoOrder.end(), remaining.begin(), remaining.end());
	}

	// ------------------------------------------------------------
	// Apply write-back in topological order and keep an "applied" global cache.
	// This avoids inconsistencies when we override local translation for
	// DynamicAndPositionAdjust bones: children will see the actual parent transform.
	// ------------------------------------------------------------

	std::unordered_map<int, XMFLOAT4X4> appliedGlobals;
	appliedGlobals.reserve(topoOrder.size() * 2);

	for (int boneIndex : topoOrder)
	{
		const auto& boneDef = bonesDef[static_cast<size_t>(boneIndex)];

		const XMMATRIX desiredG = XMLoadFloat4x4(&desiredGlobals[boneIndex]);

		// Validate matrix values (avoid propagating NaNs)
		XMFLOAT4X4 checkG;
		XMStoreFloat4x4(&checkG, desiredG);
		bool valid = true;
		for (int k = 0; k < 16; ++k)
		{
			if (!std::isfinite(checkG.m[k / 4][k % 4]))
			{
				valid = false; break;
			}
		}
		if (!valid) continue;

		XMMATRIX parentG = XMMatrixIdentity();
		if (boneDef.parentIndex >= 0)
		{
			auto it = appliedGlobals.find(boneDef.parentIndex);
			if (it != appliedGlobals.end())
			{
				parentG = XMLoadFloat4x4(&it->second);
			}
			else
			{
				parentG = XMLoadFloat4x4(&bones.GetBoneGlobalMatrix(boneDef.parentIndex));
			}

			const XMVECTOR rel = XMVectorSubtract(
				Load3(boneDef.position),
				Load3(bonesDef[static_cast<size_t>(boneDef.parentIndex)].position));
			parentG = XMMatrixTranslationFromVector(rel) * parentG;
		}
		else
		{
			parentG = XMMatrixTranslationFromVector(Load3(boneDef.position));
		}

		const XMMATRIX localMat = desiredG * XMMatrixInverse(nullptr, parentG);

		XMFLOAT3 t;
		XMFLOAT4 r;
		DecomposeTR(localMat, t, r);

		if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) continue;
		if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z) || !std::isfinite(r.w)) continue;

		if (!keepTranslationBones.empty() && keepTranslationBones.count(boneIndex))
		{
			t = originalLocalTranslation[static_cast<size_t>(boneIndex)];
		}

		bones.SetBoneLocalPose(static_cast<size_t>(boneIndex), t, r);

		// Cache the actually applied global transform for downstream bones.
		const XMMATRIX appliedLocal = MatrixFromTR(t, r);
		const XMMATRIX appliedG = appliedLocal * parentG;

		XMFLOAT4X4 appliedGF{};
		XMStoreFloat4x4(&appliedGF, appliedG);
		appliedGlobals[boneIndex] = appliedGF;
	}
}


XMVECTOR MmdPhysicsWorld::Load3(const XMFLOAT3& v)
{
	return XMLoadFloat3(&v);
}
XMVECTOR MmdPhysicsWorld::Load4(const XMFLOAT4& v)
{
	return XMLoadFloat4(&v);
}
void MmdPhysicsWorld::Store3(XMFLOAT3& o, XMVECTOR v)
{
	XMStoreFloat3(&o, v);
}
void MmdPhysicsWorld::Store4(XMFLOAT4& o, XMVECTOR v)
{
	XMStoreFloat4(&o, v);
}

XMMATRIX MmdPhysicsWorld::MatrixFromTR(const XMFLOAT3& t, const XMFLOAT4& r)
{
	return XMMatrixRotationQuaternion(XMLoadFloat4(&r)) * XMMatrixTranslationFromVector(XMLoadFloat3(&t));
}
void MmdPhysicsWorld::DecomposeTR(const XMMATRIX& m, XMFLOAT3& outT, XMFLOAT4& outR)
{
	XMVECTOR s, r, t;
	XMMatrixDecompose(&s, &r, &t, m);
	XMStoreFloat3(&outT, t);
	XMStoreFloat4(&outR, XMQuaternionNormalize(r));
}
DirectX::XMFLOAT3 MmdPhysicsWorld::ExtractTranslation(const DirectX::XMMATRIX& m)
{
	XMFLOAT3 t; XMStoreFloat3(&t, m.r[3]); return t;
}
float MmdPhysicsWorld::ComputeDepth(const std::vector<PmxModel::Bone>& bones, int boneIndex)
{
	float d = 0; int c = boneIndex; int g = 0;
	while (c >= 0 && c < (int)bones.size() && g++ < 1000)
	{
		c = bones[c].parentIndex; d++;
	}
	return d;
}