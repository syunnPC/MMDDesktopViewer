#pragma once
#include <filesystem>
#include <string>
#include <map>

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

struct AppSettings
{
	std::filesystem::path modelPath;
	bool alwaysOnTop{ true };
	int targetFps{ 60 };
	bool unlimitedFps{ false };

	// プリセット関連設定
	PresetMode globalPresetMode{ PresetMode::Ask };
	std::map<std::wstring, PresetMode> perModelPresetSettings; 

	LightSettings light;
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

	// 指定したモデル用のプリセットを保存 (LightSettingsのみ)
	static void SavePreset(const std::filesystem::path& baseDir, const std::filesystem::path& modelPath, const LightSettings& settings);

	// 指定したモデル用のプリセットを読み込み (存在しなければ false)
	static bool LoadPreset(const std::filesystem::path& baseDir, const std::filesystem::path& modelPath, LightSettings& outSettings);
};