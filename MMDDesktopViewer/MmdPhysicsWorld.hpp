#pragma once

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <DirectXMath.h>
#include "PmxModel.hpp"
#include "BoneSolver.hpp"

class MmdPhysicsWorld
{
public:
	struct Settings
	{
		float fixedTimeStep{ 1.0f / 60.0f };

		int maxSubSteps{ 2 };
		int maxCatchUpSteps{ 4 };

		DirectX::XMFLOAT3 gravity{ 0.0f, -9.8f, 0.0f };
		float groundY{ -1000.0f };

		// [FIX] 1.0e-5f -> 0.0f
		// ここを0.0fにすることで、サブステップ数に関わらず「絶対に伸びない関節」になります。
		// これがスカートの垂れ下がりと、それに伴う浮遊バグを解決します。
		float jointCompliance{ 0.0f };

		// 衝突の柔らかさは維持 (爆発防止)
		float contactCompliance{ 0.001f };

		// [FIX] 0.5f -> 0.0f
		// 余計な力を持ち越さないようにリセットします。
		float jointWarmStart{ 0.0f };

		// 揺れ抑制のため 0.0f を維持
		float postSolveVelocityBlend{ 0.0f };
		float postSolveAngularVelocityBlend{ 0.0f };

		float maxContactAngularCorrection{ 0.02f };

		bool enableRigidBodyCollisions{ true };

		int collisionGroupMaskSemantics{ 0 };
		bool collideJointConnectedBodies{ false };
		bool respectCollisionGroups{ true };
		bool requireAfterPhysicsFlag{ true };

		// Auto-generate kinematic body colliders (capsules) from the skeleton when the model has
		// no bone-attached static rigid bodies (i.e. no collision bodies for the character itself).
		bool generateBodyCollidersIfMissing{ true };
		int minExistingBodyColliders{ 1 };
		int maxGeneratedBodyColliders{ 200 };

		float generatedBodyColliderMinBoneLength{ 0.04f };
		float generatedBodyColliderRadiusRatio{ 0.18f };
		float generatedBodyColliderMinRadius{ 0.5f };
		float generatedBodyColliderMaxRadius{ 10.0f };

		// Bones far away from the skeleton centroid are treated as accessories and ignored.
		float generatedBodyColliderOutlierDistanceFactor{ 1.8f };

		float generatedBodyColliderFriction{ 0.6f };
		float generatedBodyColliderRestitution{ 0.0f };

		int solverIterations{ 4 };
		int collisionIterations{ 4 };

		float collisionMargin{ 0.005f };

		// Extra margin applied only in mixed (static vs dynamic) pairs. Default 0 to avoid perpetual contact.
		float phantomMargin{ 0.0f };

		// Penetration slop: small overlaps are ignored (helps eliminate idle jitter).
		float contactSlop{ 0.001f };

		// If requireAfterPhysicsFlag==true but the model has no AfterPhysics bones,
		// fallback write-back mode:
		//  - true  : only rigid bodies with OperationType::DynamicAndPositionAdjust drive bones (safe default)
		//  - false : allow all non-static dynamic bodies to drive bones (legacy behavior; may move whole model)
		bool writebackFallbackPositionAdjustOnly{ true };

		float collisionRadiusScale{ 1.0f };

		float maxLinearSpeed{ 100.0f };
		float maxAngularSpeed{ 40.0f };

		float maxJointPositionCorrection{ 1.0f };
		float maxJointAngularCorrection{ 0.15f };

		// 爆発防止のため 2.0f を維持
		float maxDepenetrationVelocity{ 2.0f };

		float maxSpringCorrectionRate{ 0.4f };

		// [FIX] 0.8f -> 0.4f
		// 関節が伸びなくなったので、バネ係数は少し下げて「しなやかさ」を出します。
		// 動きが硬いと感じる場合は、ここを下げてください (0.2~0.4推奨)。
		float springStiffnessScale{ 0.2f };

		float minLinearDamping{ 0.2f };
		float minAngularDamping{ 0.2f };

		float maxInvInertia{ 1.0f };

		float sleepLinearSpeed{ 0.0f };
		float sleepAngularSpeed{ 0.0f };

		float maxInvMass{ 0.0f };
	};

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