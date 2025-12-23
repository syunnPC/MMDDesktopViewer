#pragma once

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <DirectXMath.h>
#include "PmxModel.hpp"
#include "BoneSolver.hpp"
#include "Settings.hpp"

class MmdPhysicsWorld
{
public:
	using Settings = PhysicsSettings;

	MmdPhysicsWorld() = default;

	void Reset();

	void BuildFromModel(const PmxModel& model, const BoneSolver& bones);
	void Step(double dtSeconds, const PmxModel& model, BoneSolver& bones);

	bool IsBuilt() const
	{
		return m_isBuilt;
	}
	uint64_t BuiltRevision() const
	{
		return m_builtRevision;
	}
	Settings& GetSettings()
	{
		return m_settings;
	}
	const Settings& GetSettings() const
	{
		return m_settings;
	}

private:
	struct Body
	{
		int defIndex{ -1 };
		int boneIndex{ -1 };
		PmxModel::RigidBody::OperationType operation{ PmxModel::RigidBody::OperationType::Static };

		DirectX::XMFLOAT4X4 localFromBone{};

		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };

		DirectX::XMFLOAT3 prevPosition{};
		DirectX::XMFLOAT4 prevRotation{ 0.0f, 0.0f, 0.0f, 1.0f };

		DirectX::XMFLOAT3 kinematicTargetPos{};
		DirectX::XMFLOAT4 kinematicTargetRot{};
		DirectX::XMFLOAT3 kinematicStartPos{};
		DirectX::XMFLOAT4 kinematicStartRot{};

		DirectX::XMFLOAT3 linearVelocity{};
		DirectX::XMFLOAT3 angularVelocity{};

		float invMass{ 0.0f };
		DirectX::XMFLOAT3 invInertia{ 0.0f, 0.0f, 0.0f };

		PmxModel::RigidBody::ShapeType shapeType{ PmxModel::RigidBody::ShapeType::Sphere };
		DirectX::XMFLOAT3 shapeSize{};
		float capsuleRadius{ 0.0f };
		float capsuleHalfHeight{ 0.0f };


		// Capsule axis in local space (Box uses longest axis)
		DirectX::XMFLOAT3 capsuleLocalAxis{ 0.0f, 1.0f, 0.0f };
		int group{ 0 };
		uint16_t groupMask{ 0 };
		float friction{ 0.5f };
		float restitution{ 0.0f };

		float linearDamping{ 0.0f };
		float angularDamping{ 0.0f };
	};

	struct JointConstraint
	{
		int bodyA{ -1 };
		int bodyB{ -1 };

		DirectX::XMFLOAT3 localAnchorA{};
		DirectX::XMFLOAT3 localAnchorB{};

		DirectX::XMFLOAT4 rotAtoJ{};
		DirectX::XMFLOAT4 rotBtoJ{};

		DirectX::XMFLOAT3 posLower{};
		DirectX::XMFLOAT3 posUpper{};
		DirectX::XMFLOAT3 rotLower{};
		DirectX::XMFLOAT3 rotUpper{};

		DirectX::XMFLOAT3 positionSpring{};
		DirectX::XMFLOAT3 rotationSpring{};

		float lambdaPos{ 0.0f };
	};

	void BuildConstraints(const PmxModel& model);
	bool IsJointConnected(uint32_t a, uint32_t b) const;

	void PrecomputeKinematicTargets(const PmxModel& model, const BoneSolver& bones);
	void InterpolateKinematicBodies(float t);

	void BeginSubStep();
	void Integrate(float dt, const PmxModel& model);
	void SolveXPBD(float dt, const PmxModel& model);
	void SolveBodyCollisions(float dt);
	void SolveGround(float dt, const PmxModel& model);
	void SolveJoints(float dt);
	void EndSubStep(float dt, const PmxModel& model);

	void WriteBackBones(const PmxModel& model, BoneSolver& bones);

	static DirectX::XMVECTOR Load3(const DirectX::XMFLOAT3& v);
	static DirectX::XMVECTOR Load4(const DirectX::XMFLOAT4& v);
	static void Store3(DirectX::XMFLOAT3& o, DirectX::XMVECTOR v);
	static void Store4(DirectX::XMFLOAT4& o, DirectX::XMVECTOR v);
	static DirectX::XMMATRIX MatrixFromTR(const DirectX::XMFLOAT3& t, const DirectX::XMFLOAT4& r);
	static void DecomposeTR(const DirectX::XMMATRIX& m, DirectX::XMFLOAT3& outT, DirectX::XMFLOAT4& outR);
	static float ComputeDepth(const std::vector<PmxModel::Bone>& bones, int boneIndex);
	DirectX::XMFLOAT3 ExtractTranslation(const DirectX::XMMATRIX& m);

	Settings m_settings{};

	bool m_isBuilt{ false };
	uint64_t m_builtRevision{ 0 };
	double m_accumulator{ 0.0 };

	std::vector<Body> m_bodies;
	std::vector<JointConstraint> m_joints;
	std::vector<std::vector<uint32_t>> m_jointAdjacency;

	bool m_groupIndexIsOneBased{ false };
	bool m_groupMaskIsCollisionMask{ true };

	bool m_anyKinematicMovedThisTick{ false };
	int  m_sleepCounter{ 0 };
	bool m_worldSleeping{ false };

	bool ShouldSkipPhysicsTick();

	struct CollisionShapeCache
	{
		DirectX::XMVECTOR p0;
		DirectX::XMVECTOR p1;
		DirectX::XMVECTOR rotation;
		float radius;
		float ex, ey, ez;
		bool isBox;
	};
	std::vector<CollisionShapeCache> m_shapeCache;

	// 物理演算用ワークバッファ（毎回確保しないようにメンバ化）

	struct SapPair
	{
		int a; int b;
	};
	std::vector<SapPair> m_candidates;

	// SAP用バッファ
	struct SapNode
	{
		float minX; int index;
	};
	std::vector<SapNode> m_axisList;
	std::vector<float> m_radii;
	std::vector<float> m_maxXs;
};