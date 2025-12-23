#pragma once
#include <filesystem>
#include <string>
#include <map>
#include <DirectXMath.h>

enum class PresetMode
{
	Ask = 0,        // 毎回確認
	AlwaysLoad = 1, // 常に読み込む
	NeverLoad = 2   // 読み込まない
};

struct LightSettings
{
	float brightness = 1.5f;
	float ambientStrength = 0.55f;

	float globalSaturation = 1.1f;

	// Key light
	float keyLightDirX = 0.25f;
	float keyLightDirY = 0.85f;
	float keyLightDirZ = -0.5f;
	float keyLightColorR = 1.0f;
	float keyLightColorG = 1.0f;
	float keyLightColorB = 1.0f;
	float keyLightIntensity = 1.6f;

	// Fill light
	float fillLightDirX = -0.65f;
	float fillLightDirY = 0.25f;
	float fillLightDirZ = -0.15f;
	float fillLightColorR = 1.0f;
	float fillLightColorG = 1.0f;
	float fillLightColorB = 1.0f;
	float fillLightIntensity = 0.65f;

	// Model scale
	float modelScale = 1.0f;

	// Stylized toon shading controls
	bool toonEnabled = true;
	float toonContrast = 1.15f;
	float shadowHueShiftDeg = -8.0f;
	float shadowSaturationBoost = 0.25f;
	float rimWidth = 0.6f;
	float rimIntensity = 0.35f;
	float specularStep = 0.3f;

	float shadowRampShift = 0.0f;

	float shadowDeepThreshold = 0.28f;
	float shadowDeepSoftness = 0.03f;
	float shadowDeepMul = 0.65f;

	float faceShadowMul = 0.0f;       // 顔の影の濃さ（0.0で完全に影なし）
	float faceToonContrastMul = 0.9f; // 顔のトゥーン境界の柔らかさ調整
};

struct PhysicsSettings
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

struct AppSettings
{
	std::filesystem::path modelPath;
	bool alwaysOnTop{ true };
	int targetFps{ 60 };
	bool unlimitedFps{ false };

	// 追加: ウィンドウサイズ
	int windowWidth{ 0 };
	int windowHeight{ 0 };

	// プリセット関連設定
	PresetMode globalPresetMode{ PresetMode::Ask };
	std::map<std::wstring, PresetMode> perModelPresetSettings;

	LightSettings light;
	PhysicsSettings physics;
};

class SettingsManager
{
public:
	static AppSettings Load(const std::filesystem::path& baseDir,
							const std::filesystem::path& defaultModelPath);

	static void Save(const std::filesystem::path& baseDir,
					 const AppSettings& settings);

	// 指定したモデル用のプリセットが存在するか確認
	static bool HasPreset(const std::filesystem::path& baseDir, const std::filesystem::path& modelPath);

	// 指定したモデル用のプリセットを保存 (LightSettings / PhysicsSettings)
	static void SavePreset(const std::filesystem::path& baseDir,
						   const std::filesystem::path& modelPath,
						   const LightSettings& lightSettings,
						   const PhysicsSettings& physicsSettings);

	// 指定したモデル用のプリセットを読み込み (存在しなければ false)
	static bool LoadPreset(const std::filesystem::path& baseDir,
						   const std::filesystem::path& modelPath,
						   LightSettings& outLightSettings,
						   PhysicsSettings& outPhysicsSettings);
};