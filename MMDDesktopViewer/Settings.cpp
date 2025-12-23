#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "Settings.hpp"

#include <codecvt>
#include <fstream>
#include <locale>
#include <system_error>
#include <sstream>
#include <cwchar>

namespace
{
	constexpr wchar_t kSettingsFileName[] = L"settings.ini";
	constexpr wchar_t kPresetsDirName[] = L"Presets";

	std::wstring Trim(const std::wstring& s)
	{
		const auto first = s.find_first_not_of(L" \t\r\n");
		if (first == std::wstring::npos) return L"";
		const auto last = s.find_last_not_of(L" \t\r\n");
		return s.substr(first, last - first + 1);
	}

	void EnsureUtf8Locale(std::basic_ios<wchar_t>& s)
	{
		s.imbue(std::locale(std::locale::classic(), new std::codecvt_utf8<wchar_t>()));
	}

	std::filesystem::path SettingsPath(const std::filesystem::path& baseDir)
	{
		return baseDir / kSettingsFileName;
	}

	std::filesystem::path GetPresetPath(const std::filesystem::path& baseDir, const std::filesystem::path& modelPath)
	{
		if (modelPath.empty()) return {};
		auto presetsDir = baseDir / kPresetsDirName;
		if (!std::filesystem::exists(presetsDir))
		{
			std::error_code ec;
			std::filesystem::create_directory(presetsDir, ec);
		}
		auto filename = modelPath.filename().wstring() + L".ini";
		return presetsDir / filename;
	}

	float ParseFloat(const std::wstring& s, float defaultVal)
	{
		try
		{
			return std::stof(s);
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	std::wstring FloatToWString(float v)
	{
		std::wostringstream oss;
		oss << v;
		return oss.str();
	}

	int ParseInt(const std::wstring& s, int defaultVal)
	{
		try
		{
			return std::stoi(s);
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	std::wstring IntToWString(int v)
	{
		std::wostringstream oss;
		oss << v;
		return oss.str();
	}

	void ParseLightSettingLine(const std::wstring& key, const std::wstring& value, LightSettings& light)
	{
		if (key == L"brightness") light.brightness = ParseFloat(value, light.brightness);
		else if (key == L"ambientStrength") light.ambientStrength = ParseFloat(value, light.ambientStrength);
		else if (key == L"globalSaturation") light.globalSaturation = ParseFloat(value, light.globalSaturation);
		else if (key == L"keyLightDirX") light.keyLightDirX = ParseFloat(value, light.keyLightDirX);
		else if (key == L"keyLightDirY") light.keyLightDirY = ParseFloat(value, light.keyLightDirY);
		else if (key == L"keyLightDirZ") light.keyLightDirZ = ParseFloat(value, light.keyLightDirZ);
		else if (key == L"keyLightColorR") light.keyLightColorR = ParseFloat(value, light.keyLightColorR);
		else if (key == L"keyLightColorG") light.keyLightColorG = ParseFloat(value, light.keyLightColorG);
		else if (key == L"keyLightColorB") light.keyLightColorB = ParseFloat(value, light.keyLightColorB);
		else if (key == L"keyLightIntensity") light.keyLightIntensity = ParseFloat(value, light.keyLightIntensity);
		else if (key == L"fillLightDirX") light.fillLightDirX = ParseFloat(value, light.fillLightDirX);
		else if (key == L"fillLightDirY") light.fillLightDirY = ParseFloat(value, light.fillLightDirY);
		else if (key == L"fillLightDirZ") light.fillLightDirZ = ParseFloat(value, light.fillLightDirZ);
		else if (key == L"fillLightColorR") light.fillLightColorR = ParseFloat(value, light.fillLightColorR);
		else if (key == L"fillLightColorG") light.fillLightColorG = ParseFloat(value, light.fillLightColorG);
		else if (key == L"fillLightColorB") light.fillLightColorB = ParseFloat(value, light.fillLightColorB);
		else if (key == L"fillLightIntensity") light.fillLightIntensity = ParseFloat(value, light.fillLightIntensity);
		else if (key == L"modelScale") light.modelScale = ParseFloat(value, light.modelScale);
		else if (key == L"toonEnabled") light.toonEnabled = (value == L"1" || value == L"true" || value == L"True");
		else if (key == L"toonContrast") light.toonContrast = ParseFloat(value, light.toonContrast);
		else if (key == L"shadowHueShiftDeg") light.shadowHueShiftDeg = ParseFloat(value, light.shadowHueShiftDeg);
		else if (key == L"shadowSaturationBoost") light.shadowSaturationBoost = ParseFloat(value, light.shadowSaturationBoost);
		else if (key == L"shadowRampShift") light.shadowRampShift = ParseFloat(value, light.shadowRampShift);
		else if (key == L"rimWidth") light.rimWidth = ParseFloat(value, light.rimWidth);
		else if (key == L"rimIntensity") light.rimIntensity = ParseFloat(value, light.rimIntensity);
		else if (key == L"specularStep") light.specularStep = ParseFloat(value, light.specularStep);
		else if (key == L"shadowDeepThreshold") light.shadowDeepThreshold = ParseFloat(value, light.shadowDeepThreshold);
		else if (key == L"shadowDeepSoftness") light.shadowDeepSoftness = ParseFloat(value, light.shadowDeepSoftness);
		else if (key == L"shadowDeepMul") light.shadowDeepMul = ParseFloat(value, light.shadowDeepMul);
		else if (key == L"faceShadowMul") light.faceShadowMul = ParseFloat(value, light.faceShadowMul);
		else if (key == L"faceToonContrastMul") light.faceToonContrastMul = ParseFloat(value, light.faceToonContrastMul);
	}

	void WriteLightSettings(std::wostream& os, const LightSettings& light)
	{
		os << L"brightness=" << FloatToWString(light.brightness) << L"\n";
		os << L"ambientStrength=" << FloatToWString(light.ambientStrength) << L"\n";
		os << L"globalSaturation=" << FloatToWString(light.globalSaturation) << L"\n";
		os << L"keyLightDirX=" << FloatToWString(light.keyLightDirX) << L"\n";
		os << L"keyLightDirY=" << FloatToWString(light.keyLightDirY) << L"\n";
		os << L"keyLightDirZ=" << FloatToWString(light.keyLightDirZ) << L"\n";
		os << L"keyLightColorR=" << FloatToWString(light.keyLightColorR) << L"\n";
		os << L"keyLightColorG=" << FloatToWString(light.keyLightColorG) << L"\n";
		os << L"keyLightColorB=" << FloatToWString(light.keyLightColorB) << L"\n";
		os << L"keyLightIntensity=" << FloatToWString(light.keyLightIntensity) << L"\n";
		os << L"fillLightDirX=" << FloatToWString(light.fillLightDirX) << L"\n";
		os << L"fillLightDirY=" << FloatToWString(light.fillLightDirY) << L"\n";
		os << L"fillLightDirZ=" << FloatToWString(light.fillLightDirZ) << L"\n";
		os << L"fillLightColorR=" << FloatToWString(light.fillLightColorR) << L"\n";
		os << L"fillLightColorG=" << FloatToWString(light.fillLightColorG) << L"\n";
		os << L"fillLightColorB=" << FloatToWString(light.fillLightColorB) << L"\n";
		os << L"fillLightIntensity=" << FloatToWString(light.fillLightIntensity) << L"\n";
		os << L"modelScale=" << FloatToWString(light.modelScale) << L"\n";
		os << L"toonEnabled=" << (light.toonEnabled ? L"1" : L"0") << L"\n";
		os << L"toonContrast=" << FloatToWString(light.toonContrast) << L"\n";
		os << L"shadowHueShiftDeg=" << FloatToWString(light.shadowHueShiftDeg) << L"\n";
		os << L"shadowSaturationBoost=" << FloatToWString(light.shadowSaturationBoost) << L"\n";
		os << L"shadowRampShift=" << FloatToWString(light.shadowRampShift) << L"\n";
		os << L"rimWidth=" << FloatToWString(light.rimWidth) << L"\n";
		os << L"rimIntensity=" << FloatToWString(light.rimIntensity) << L"\n";
		os << L"specularStep=" << FloatToWString(light.specularStep) << L"\n";
		os << L"shadowDeepThreshold=" << FloatToWString(light.shadowDeepThreshold) << L"\n";
		os << L"shadowDeepSoftness=" << FloatToWString(light.shadowDeepSoftness) << L"\n";
		os << L"shadowDeepMul=" << FloatToWString(light.shadowDeepMul) << L"\n";
		os << L"faceShadowMul=" << FloatToWString(light.faceShadowMul) << L"\n";
		os << L"faceToonContrastMul=" << FloatToWString(light.faceToonContrastMul) << L"\n";
	}
}

bool ParsePhysicsSettingLine(const std::wstring& key, const std::wstring& value, PhysicsSettings& physics)
{
	constexpr wchar_t kPrefix[] = L"physics.";
	if (key.rfind(kPrefix, 0) != 0)
	{
		return false;
	}

	const size_t prefixLen = wcslen(kPrefix);
	const std::wstring subKey = key.substr(prefixLen);

	if (subKey == L"fixedTimeStep") physics.fixedTimeStep = ParseFloat(value, physics.fixedTimeStep);
	else if (subKey == L"maxSubSteps") physics.maxSubSteps = ParseInt(value, physics.maxSubSteps);
	else if (subKey == L"maxCatchUpSteps") physics.maxCatchUpSteps = ParseInt(value, physics.maxCatchUpSteps);
	else if (subKey == L"gravityX") physics.gravity.x = ParseFloat(value, physics.gravity.x);
	else if (subKey == L"gravityY") physics.gravity.y = ParseFloat(value, physics.gravity.y);
	else if (subKey == L"gravityZ") physics.gravity.z = ParseFloat(value, physics.gravity.z);
	else if (subKey == L"groundY") physics.groundY = ParseFloat(value, physics.groundY);
	else if (subKey == L"jointCompliance") physics.jointCompliance = ParseFloat(value, physics.jointCompliance);
	else if (subKey == L"contactCompliance") physics.contactCompliance = ParseFloat(value, physics.contactCompliance);
	else if (subKey == L"jointWarmStart") physics.jointWarmStart = ParseFloat(value, physics.jointWarmStart);
	else if (subKey == L"postSolveVelocityBlend") physics.postSolveVelocityBlend = ParseFloat(value, physics.postSolveVelocityBlend);
	else if (subKey == L"postSolveAngularVelocityBlend") physics.postSolveAngularVelocityBlend = ParseFloat(value, physics.postSolveAngularVelocityBlend);
	else if (subKey == L"maxContactAngularCorrection") physics.maxContactAngularCorrection = ParseFloat(value, physics.maxContactAngularCorrection);
	else if (subKey == L"enableRigidBodyCollisions") physics.enableRigidBodyCollisions = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"collisionGroupMaskSemantics") physics.collisionGroupMaskSemantics = ParseInt(value, physics.collisionGroupMaskSemantics);
	else if (subKey == L"collideJointConnectedBodies") physics.collideJointConnectedBodies = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"respectCollisionGroups") physics.respectCollisionGroups = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"requireAfterPhysicsFlag") physics.requireAfterPhysicsFlag = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"generateBodyCollidersIfMissing") physics.generateBodyCollidersIfMissing = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"minExistingBodyColliders") physics.minExistingBodyColliders = ParseInt(value, physics.minExistingBodyColliders);
	else if (subKey == L"maxGeneratedBodyColliders") physics.maxGeneratedBodyColliders = ParseInt(value, physics.maxGeneratedBodyColliders);
	else if (subKey == L"generatedBodyColliderMinBoneLength") physics.generatedBodyColliderMinBoneLength = ParseFloat(value, physics.generatedBodyColliderMinBoneLength);
	else if (subKey == L"generatedBodyColliderRadiusRatio") physics.generatedBodyColliderRadiusRatio = ParseFloat(value, physics.generatedBodyColliderRadiusRatio);
	else if (subKey == L"generatedBodyColliderMinRadius") physics.generatedBodyColliderMinRadius = ParseFloat(value, physics.generatedBodyColliderMinRadius);
	else if (subKey == L"generatedBodyColliderMaxRadius") physics.generatedBodyColliderMaxRadius = ParseFloat(value, physics.generatedBodyColliderMaxRadius);
	else if (subKey == L"generatedBodyColliderOutlierDistanceFactor") physics.generatedBodyColliderOutlierDistanceFactor = ParseFloat(value, physics.generatedBodyColliderOutlierDistanceFactor);
	else if (subKey == L"generatedBodyColliderFriction") physics.generatedBodyColliderFriction = ParseFloat(value, physics.generatedBodyColliderFriction);
	else if (subKey == L"generatedBodyColliderRestitution") physics.generatedBodyColliderRestitution = ParseFloat(value, physics.generatedBodyColliderRestitution);
	else if (subKey == L"solverIterations") physics.solverIterations = ParseInt(value, physics.solverIterations);
	else if (subKey == L"collisionIterations") physics.collisionIterations = ParseInt(value, physics.collisionIterations);
	else if (subKey == L"collisionMargin") physics.collisionMargin = ParseFloat(value, physics.collisionMargin);
	else if (subKey == L"phantomMargin") physics.phantomMargin = ParseFloat(value, physics.phantomMargin);
	else if (subKey == L"contactSlop") physics.contactSlop = ParseFloat(value, physics.contactSlop);
	else if (subKey == L"writebackFallbackPositionAdjustOnly") physics.writebackFallbackPositionAdjustOnly = (value == L"1" || value == L"true" || value == L"True");
	else if (subKey == L"collisionRadiusScale") physics.collisionRadiusScale = ParseFloat(value, physics.collisionRadiusScale);
	else if (subKey == L"maxLinearSpeed") physics.maxLinearSpeed = ParseFloat(value, physics.maxLinearSpeed);
	else if (subKey == L"maxAngularSpeed") physics.maxAngularSpeed = ParseFloat(value, physics.maxAngularSpeed);
	else if (subKey == L"maxJointPositionCorrection") physics.maxJointPositionCorrection = ParseFloat(value, physics.maxJointPositionCorrection);
	else if (subKey == L"maxJointAngularCorrection") physics.maxJointAngularCorrection = ParseFloat(value, physics.maxJointAngularCorrection);
	else if (subKey == L"maxDepenetrationVelocity") physics.maxDepenetrationVelocity = ParseFloat(value, physics.maxDepenetrationVelocity);
	else if (subKey == L"maxSpringCorrectionRate") physics.maxSpringCorrectionRate = ParseFloat(value, physics.maxSpringCorrectionRate);
	else if (subKey == L"springStiffnessScale") physics.springStiffnessScale = ParseFloat(value, physics.springStiffnessScale);
	else if (subKey == L"minLinearDamping") physics.minLinearDamping = ParseFloat(value, physics.minLinearDamping);
	else if (subKey == L"minAngularDamping") physics.minAngularDamping = ParseFloat(value, physics.minAngularDamping);
	else if (subKey == L"maxInvInertia") physics.maxInvInertia = ParseFloat(value, physics.maxInvInertia);
	else if (subKey == L"sleepLinearSpeed") physics.sleepLinearSpeed = ParseFloat(value, physics.sleepLinearSpeed);
	else if (subKey == L"sleepAngularSpeed") physics.sleepAngularSpeed = ParseFloat(value, physics.sleepAngularSpeed);
	else if (subKey == L"maxInvMass") physics.maxInvMass = ParseFloat(value, physics.maxInvMass);
	else return false;

	return true;
}

void WritePhysicsSettings(std::wostream& os, const PhysicsSettings& physics)
{
	constexpr wchar_t kPrefix[] = L"physics.";
	os << kPrefix << L"fixedTimeStep=" << FloatToWString(physics.fixedTimeStep) << L"\n";
	os << kPrefix << L"maxSubSteps=" << IntToWString(physics.maxSubSteps) << L"\n";
	os << kPrefix << L"maxCatchUpSteps=" << IntToWString(physics.maxCatchUpSteps) << L"\n";
	os << kPrefix << L"gravityX=" << FloatToWString(physics.gravity.x) << L"\n";
	os << kPrefix << L"gravityY=" << FloatToWString(physics.gravity.y) << L"\n";
	os << kPrefix << L"gravityZ=" << FloatToWString(physics.gravity.z) << L"\n";
	os << kPrefix << L"groundY=" << FloatToWString(physics.groundY) << L"\n";
	os << kPrefix << L"jointCompliance=" << FloatToWString(physics.jointCompliance) << L"\n";
	os << kPrefix << L"contactCompliance=" << FloatToWString(physics.contactCompliance) << L"\n";
	os << kPrefix << L"jointWarmStart=" << FloatToWString(physics.jointWarmStart) << L"\n";
	os << kPrefix << L"postSolveVelocityBlend=" << FloatToWString(physics.postSolveVelocityBlend) << L"\n";
	os << kPrefix << L"postSolveAngularVelocityBlend=" << FloatToWString(physics.postSolveAngularVelocityBlend) << L"\n";
	os << kPrefix << L"maxContactAngularCorrection=" << FloatToWString(physics.maxContactAngularCorrection) << L"\n";
	os << kPrefix << L"enableRigidBodyCollisions=" << (physics.enableRigidBodyCollisions ? L"1" : L"0") << L"\n";
	os << kPrefix << L"collisionGroupMaskSemantics=" << IntToWString(physics.collisionGroupMaskSemantics) << L"\n";
	os << kPrefix << L"collideJointConnectedBodies=" << (physics.collideJointConnectedBodies ? L"1" : L"0") << L"\n";
	os << kPrefix << L"respectCollisionGroups=" << (physics.respectCollisionGroups ? L"1" : L"0") << L"\n";
	os << kPrefix << L"requireAfterPhysicsFlag=" << (physics.requireAfterPhysicsFlag ? L"1" : L"0") << L"\n";
	os << kPrefix << L"generateBodyCollidersIfMissing=" << (physics.generateBodyCollidersIfMissing ? L"1" : L"0") << L"\n";
	os << kPrefix << L"minExistingBodyColliders=" << IntToWString(physics.minExistingBodyColliders) << L"\n";
	os << kPrefix << L"maxGeneratedBodyColliders=" << IntToWString(physics.maxGeneratedBodyColliders) << L"\n";
	os << kPrefix << L"generatedBodyColliderMinBoneLength=" << FloatToWString(physics.generatedBodyColliderMinBoneLength) << L"\n";
	os << kPrefix << L"generatedBodyColliderRadiusRatio=" << FloatToWString(physics.generatedBodyColliderRadiusRatio) << L"\n";
	os << kPrefix << L"generatedBodyColliderMinRadius=" << FloatToWString(physics.generatedBodyColliderMinRadius) << L"\n";
	os << kPrefix << L"generatedBodyColliderMaxRadius=" << FloatToWString(physics.generatedBodyColliderMaxRadius) << L"\n";
	os << kPrefix << L"generatedBodyColliderOutlierDistanceFactor=" << FloatToWString(physics.generatedBodyColliderOutlierDistanceFactor) << L"\n";
	os << kPrefix << L"generatedBodyColliderFriction=" << FloatToWString(physics.generatedBodyColliderFriction) << L"\n";
	os << kPrefix << L"generatedBodyColliderRestitution=" << FloatToWString(physics.generatedBodyColliderRestitution) << L"\n";
	os << kPrefix << L"solverIterations=" << IntToWString(physics.solverIterations) << L"\n";
	os << kPrefix << L"collisionIterations=" << IntToWString(physics.collisionIterations) << L"\n";
	os << kPrefix << L"collisionMargin=" << FloatToWString(physics.collisionMargin) << L"\n";
	os << kPrefix << L"phantomMargin=" << FloatToWString(physics.phantomMargin) << L"\n";
	os << kPrefix << L"contactSlop=" << FloatToWString(physics.contactSlop) << L"\n";
	os << kPrefix << L"writebackFallbackPositionAdjustOnly=" << (physics.writebackFallbackPositionAdjustOnly ? L"1" : L"0") << L"\n";
	os << kPrefix << L"collisionRadiusScale=" << FloatToWString(physics.collisionRadiusScale) << L"\n";
	os << kPrefix << L"maxLinearSpeed=" << FloatToWString(physics.maxLinearSpeed) << L"\n";
	os << kPrefix << L"maxAngularSpeed=" << FloatToWString(physics.maxAngularSpeed) << L"\n";
	os << kPrefix << L"maxJointPositionCorrection=" << FloatToWString(physics.maxJointPositionCorrection) << L"\n";
	os << kPrefix << L"maxJointAngularCorrection=" << FloatToWString(physics.maxJointAngularCorrection) << L"\n";
	os << kPrefix << L"maxDepenetrationVelocity=" << FloatToWString(physics.maxDepenetrationVelocity) << L"\n";
	os << kPrefix << L"maxSpringCorrectionRate=" << FloatToWString(physics.maxSpringCorrectionRate) << L"\n";
	os << kPrefix << L"springStiffnessScale=" << FloatToWString(physics.springStiffnessScale) << L"\n";
	os << kPrefix << L"minLinearDamping=" << FloatToWString(physics.minLinearDamping) << L"\n";
	os << kPrefix << L"minAngularDamping=" << FloatToWString(physics.minAngularDamping) << L"\n";
	os << kPrefix << L"maxInvInertia=" << FloatToWString(physics.maxInvInertia) << L"\n";
	os << kPrefix << L"sleepLinearSpeed=" << FloatToWString(physics.sleepLinearSpeed) << L"\n";
	os << kPrefix << L"sleepAngularSpeed=" << FloatToWString(physics.sleepAngularSpeed) << L"\n";
	os << kPrefix << L"maxInvMass=" << FloatToWString(physics.maxInvMass) << L"\n";
}

AppSettings SettingsManager::Load(const std::filesystem::path& baseDir,
								  const std::filesystem::path& defaultModelPath)
{
	AppSettings settings{};
	settings.modelPath = defaultModelPath;
	settings.alwaysOnTop = true;

	const auto path = SettingsPath(baseDir);
	if (!std::filesystem::exists(path)) return settings;

	std::wifstream fin(path);
	EnsureUtf8Locale(fin);
	if (!fin) return settings;

	std::wstring line;
	while (std::getline(fin, line))
	{
		auto pos = line.find(L'=');
		if (pos == std::wstring::npos) continue;
		auto key = Trim(line.substr(0, pos));
		auto value = Trim(line.substr(pos + 1));

		if (key == L"model")
		{
			if (!value.empty()) settings.modelPath = value;
		}
		else if (key == L"alwaysOnTop")
		{
			settings.alwaysOnTop = (value == L"1" || value == L"true" || value == L"True");
		}
		else if (key == L"targetFps")
		{
			settings.targetFps = ParseInt(value, 60);
			if (settings.targetFps < 1) settings.targetFps = 1;
		}
		else if (key == L"unlimitedFps")
		{
			settings.unlimitedFps = (value == L"1" || value == L"true" || value == L"True");
		}
		else if (key == L"windowWidth")
		{
			settings.windowWidth = ParseInt(value, 0);
		}
		else if (key == L"windowHeight")
		{
			settings.windowHeight = ParseInt(value, 0);
		}
		else if (key == L"globalPresetMode")
		{
			settings.globalPresetMode = static_cast<PresetMode>(ParseInt(value, 0));
		}
		else if (key == L"mediaReactiveEnabled")
		{
			settings.mediaReactiveEnabled = (value == L"1" || value == L"true" || value == L"True");
		}
		else if (key.rfind(L"modelPreset_", 0) == 0)
		{
			std::wstring filename = key.substr(12); // length of "modelPreset_"
			if (!filename.empty())
			{
				settings.perModelPresetSettings[filename] = static_cast<PresetMode>(ParseInt(value, 0));
			}
		}
		else if (!ParsePhysicsSettingLine(key, value, settings.physics))
		{
			ParseLightSettingLine(key, value, settings.light);
		}
	}
	return settings;
}

void SettingsManager::Save(const std::filesystem::path& baseDir,
						   const AppSettings& settings)
{
	auto pathForSave = settings.modelPath;
	if (!pathForSave.empty() && pathForSave.is_absolute())
	{
		std::error_code ec;
		auto rel = std::filesystem::relative(pathForSave, baseDir, ec);
		if (!ec) pathForSave = rel;
	}

	const auto path = SettingsPath(baseDir);
	std::wofstream fout(path);
	EnsureUtf8Locale(fout);
	if (!fout) return;

	fout << L"model=" << pathForSave.wstring() << L"\n";
	fout << L"alwaysOnTop=" << (settings.alwaysOnTop ? L"1" : L"0") << L"\n";
	fout << L"targetFps=" << IntToWString(settings.targetFps) << L"\n";
	fout << L"unlimitedFps=" << (settings.unlimitedFps ? L"1" : L"0") << L"\n";
	fout << L"windowWidth=" << IntToWString(settings.windowWidth) << L"\n";
	fout << L"windowHeight=" << IntToWString(settings.windowHeight) << L"\n";
	fout << L"globalPresetMode=" << IntToWString(static_cast<int>(settings.globalPresetMode)) << L"\n";
	fout << L"mediaReactiveEnabled=" << (settings.mediaReactiveEnabled ? L"1" : L"0") << L"\n";

	for (const auto& [name, mode] : settings.perModelPresetSettings)
	{
		fout << L"modelPreset_" << name << L"=" << IntToWString(static_cast<int>(mode)) << L"\n";
	}

	WriteLightSettings(fout, settings.light);
	WritePhysicsSettings(fout, settings.physics);
}

bool SettingsManager::HasPreset(const std::filesystem::path& baseDir, const std::filesystem::path& modelPath)
{
	auto path = GetPresetPath(baseDir, modelPath);
	return !path.empty() && std::filesystem::exists(path);
}

void SettingsManager::SavePreset(const std::filesystem::path& baseDir,
								 const std::filesystem::path& modelPath,
								 const LightSettings& lightSettings,
								 const PhysicsSettings& physicsSettings)
{
	auto path = GetPresetPath(baseDir, modelPath);
	if (path.empty()) return;

	std::wofstream fout(path);
	EnsureUtf8Locale(fout);
	if (!fout) return;

	fout << L"; Preset for " << modelPath.filename().wstring() << L"\n";
	WriteLightSettings(fout, lightSettings);
	WritePhysicsSettings(fout, physicsSettings);
}

bool SettingsManager::LoadPreset(const std::filesystem::path& baseDir,
								 const std::filesystem::path& modelPath,
								 LightSettings& outLightSettings,
								 PhysicsSettings& outPhysicsSettings)
{
	auto path = GetPresetPath(baseDir, modelPath);
	if (path.empty() || !std::filesystem::exists(path)) return false;

	std::wifstream fin(path);
	EnsureUtf8Locale(fin);
	if (!fin) return false;

	std::wstring line;
	while (std::getline(fin, line))
	{
		auto pos = line.find(L'=');
		if (pos == std::wstring::npos) continue;
		auto key = Trim(line.substr(0, pos));
		auto value = Trim(line.substr(pos + 1));
		if (!ParsePhysicsSettingLine(key, value, outPhysicsSettings))
		{
			ParseLightSettingLine(key, value, outLightSettings);
		}
	}
	return true;
}