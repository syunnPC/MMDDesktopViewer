#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SettingsWindow.hpp"
#include "App.hpp"
#include "Settings.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	constexpr wchar_t kClassName[] = L"MMDDesk.SettingsWindow";
	constexpr wchar_t kContentClassName[] = L"MMDDesk.SettingsContent";
	constexpr wchar_t kSegTabsClassName[] = L"MMDDesk.SettingsSegTabs";
	constexpr COLORREF kDarkBkColor = RGB(32, 32, 32);
	constexpr COLORREF kTextColor = RGB(240, 240, 240);

	constexpr int ID_NAV_TABS = 90;
	constexpr UINT WM_APP_NAV_CHANGED = WM_APP + 0x4D;

	constexpr int ID_MODEL_PATH = 101;
	constexpr int ID_BROWSE = 102;
	constexpr int ID_TOPMOST = 103;
	constexpr int ID_FPS_EDIT = 104;
	constexpr int ID_FPS_SPIN = 1041;
	constexpr int ID_FPS_UNLIMITED = 1042;
	constexpr int ID_PRESET_MODE_COMBO = 1043; // 新規

	constexpr int ID_SCALE_SLIDER = 105;
	constexpr int ID_BRIGHTNESS_SLIDER = 110;
	constexpr int ID_AMBIENT_SLIDER = 111;
	constexpr int ID_GLOBAL_SAT_SLIDER = 1111;
	constexpr int ID_KEY_INTENSITY_SLIDER = 112;
	constexpr int ID_FILL_INTENSITY_SLIDER = 113;
	constexpr int ID_KEY_DIR_X_SLIDER = 114;
	constexpr int ID_KEY_DIR_Y_SLIDER = 115;
	constexpr int ID_KEY_DIR_Z_SLIDER = 116;
	constexpr int ID_FILL_DIR_X_SLIDER = 117;
	constexpr int ID_FILL_DIR_Y_SLIDER = 118;
	constexpr int ID_FILL_DIR_Z_SLIDER = 119;

	constexpr int ID_RESET_LIGHT = 120;
	constexpr int ID_SAVE_PRESET = 121;
	constexpr int ID_LOAD_PRESET = 122;

	constexpr int ID_KEY_COLOR_BTN = 130;
	constexpr int ID_FILL_COLOR_BTN = 131;

	constexpr int ID_TOON_ENABLE = 140;
	constexpr int ID_TOON_CONTRAST_SLIDER = 141;
	constexpr int ID_SHADOW_HUE_SLIDER = 142;
	constexpr int ID_SHADOW_SAT_SLIDER = 143;
	constexpr int ID_RIM_WIDTH_SLIDER = 144;
	constexpr int ID_RIM_INTENSITY_SLIDER = 145;
	constexpr int ID_SPECULAR_STEP_SLIDER = 146;
	constexpr int ID_SHADOW_RAMP_SLIDER = 147;

	constexpr int ID_SHADOW_DEEP_THRESH_SLIDER = 148;
	constexpr int ID_SHADOW_DEEP_SOFT_SLIDER = 149;
	constexpr int ID_SHADOW_DEEP_MUL_SLIDER = 150;
	constexpr int ID_FACE_SHADOW_MUL_SLIDER = 151;
	constexpr int ID_FACE_CONTRAST_MUL_SLIDER = 152;

	constexpr int ID_PHYS_FIXED_TIMESTEP = 300;
	constexpr int ID_PHYS_MAX_SUBSTEPS = 301;
	constexpr int ID_PHYS_MAX_CATCHUP = 302;
	constexpr int ID_PHYS_GRAVITY_X = 303;
	constexpr int ID_PHYS_GRAVITY_Y = 304;
	constexpr int ID_PHYS_GRAVITY_Z = 305;
	constexpr int ID_PHYS_GROUND_Y = 306;
	constexpr int ID_PHYS_JOINT_COMPLIANCE = 307;
	constexpr int ID_PHYS_CONTACT_COMPLIANCE = 308;
	constexpr int ID_PHYS_JOINT_WARMSTART = 309;
	constexpr int ID_PHYS_POST_SOLVE_VEL_BLEND = 310;
	constexpr int ID_PHYS_POST_SOLVE_ANG_BLEND = 311;
	constexpr int ID_PHYS_MAX_CONTACT_ANG_CORR = 312;
	constexpr int ID_PHYS_ENABLE_RB_COLLISIONS = 313;
	constexpr int ID_PHYS_COLLISION_GROUP_SEMANTICS = 314;
	constexpr int ID_PHYS_COLLIDE_JOINT_CONNECTED = 315;
	constexpr int ID_PHYS_RESPECT_COLLISION_GROUPS = 316;
	constexpr int ID_PHYS_REQUIRE_AFTER_PHYSICS = 317;
	constexpr int ID_PHYS_GENERATE_BODY_COLLIDERS = 318;
	constexpr int ID_PHYS_MIN_EXISTING_COLLIDERS = 319;
	constexpr int ID_PHYS_MAX_GENERATED_COLLIDERS = 320;
	constexpr int ID_PHYS_GENERATED_MIN_BONE_LEN = 321;
	constexpr int ID_PHYS_GENERATED_RADIUS_RATIO = 322;
	constexpr int ID_PHYS_GENERATED_MIN_RADIUS = 323;
	constexpr int ID_PHYS_GENERATED_MAX_RADIUS = 324;
	constexpr int ID_PHYS_GENERATED_OUTLIER_FACTOR = 325;
	constexpr int ID_PHYS_GENERATED_FRICTION = 326;
	constexpr int ID_PHYS_GENERATED_RESTITUTION = 327;
	constexpr int ID_PHYS_SOLVER_ITERATIONS = 328;
	constexpr int ID_PHYS_COLLISION_ITERATIONS = 329;
	constexpr int ID_PHYS_COLLISION_MARGIN = 330;
	constexpr int ID_PHYS_PHANTOM_MARGIN = 331;
	constexpr int ID_PHYS_CONTACT_SLOP = 332;
	constexpr int ID_PHYS_WRITEBACK_FALLBACK = 333;
	constexpr int ID_PHYS_COLLISION_RADIUS_SCALE = 334;
	constexpr int ID_PHYS_MAX_LINEAR_SPEED = 335;
	constexpr int ID_PHYS_MAX_ANGULAR_SPEED = 336;
	constexpr int ID_PHYS_MAX_JOINT_POS_CORR = 337;
	constexpr int ID_PHYS_MAX_JOINT_ANG_CORR = 338;
	constexpr int ID_PHYS_MAX_DEPENETRATION = 339;
	constexpr int ID_PHYS_MAX_SPRING_RATE = 340;
	constexpr int ID_PHYS_SPRING_STIFFNESS = 341;
	constexpr int ID_PHYS_MIN_LINEAR_DAMPING = 342;
	constexpr int ID_PHYS_MIN_ANGULAR_DAMPING = 343;
	constexpr int ID_PHYS_MAX_INV_INERTIA = 344;
	constexpr int ID_PHYS_SLEEP_LINEAR_SPEED = 345;
	constexpr int ID_PHYS_SLEEP_ANGULAR_SPEED = 346;
	constexpr int ID_PHYS_MAX_INV_MASS = 347;

	constexpr int ID_OK = 200;
	constexpr int ID_CANCEL = 201;
	constexpr int ID_APPLY = 202;

	std::wstring FormatFloat(float val)
	{
		std::wostringstream oss;
		oss << std::fixed << std::setprecision(2) << val;
		return oss.str();
	}

	std::wstring FormatFloatPrec(float val, int precision)
	{
		std::wostringstream oss;
		oss << std::fixed << std::setprecision(precision) << val;
		return oss.str();
	}

	COLORREF FloatToColorRef(float r, float g, float b)
	{
		return RGB(static_cast<BYTE>(r * 255.0f), static_cast<BYTE>(g * 255.0f), static_cast<BYTE>(b * 255.0f));
	}

	int GetEditBoxInt(HWND hEdit, int defaultVal)
	{
		wchar_t buf[32]{};
		GetWindowTextW(hEdit, buf, static_cast<int>(std::size(buf)));
		try
		{
			return std::stoi(buf);
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	float GetEditBoxFloat(HWND hEdit, float defaultVal)
	{
		wchar_t buf[64]{};
		GetWindowTextW(hEdit, buf, static_cast<int>(std::size(buf)));
		try
		{
			return std::stof(buf);
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	int GetVScrollBarWidthForWindow(HWND hwnd)
	{
		UINT dpi = 96;
		const HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32)
		{
			auto pGetDpiForWindow = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(user32, "GetDpiForWindow"));
			if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);

			auto pGetSystemMetricsForDpi = reinterpret_cast<int(WINAPI*)(int, UINT)>(GetProcAddress(user32, "GetSystemMetricsForDpi"));
			if (pGetSystemMetricsForDpi) return pGetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
		}
		return GetSystemMetrics(SM_CXVSCROLL);
	}

	bool NearlyEqual(float a, float b, float relEps = 1e-5f, float absEps = 1e-6f)
	{
		const float diff = std::fabs(a - b);
		if (diff <= absEps) return true;
		const float scale = std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
		return diff <= relEps * scale;
	}

	bool LightSettingsEqual(const LightSettings& a, const LightSettings& b)
	{
		return
			NearlyEqual(a.modelScale, b.modelScale) &&
			NearlyEqual(a.brightness, b.brightness) &&
			NearlyEqual(a.ambientStrength, b.ambientStrength) &&
			NearlyEqual(a.globalSaturation, b.globalSaturation) &&
			NearlyEqual(a.keyLightIntensity, b.keyLightIntensity) &&
			NearlyEqual(a.fillLightIntensity, b.fillLightIntensity) &&
			NearlyEqual(a.keyLightColorR, b.keyLightColorR) &&
			NearlyEqual(a.keyLightColorG, b.keyLightColorG) &&
			NearlyEqual(a.keyLightColorB, b.keyLightColorB) &&
			NearlyEqual(a.fillLightColorR, b.fillLightColorR) &&
			NearlyEqual(a.fillLightColorG, b.fillLightColorG) &&
			NearlyEqual(a.fillLightColorB, b.fillLightColorB) &&
			(a.toonEnabled == b.toonEnabled) &&
			NearlyEqual(a.toonContrast, b.toonContrast) &&
			NearlyEqual(a.shadowHueShiftDeg, b.shadowHueShiftDeg) &&
			NearlyEqual(a.shadowSaturationBoost, b.shadowSaturationBoost) &&
			NearlyEqual(a.shadowRampShift, b.shadowRampShift) &&
			NearlyEqual(a.shadowDeepThreshold, b.shadowDeepThreshold) &&
			NearlyEqual(a.shadowDeepSoftness, b.shadowDeepSoftness) &&
			NearlyEqual(a.shadowDeepMul, b.shadowDeepMul) &&
			NearlyEqual(a.faceShadowMul, b.faceShadowMul) &&
			NearlyEqual(a.faceToonContrastMul, b.faceToonContrastMul) &&
			NearlyEqual(a.rimWidth, b.rimWidth) &&
			NearlyEqual(a.rimIntensity, b.rimIntensity) &&
			NearlyEqual(a.specularStep, b.specularStep) &&
			NearlyEqual(a.keyLightDirX, b.keyLightDirX) &&
			NearlyEqual(a.keyLightDirY, b.keyLightDirY) &&
			NearlyEqual(a.keyLightDirZ, b.keyLightDirZ) &&
			NearlyEqual(a.fillLightDirX, b.fillLightDirX) &&
			NearlyEqual(a.fillLightDirY, b.fillLightDirY) &&
			NearlyEqual(a.fillLightDirZ, b.fillLightDirZ);
	}

	bool PhysicsSettingsEqual(const PhysicsSettings& a, const PhysicsSettings& b)
	{
		return
			NearlyEqual(a.fixedTimeStep, b.fixedTimeStep) &&
			(a.maxSubSteps == b.maxSubSteps) &&
			(a.maxCatchUpSteps == b.maxCatchUpSteps) &&
			NearlyEqual(a.gravity.x, b.gravity.x) &&
			NearlyEqual(a.gravity.y, b.gravity.y) &&
			NearlyEqual(a.gravity.z, b.gravity.z) &&
			NearlyEqual(a.groundY, b.groundY) &&
			NearlyEqual(a.jointCompliance, b.jointCompliance) &&
			NearlyEqual(a.contactCompliance, b.contactCompliance) &&
			NearlyEqual(a.jointWarmStart, b.jointWarmStart) &&
			NearlyEqual(a.postSolveVelocityBlend, b.postSolveVelocityBlend) &&
			NearlyEqual(a.postSolveAngularVelocityBlend, b.postSolveAngularVelocityBlend) &&
			NearlyEqual(a.maxContactAngularCorrection, b.maxContactAngularCorrection) &&
			(a.enableRigidBodyCollisions == b.enableRigidBodyCollisions) &&
			(a.collisionGroupMaskSemantics == b.collisionGroupMaskSemantics) &&
			(a.collideJointConnectedBodies == b.collideJointConnectedBodies) &&
			(a.respectCollisionGroups == b.respectCollisionGroups) &&
			(a.requireAfterPhysicsFlag == b.requireAfterPhysicsFlag) &&
			(a.generateBodyCollidersIfMissing == b.generateBodyCollidersIfMissing) &&
			(a.minExistingBodyColliders == b.minExistingBodyColliders) &&
			(a.maxGeneratedBodyColliders == b.maxGeneratedBodyColliders) &&
			NearlyEqual(a.generatedBodyColliderMinBoneLength, b.generatedBodyColliderMinBoneLength) &&
			NearlyEqual(a.generatedBodyColliderRadiusRatio, b.generatedBodyColliderRadiusRatio) &&
			NearlyEqual(a.generatedBodyColliderMinRadius, b.generatedBodyColliderMinRadius) &&
			NearlyEqual(a.generatedBodyColliderMaxRadius, b.generatedBodyColliderMaxRadius) &&
			NearlyEqual(a.generatedBodyColliderOutlierDistanceFactor, b.generatedBodyColliderOutlierDistanceFactor) &&
			NearlyEqual(a.generatedBodyColliderFriction, b.generatedBodyColliderFriction) &&
			NearlyEqual(a.generatedBodyColliderRestitution, b.generatedBodyColliderRestitution) &&
			(a.solverIterations == b.solverIterations) &&
			(a.collisionIterations == b.collisionIterations) &&
			NearlyEqual(a.collisionMargin, b.collisionMargin) &&
			NearlyEqual(a.phantomMargin, b.phantomMargin) &&
			NearlyEqual(a.contactSlop, b.contactSlop) &&
			(a.writebackFallbackPositionAdjustOnly == b.writebackFallbackPositionAdjustOnly) &&
			NearlyEqual(a.collisionRadiusScale, b.collisionRadiusScale) &&
			NearlyEqual(a.maxLinearSpeed, b.maxLinearSpeed) &&
			NearlyEqual(a.maxAngularSpeed, b.maxAngularSpeed) &&
			NearlyEqual(a.maxJointPositionCorrection, b.maxJointPositionCorrection) &&
			NearlyEqual(a.maxJointAngularCorrection, b.maxJointAngularCorrection) &&
			NearlyEqual(a.maxDepenetrationVelocity, b.maxDepenetrationVelocity) &&
			NearlyEqual(a.maxSpringCorrectionRate, b.maxSpringCorrectionRate) &&
			NearlyEqual(a.springStiffnessScale, b.springStiffnessScale) &&
			NearlyEqual(a.minLinearDamping, b.minLinearDamping) &&
			NearlyEqual(a.minAngularDamping, b.minAngularDamping) &&
			NearlyEqual(a.maxInvInertia, b.maxInvInertia) &&
			NearlyEqual(a.sleepLinearSpeed, b.sleepLinearSpeed) &&
			NearlyEqual(a.sleepAngularSpeed, b.sleepAngularSpeed) &&
			NearlyEqual(a.maxInvMass, b.maxInvMass);
	}

	// ------------------------------
	// Dark mode helpers (best-effort)
	// ------------------------------
	enum class PreferredAppMode : int
	{
		Default = 0,
		AllowDark = 1,
		ForceDark = 2,
		ForceLight = 3,
		Max = 4
	};

	using SetPreferredAppMode_t = PreferredAppMode(WINAPI*)(PreferredAppMode);
	using AllowDarkModeForWindow_t = BOOL(WINAPI*)(HWND, BOOL);
	using RefreshImmersiveColorPolicyState_t = void (WINAPI*)();
	using FlushMenuThemes_t = void (WINAPI*)();

	struct DarkModeApi
	{
		HMODULE hUxTheme{};
		SetPreferredAppMode_t setPreferredAppMode{};
		AllowDarkModeForWindow_t allowDarkModeForWindow{};
		RefreshImmersiveColorPolicyState_t refreshImmersiveColorPolicyState{};
		FlushMenuThemes_t flushMenuThemes{};

		DarkModeApi()
		{
			hUxTheme = LoadLibraryW(L"uxtheme.dll");
			if (!hUxTheme) return;

			// These are undocumented exports; if they aren't present, we simply skip.
			setPreferredAppMode = reinterpret_cast<SetPreferredAppMode_t>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
			allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindow_t>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
			refreshImmersiveColorPolicyState = reinterpret_cast<RefreshImmersiveColorPolicyState_t>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104)));
			flushMenuThemes = reinterpret_cast<FlushMenuThemes_t>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));

			if (setPreferredAppMode) setPreferredAppMode(PreferredAppMode::AllowDark);
			if (refreshImmersiveColorPolicyState) refreshImmersiveColorPolicyState();
			if (flushMenuThemes) flushMenuThemes();
		}

		~DarkModeApi()
		{
			if (hUxTheme) FreeLibrary(hUxTheme);
		}
	};

	DarkModeApi& GetDarkModeApi()
	{
		static DarkModeApi api;
		return api;
	}

	constexpr UINT WM_APP_REFRESH_DARKSCROLLBARS = WM_APP + 0x4A3;
	const wchar_t* const kPropThemePending = L"__MMD_ThemeRefreshPending";
	// Per-window flags to avoid infinite WM_THEMECHANGED loops caused by SetWindowTheme.
	const wchar_t* const kPropThemeApplied = L"__MMD_DarkThemeApplied";
	const wchar_t* const kPropIgnoreThemeCount = L"__MMD_IgnoreThemeChangeCount";

	void ScheduleDarkScrollbarsRefresh(HWND hwnd)
	{
		if (!IsWindow(hwnd)) return;
		if (GetPropW(hwnd, kPropThemePending)) return;
		SetPropW(hwnd, kPropThemePending, reinterpret_cast<HANDLE>(1));
		PostMessageW(hwnd, WM_APP_REFRESH_DARKSCROLLBARS, 0, 0);
	}

	void EnableDarkScrollbarsBestEffort(HWND hwnd)
	{
		if (!IsWindow(hwnd)) return;

		// SetWindowTheme/AllowDarkModeForWindow may synchronously trigger WM_THEMECHANGED/WM_SETTINGCHANGE
		// on some systems. Guard against re-entrancy to avoid stack overflow.
		static thread_local bool s_inApply = false;
		if (s_inApply) return;
		s_inApply = true;
		struct Reset
		{
			bool* p; ~Reset()
			{
				*p = false;
			}
		} reset{ &s_inApply };

		auto& api = GetDarkModeApi();
		if (api.allowDarkModeForWindow) api.allowDarkModeForWindow(hwnd, TRUE);

		// Only call SetWindowTheme once per HWND unless the OS theme actually changes.
		// Repeated SetWindowTheme() can trigger WM_THEMECHANGED/WM_SETTINGCHANGE continuously on some systems.
		if (!GetPropW(hwnd, kPropThemeApplied))
		{
			// Ignore up to 2 subsequent theme-related messages that may be fired as a side effect.
			SetPropW(hwnd, kPropIgnoreThemeCount, reinterpret_cast<HANDLE>(2));
			SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
			SetPropW(hwnd, kPropThemeApplied, reinterpret_cast<HANDLE>(1));
		}
	}

	// ------------------------------
	// Segmented navigation tabs (custom painted)
	// ------------------------------
	constexpr UINT SEGMSG_SETSEL = WM_USER + 0x231;
	constexpr UINT SEGMSG_GETSEL = WM_USER + 0x232;

	constexpr int kSegTabCount = 4;
	const wchar_t* const kSegTabLabels[kSegTabCount] = { L"基本", L"ライト", L"トゥーン", L"物理" };

	// Modern dark segmented tabs
	constexpr COLORREF kTabsPillFill = RGB(42, 42, 42);
	constexpr COLORREF kTabsPillBorder = RGB(64, 64, 64);
	constexpr COLORREF kTabsHoverFill = RGB(58, 58, 58);
	constexpr COLORREF kTabsPressedFill = RGB(66, 66, 66);
	constexpr COLORREF kTabsSelectedFill = RGB(74, 74, 74);
	constexpr COLORREF kTabsSelectedBorder = RGB(112, 112, 112);
	constexpr COLORREF kTabsText = RGB(230, 230, 230);
	constexpr COLORREF kTabsTextSelected = RGB(255, 255, 255);

	constexpr int kTabsRadius = 10;
	constexpr int kTabsGap = 8;
	constexpr int kTabsPadX = 14;
	constexpr int kTabsPadY = 2;

	struct SegTabsState
	{
		SettingsWindow* owner{};
		int sel{ 0 };
		int hover{ -1 };
		int pressed{ -1 };
		bool tracking{ false };
		HFONT font{};
	};

	struct SegTabsLayout
	{
		RECT pill{};
		RECT seg[kSegTabCount]{};
	};

	SegTabsLayout SegTabsComputeLayout(HWND hwnd, const SegTabsState* st)
	{
		SegTabsLayout lo{};
		RECT rcClient{};
		GetClientRect(hwnd, &rcClient);
		RECT rc = rcClient;
		InflateRect(&rc, -2, -2);
		InflateRect(&rc, -1, -kTabsPadY);

		const int availW = std::max(0l, rc.right - rc.left);
		const int availH = std::max(0l, rc.bottom - rc.top);
		if (availW <= 0 || availH <= 0)
		{
			lo.pill = rc;
			return lo;
		}

		// Measure text widths
		int segW[kSegTabCount]{};
		{
			HDC hdc = GetDC(hwnd);
			HFONT font = st && st->font ? st->font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			HGDIOBJ old = SelectObject(hdc, font);
			for (int i = 0; i < kSegTabCount; ++i)
			{
				SIZE sz{};
				GetTextExtentPoint32W(hdc, kSegTabLabels[i], (int)wcslen(kSegTabLabels[i]), &sz);
				segW[i] = std::max(64l, sz.cx + kTabsPadX * 2);
			}
			SelectObject(hdc, old);
			ReleaseDC(hwnd, hdc);
		}

		int totalW = 0;
		for (int i = 0; i < kSegTabCount; ++i) totalW += segW[i];
		totalW += kTabsGap * (kSegTabCount - 1);

		// Fallback to equal width if it doesn't fit
		if (totalW > availW)
		{
			const int gap = std::min(kTabsGap, 4);
			const int usable = std::max(0, availW - gap * (kSegTabCount - 1));
			const int each = (kSegTabCount > 0) ? (usable / kSegTabCount) : usable;
			int x = rc.left;
			for (int i = 0; i < kSegTabCount; ++i)
			{
				lo.seg[i] = RECT{ x, rc.top, x + each, rc.bottom };
				x += each + gap;
			}
			lo.pill = rc;
			return lo;
		}

		int x = rc.left + (availW - totalW) / 2;
		for (int i = 0; i < kSegTabCount; ++i)
		{
			lo.seg[i] = RECT{ x, rc.top, x + segW[i], rc.bottom };
			x += segW[i] + kTabsGap;
		}
		lo.pill = RECT{ lo.seg[0].left - 6, rc.top, lo.seg[kSegTabCount - 1].right + 6, rc.bottom };
		if (lo.pill.left < rc.left) lo.pill.left = rc.left;
		if (lo.pill.right > rc.right) lo.pill.right = rc.right;
		return lo;
	}

	int SegTabsHitIndex(HWND hwnd, const SegTabsState* st, const POINT& pt)
	{
		const SegTabsLayout lo = SegTabsComputeLayout(hwnd, st);
		for (int i = 0; i < kSegTabCount; ++i)
		{
			if (PtInRect(&lo.seg[i], pt)) return i;
		}
		return -1;
	}

	void SegTabsNotifySelection(HWND hwnd, SegTabsState* st)
	{
		if (!st) return;
		HWND parent = GetParent(hwnd);
		if (parent) SendMessageW(parent, WM_APP_NAV_CHANGED, static_cast<WPARAM>(st->sel), reinterpret_cast<LPARAM>(hwnd));
	}

	void SegTabsInvalidate(HWND hwnd)
	{
		InvalidateRect(hwnd, nullptr, FALSE);
	}

	LRESULT CALLBACK SegTabsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto* st = reinterpret_cast<SegTabsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

		switch (msg)
		{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				auto* newState = new SegTabsState{};
				newState->owner = static_cast<SettingsWindow*>(cs->lpCreateParams);
				newState->sel = 0;
				newState->hover = -1;
				newState->pressed = -1;
				newState->tracking = false;
				newState->font = nullptr;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(newState));
				return 0;
			}
			case WM_DESTROY:
			{
				delete st;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
				return 0;
			}
			case WM_SETFONT:
			{
				if (st) st->font = reinterpret_cast<HFONT>(wParam);
				SegTabsInvalidate(hwnd);
				return 0;
			}
			case WM_GETFONT:
				return st ? reinterpret_cast<LRESULT>(st->font) : 0;

			case SEGMSG_SETSEL:
			{
				if (!st) return 0;
				const int idx = static_cast<int>(wParam);
				st->sel = std::clamp(idx, 0, kSegTabCount - 1);
				SegTabsInvalidate(hwnd);
				return 0;
			}
			case SEGMSG_GETSEL:
				return st ? st->sel : 0;

			case WM_GETDLGCODE:
				return DLGC_WANTARROWS | DLGC_WANTCHARS;

			case WM_KEYDOWN:
			{
				if (!st) break;
				int next = st->sel;
				if (wParam == VK_LEFT) next = std::max(0, st->sel - 1);
				else if (wParam == VK_RIGHT) next = std::min(kSegTabCount - 1, st->sel + 1);
				else break;

				if (next != st->sel)
				{
					st->sel = next;
					SegTabsNotifySelection(hwnd, st);
					SegTabsInvalidate(hwnd);
				}
				return 0;
			}

			case WM_LBUTTONDOWN:
			{
				SetFocus(hwnd);
				if (!st) break;
				POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const int hit = SegTabsHitIndex(hwnd, st, pt);
				st->pressed = hit;
				SetCapture(hwnd);
				if (hit >= 0 && hit != st->sel)
				{
					st->sel = hit;
					SegTabsNotifySelection(hwnd, st);
				}
				SegTabsInvalidate(hwnd);
				return 0;
			}
			case WM_LBUTTONUP:
			{
				if (!st) break;
				if (GetCapture() == hwnd) ReleaseCapture();
				st->pressed = -1;
				SegTabsInvalidate(hwnd);
				return 0;
			}

			case WM_MOUSEMOVE:
			{
				if (!st) break;
				if (!st->tracking)
				{
					TRACKMOUSEEVENT tme{};
					tme.cbSize = sizeof(tme);
					tme.dwFlags = TME_LEAVE;
					tme.hwndTrack = hwnd;
					TrackMouseEvent(&tme);
					st->tracking = true;
				}

				POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const int hit = SegTabsHitIndex(hwnd, st, pt);
				if (hit != st->hover)
				{
					st->hover = hit;
					SegTabsInvalidate(hwnd);
				}
				return 0;
			}
			case WM_MOUSELEAVE:
			{
				if (!st) break;
				st->tracking = false;
				st->pressed = -1;
				if (st->hover != -1)
				{
					st->hover = -1;
					SegTabsInvalidate(hwnd);
				}
				return 0;
			}

			case WM_ERASEBKGND:
				return 1;

			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);

				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);

				HDC memDC = CreateCompatibleDC(hdc);
				HBITMAP memBmp = CreateCompatibleBitmap(hdc, std::max(1l, rcClient.right - rcClient.left), std::max(1l, rcClient.bottom - rcClient.top));
				HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

				// Background
				{
					HBRUSH bg = CreateSolidBrush(kDarkBkColor);
					FillRect(memDC, &rcClient, bg);
					DeleteObject(bg);
				}

				const SegTabsLayout lo = SegTabsComputeLayout(hwnd, st);

				// Container pill
				{
					HPEN pen = CreatePen(PS_SOLID, 1, kTabsPillBorder);
					HBRUSH brush = CreateSolidBrush(kTabsPillFill);
					HGDIOBJ oldPen = SelectObject(memDC, pen);
					HGDIOBJ oldBrush = SelectObject(memDC, brush);
					RoundRect(memDC, lo.pill.left, lo.pill.top, lo.pill.right, lo.pill.bottom, kTabsRadius * 2, kTabsRadius * 2);
					SelectObject(memDC, oldBrush);
					SelectObject(memDC, oldPen);
					DeleteObject(brush);
					DeleteObject(pen);
				}

				// Segment fills (selected/hover/pressed)
				if (st)
				{
					for (int i = 0; i < kSegTabCount; ++i)
					{
						RECT seg = lo.seg[i];
						InflateRect(&seg, -1, -1);
						if (seg.right <= seg.left) continue;

						const bool isSel = (i == st->sel);
						const bool isHover = (!isSel && i == st->hover);
						const bool isPressed = (i == st->pressed);

						bool draw = isSel || isHover || isPressed;
						if (!draw) continue;

						COLORREF fill = isSel ? kTabsSelectedFill : (isPressed ? kTabsPressedFill : kTabsHoverFill);
						COLORREF border = isSel ? kTabsSelectedBorder : kTabsPillBorder;
						if (isPressed && !isSel) border = kTabsSelectedBorder;

						HPEN pen = CreatePen(PS_SOLID, 1, border);
						HBRUSH brush = CreateSolidBrush(fill);
						HGDIOBJ oldPen = SelectObject(memDC, pen);
						HGDIOBJ oldBrush = SelectObject(memDC, brush);
						RoundRect(memDC, seg.left, seg.top, seg.right, seg.bottom, kTabsRadius * 2, kTabsRadius * 2);
						SelectObject(memDC, oldBrush);
						SelectObject(memDC, oldPen);
						DeleteObject(brush);
						DeleteObject(pen);
					}
				}

				// Text
				{
					const HFONT font = st && st->font ? st->font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
					HGDIOBJ oldFont = SelectObject(memDC, font);
					SetBkMode(memDC, TRANSPARENT);

					for (int i = 0; i < kSegTabCount; ++i)
					{
						const bool isSel = st && (i == st->sel);
						SetTextColor(memDC, isSel ? kTabsTextSelected : kTabsText);
						RECT tr = lo.seg[i];
						DrawTextW(memDC, kSegTabLabels[i], -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
					}
					SelectObject(memDC, oldFont);
				}

				// Focus ring (subtle)
				if (GetFocus() == hwnd)
				{
					RECT fr = lo.pill;
					InflateRect(&fr, -1, -1);
					HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
					HGDIOBJ oldPen = SelectObject(memDC, pen);
					HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
					RoundRect(memDC, fr.left, fr.top, fr.right, fr.bottom, kTabsRadius * 2, kTabsRadius * 2);
					SelectObject(memDC, oldBrush);
					SelectObject(memDC, oldPen);
					DeleteObject(pen);
				}

				BitBlt(hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, memDC, 0, 0, SRCCOPY);

				SelectObject(memDC, oldBmp);
				DeleteObject(memBmp);
				DeleteDC(memDC);

				EndPaint(hwnd, &ps);
				return 0;
			}

			default:
				break;
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

}

SettingsWindow::SettingsWindow(App& app, HINSTANCE hInst) : m_app(app), m_hInst(hInst)
{
	INITCOMMONCONTROLSEX icex{};
	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	m_darkBrush = CreateSolidBrush(kDarkBkColor);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProcThunk;
	wc.hInstance = m_hInst;
	wc.lpszClassName = kClassName;
	wc.hbrBackground = m_darkBrush;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	WNDCLASSEXW wcc{};
	wcc.cbSize = sizeof(wcc);
	wcc.lpfnWndProc = ContentProcThunk;
	wcc.hInstance = m_hInst;
	wcc.lpszClassName = kContentClassName;
	wcc.hbrBackground = m_darkBrush;
	wcc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassExW(&wcc);

	WNDCLASSEXW wct{};
	wct.cbSize = sizeof(wct);
	wct.lpfnWndProc = SegTabsProc;
	wct.hInstance = m_hInst;
	wct.lpszClassName = kSegTabsClassName;
	wct.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wct.hbrBackground = m_darkBrush; // paint is custom, but keep consistent background
	RegisterClassExW(&wct);

	for (int i = 0; i < 16; ++i) m_customColors[i] = RGB(255, 255, 255);

	m_hFont = CreateFontW(
		-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

	m_hHeaderFont = CreateFontW(
		-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

SettingsWindow::~SettingsWindow()
{
	if (m_hwnd) DestroyWindow(m_hwnd);
	UnregisterClassW(kClassName, m_hInst);
	UnregisterClassW(kContentClassName, m_hInst);
	UnregisterClassW(kSegTabsClassName, m_hInst);
	if (m_hFont) DeleteObject(m_hFont);
	if (m_hHeaderFont) DeleteObject(m_hHeaderFont);
	if (m_darkBrush) DeleteObject(m_darkBrush);
	if (m_tooltip) DestroyWindow(m_tooltip);
}

void SettingsWindow::Show()
{
	if (!m_hwnd)
	{
		m_hwnd = CreateWindowExW(
			WS_EX_DLGMODALFRAME,
			kClassName, L"設定",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 640, 920,
			nullptr, nullptr, m_hInst, this);

		// 横幅固定(縦のみリサイズ可)
		RECT wr{};
		GetWindowRect(m_hwnd, &wr);
		m_fixedWindowWidth = wr.right - wr.left;

		BOOL useDarkMode = TRUE;
		DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
		// Scrollbar theming is applied to the scrollable content window (m_content).
		// Applying SetWindowTheme on the top-level window can trigger redundant theme notifications on some systems.
	}

	if (!m_created)
	{
		CreateControls();
		m_created = true;
	}

	ScrollTo(0);

	m_backupSettings = m_app.Settings();
	LoadCurrentSettings();
	ShowWindow(m_hwnd, SW_SHOW);
	SetForegroundWindow(m_hwnd);
}

void SettingsWindow::Hide()
{
	if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

void SettingsWindow::SetModernFont(HWND hChild)
{
	if (m_hFont && hChild) SendMessageW(hChild, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
}

void SettingsWindow::SetHeaderFont(HWND hChild)
{
	if (m_hHeaderFont && hChild) SendMessageW(hChild, WM_SETFONT, reinterpret_cast<WPARAM>(m_hHeaderFont), TRUE);
}

void SettingsWindow::SetDarkTheme(HWND hChild)
{
	if (hChild) SetWindowTheme(hChild, L"DarkMode_Explorer", nullptr);
}

void SettingsWindow::CreateControls()
{
	// -------- 固定UI (タブ + フッター) --------
	RECT rcMain{};
	GetClientRect(m_hwnd, &rcMain);
	const int clientW = rcMain.right - rcMain.left;
	const int clientH = rcMain.bottom - rcMain.top;

	const int outerPadX = 16;
	const int outerPadY = 10;
	const int tabH = 32;
	m_headerHeight = tabH + outerPadY * 2;
	m_footerHeight = 58;
	const int footerTop = std::max(m_headerHeight, clientH - m_footerHeight);

	m_tabs = CreateWindowExW(
		0,
		kSegTabsClassName,
		nullptr,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		outerPadX,
		outerPadY,
		std::max(0, clientW - outerPadX * 2),
		tabH,
		m_hwnd,
		reinterpret_cast<HMENU>(ID_NAV_TABS),
		m_hInst,
		this);
	SetModernFont(m_tabs);
	SendMessageW(m_tabs, SEGMSG_SETSEL, 0, 0);

	m_footerDivider = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
									  0, footerTop, clientW, 1, m_hwnd, nullptr, m_hInst, nullptr);

	const int btnW = 92;
	const int btnH = 30;
	const int footerPad = 16;
	const int footerY = footerTop + (m_footerHeight - btnH) / 2;
	const int xApply = std::max(footerPad, clientW - footerPad - btnW);
	const int xCancel = std::max(footerPad, xApply - 10 - btnW);
	const int xOk = std::max(footerPad, xCancel - 10 - btnW);

	m_okBtn = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
							  xOk, footerY, btnW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_OK), m_hInst, nullptr);
	SetModernFont(m_okBtn);
	SetDarkTheme(m_okBtn);

	m_cancelBtn = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE,
								  xCancel, footerY, btnW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_CANCEL), m_hInst, nullptr);
	SetModernFont(m_cancelBtn);
	SetDarkTheme(m_cancelBtn);

	m_applyBtn = CreateWindowExW(0, L"BUTTON", L"適用", WS_CHILD | WS_VISIBLE,
								 xApply, footerY, btnW, btnH, m_hwnd, reinterpret_cast<HMENU>(ID_APPLY), m_hInst, nullptr);
	SetModernFont(m_applyBtn);
	SetDarkTheme(m_applyBtn);

	// -------- スクロール領域 --------
	m_content = CreateWindowExW(
		0,
		kContentClassName,
		nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
		0,
		m_headerHeight,
		clientW,
		std::max(0, footerTop - m_headerHeight),
		m_hwnd,
		nullptr,
		m_hInst,
		this);
	EnableDarkScrollbarsBestEffort(m_content);

	// ここから下は m_content にコントロールを作成する
	HWND parent = m_content;
	RECT rcContent{};
	GetClientRect(parent, &rcContent);
	const int contentW = rcContent.right - rcContent.left;

	int y = 14;
	const int rowH = 32;
	const int xPadding = 20;
	const int labelW = 170;
	const int valueW = 56;
	const int browseBtnW = 86;
	const int usableW = std::max(0, contentW - xPadding * 2);
	const int editW = std::max(220, usableW - labelW - browseBtnW - 14);
	const int sliderW = std::max(200, usableW - labelW - valueW - 14);
	const int sectionW = std::max(0, usableW);

	auto CreateLabel = [&](const wchar_t* text, int x, int y, int w) {
		HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, y, w, 24, parent, nullptr, m_hInst, nullptr);
		SetModernFont(h);
		return h;
		};

	auto CreateSectionHeader = [&](const wchar_t* text, int x, int y, int w) {
		HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, x, y, w, 24, parent, nullptr, m_hInst, nullptr);
		SetHeaderFont(h);
		return h;
		};

	auto CreateDivider = [&](int x, int y, int w) {
		HWND h = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, x, y, w, 1, parent, nullptr, m_hInst, nullptr);
		return h;
		};

	auto CreateSlider = [&](int id, int x, int y, int w) {
		HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, 24, parent, reinterpret_cast<HMENU>(id), m_hInst, nullptr);
		SetModernFont(h);
		SetDarkTheme(h);
		return h;
		};

	auto CreateEdit = [&](int id, int x, int y, int w, bool numeric = false) {
		DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
		if (numeric) style |= ES_NUMBER;
		HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, x, y, w, 24, parent, reinterpret_cast<HMENU>(id), m_hInst, nullptr);
		SetModernFont(h);
		SetDarkTheme(h);
		return h;
		};

	auto CreateCheck = [&](int id, const wchar_t* text, int x, int y, int w) {
		HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, 24, parent, reinterpret_cast<HMENU>(id), m_hInst, nullptr);
		SetModernFont(h);
		SetDarkTheme(h);
		return h;
		};

	m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
								CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
								m_hwnd, nullptr, m_hInst, nullptr);
	SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, 340);
	SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
	SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);

	auto AddTooltip = [&](HWND target, const wchar_t* text) {
		if (!m_tooltip || !target || !text) return;
		TTTOOLINFOW tti{};
		tti.cbSize = sizeof(tti);
		tti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		tti.hwnd = m_hwnd;
		tti.uId = reinterpret_cast<UINT_PTR>(target);
		tti.lpszText = const_cast<LPWSTR>(text);
		SendMessageW(m_tooltip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&tti));
		};

	// ---- 基本設定 ----
	m_sectionYBasic = y;
	CreateSectionHeader(L"基本設定", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;

	// モデルパス
	CreateLabel(L"モデルパス:", xPadding, y, labelW);
	m_modelPathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
									  xPadding + labelW + 6, y, editW, 24, parent, reinterpret_cast<HMENU>(ID_MODEL_PATH), m_hInst, nullptr);
	SetModernFont(m_modelPathEdit);
	SetDarkTheme(m_modelPathEdit);
	m_browseBtn = CreateWindowExW(0, L"BUTTON", L"参照...", WS_CHILD | WS_VISIBLE,
								  xPadding + labelW + 6 + editW + 8, y, browseBtnW, 24, parent, reinterpret_cast<HMENU>(ID_BROWSE), m_hInst, nullptr);
	SetModernFont(m_browseBtn);
	SetDarkTheme(m_browseBtn);
	y += rowH + 8;

	m_topmostCheck = CreateCheck(ID_TOPMOST, L"常に最前面に表示", xPadding, y, 220);
	y += rowH + 10;

	CreateLabel(L"最大FPS:", xPadding, y, labelW);
	m_fpsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER,
								xPadding + labelW + 6, y, 76, 24, parent, reinterpret_cast<HMENU>(ID_FPS_EDIT), m_hInst, nullptr);
	SetModernFont(m_fpsEdit);
	SetDarkTheme(m_fpsEdit);
	m_fpsSpin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
								WS_CHILD | WS_VISIBLE | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_ALIGNRIGHT,
								xPadding + labelW + 6 + 76, y, 20, 24, parent, reinterpret_cast<HMENU>(ID_FPS_SPIN), m_hInst, nullptr);
	SendMessageW(m_fpsSpin, UDM_SETRANGE32, 10, 240);
	SendMessageW(m_fpsSpin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(m_fpsEdit), 0);
	m_unlimitedFpsCheck = CreateCheck(ID_FPS_UNLIMITED, L"無制限", xPadding + labelW + 6 + 104, y, 120);
	y += rowH;

	CreateLabel(L"プリセット読み込み:", xPadding, y, labelW);
	m_presetModeCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
										xPadding + labelW + 6, y, 240, 200, parent, reinterpret_cast<HMENU>(ID_PRESET_MODE_COMBO), m_hInst, nullptr);
	SetModernFont(m_presetModeCombo);
	SetDarkTheme(m_presetModeCombo);
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"モデル読み込み時に確認"));
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"常に読み込む"));
	SendMessageW(m_presetModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"読み込まない"));
	y += rowH + 20;

	// 表示設定
	m_sectionYLight = y;
	CreateSectionHeader(L"表示・ライト", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;
	CreateLabel(L"モデルサイズ:", xPadding, y, labelW);
	m_scaleSlider = CreateSlider(ID_SCALE_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_scaleSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 875));
	m_scaleLabel = CreateLabel(L"1.00", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"明るさ:", xPadding, y, labelW);
	m_brightnessSlider = CreateSlider(ID_BRIGHTNESS_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_brightnessSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 300));
	m_brightnessLabel = CreateLabel(L"1.30", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"環境光:", xPadding, y, labelW);
	m_ambientSlider = CreateSlider(ID_AMBIENT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_ambientSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_ambientLabel = CreateLabel(L"0.45", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"全体の彩度:", xPadding, y, labelW);
	m_globalSatSlider = CreateSlider(ID_GLOBAL_SAT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_globalSatSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_globalSatLabel = CreateLabel(L"1.10", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"主光源強度/色:", xPadding, y, labelW);
	m_keyIntensitySlider = CreateSlider(ID_KEY_INTENSITY_SLIDER, xPadding + labelW, y, sliderW - 80);
	SendMessageW(m_keyIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 300));
	m_keyIntensityLabel = CreateLabel(L"1.40", xPadding + labelW + (sliderW - 80) + 5, y, 40);
	m_keyColorBtn = CreateWindowExW(0, L"BUTTON", L"色", WS_CHILD | WS_VISIBLE, xPadding + labelW + (sliderW - 80) + 50, y, 40, 24, parent, reinterpret_cast<HMENU>(ID_KEY_COLOR_BTN), m_hInst, nullptr);
	SetModernFont(m_keyColorBtn); SetDarkTheme(m_keyColorBtn);
	m_keyColorPreview = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, xPadding + labelW + (sliderW - 80) + 95, y + 2, 20, 20, parent, nullptr, m_hInst, nullptr);
	y += rowH;

	CreateLabel(L"補助光源強度/色:", xPadding, y, labelW);
	m_fillIntensitySlider = CreateSlider(ID_FILL_INTENSITY_SLIDER, xPadding + labelW, y, sliderW - 80);
	SendMessageW(m_fillIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillIntensityLabel = CreateLabel(L"0.50", xPadding + labelW + (sliderW - 80) + 5, y, 40);
	m_fillColorBtn = CreateWindowExW(0, L"BUTTON", L"色", WS_CHILD | WS_VISIBLE, xPadding + labelW + (sliderW - 80) + 50, y, 40, 24, parent, reinterpret_cast<HMENU>(ID_FILL_COLOR_BTN), m_hInst, nullptr);
	SetModernFont(m_fillColorBtn); SetDarkTheme(m_fillColorBtn);
	m_fillColorPreview = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, xPadding + labelW + (sliderW - 80) + 95, y + 2, 20, 20, parent, nullptr, m_hInst, nullptr);
	y += rowH + 20;

	// Toon
	m_sectionYToon = y;
	CreateSectionHeader(L"トゥーンシェーディング", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;
	m_toonEnableCheck = CreateWindowExW(0, L"BUTTON", L"トゥーンシェーディング有効", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, xPadding, y, 220, 24, parent, reinterpret_cast<HMENU>(ID_TOON_ENABLE), m_hInst, nullptr);
	SetModernFont(m_toonEnableCheck); SetDarkTheme(m_toonEnableCheck);
	y += rowH;

	CreateLabel(L"コントラスト:", xPadding, y, labelW);
	m_toonContrastSlider = CreateSlider(ID_TOON_CONTRAST_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_toonContrastSlider, TBM_SETRANGE, TRUE, MAKELONG(50, 250));
	m_toonContrastLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の色味(度):", xPadding, y, labelW);
	m_shadowHueSlider = CreateSlider(ID_SHADOW_HUE_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowHueSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 90));
	m_shadowHueLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の彩度:", xPadding, y, labelW);
	m_shadowSatSlider = CreateSlider(ID_SHADOW_SAT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowSatSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowSatLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"影の範囲:", xPadding, y, labelW);
	m_shadowRampSlider = CreateSlider(ID_SHADOW_RAMP_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowRampSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowRampLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	// Deep Shadow
	CreateLabel(L"濃い影の閾値:", xPadding, y, labelW);
	m_shadowDeepThresholdSlider = CreateSlider(ID_SHADOW_DEEP_THRESH_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepThresholdSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowDeepThresholdLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"濃い影の境界:", xPadding, y, labelW);
	m_shadowDeepSoftSlider = CreateSlider(ID_SHADOW_DEEP_SOFT_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepSoftSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 50));
	m_shadowDeepSoftLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"濃い影の強度:", xPadding, y, labelW);
	m_shadowDeepMulSlider = CreateSlider(ID_SHADOW_DEEP_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_shadowDeepMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_shadowDeepMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"リム幅/強度:", xPadding, y, labelW);
	m_rimWidthSlider = CreateSlider(ID_RIM_WIDTH_SLIDER, xPadding + labelW, y, sliderW / 2 - 5);
	SendMessageW(m_rimWidthSlider, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
	m_rimIntensitySlider = CreateSlider(ID_RIM_INTENSITY_SLIDER, xPadding + labelW + sliderW / 2 + 5, y, sliderW / 2 - 5);
	SendMessageW(m_rimIntensitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_rimWidthLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"スペキュラ:", xPadding, y, labelW);
	m_specularStepSlider = CreateSlider(ID_SPECULAR_STEP_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_specularStepSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_specularStepLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH + 20;

	// Face Control
	CreateSectionHeader(L"顔マテリアル", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;
	CreateLabel(L"顔の影の濃さ:", xPadding, y, labelW);
	m_faceShadowMulSlider = CreateSlider(ID_FACE_SHADOW_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_faceShadowMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
	m_faceShadowMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH;

	CreateLabel(L"顔のコントラスト:", xPadding, y, labelW);
	m_faceContrastMulSlider = CreateSlider(ID_FACE_CONTRAST_MUL_SLIDER, xPadding + labelW, y, sliderW);
	SendMessageW(m_faceContrastMulSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_faceContrastMulLabel = CreateLabel(L"", xPadding + labelW + sliderW + 10, y, 50);
	y += rowH + 20;

	CreateSectionHeader(L"光源方向", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;
	CreateLabel(L"主光源方向 (X/Y/Z):", xPadding, y, 200); y += 24;
	int slider3W = 145;
	m_keyDirXSlider = CreateSlider(ID_KEY_DIR_X_SLIDER, xPadding, y, slider3W);
	SendMessageW(m_keyDirXSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_keyDirYSlider = CreateSlider(ID_KEY_DIR_Y_SLIDER, xPadding + 150, y, slider3W);
	SendMessageW(m_keyDirYSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_keyDirZSlider = CreateSlider(ID_KEY_DIR_Z_SLIDER, xPadding + 300, y, slider3W);
	SendMessageW(m_keyDirZSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	y += rowH + 5;

	CreateLabel(L"補助光源方向 (X/Y/Z):", xPadding, y, 200); y += 24;
	m_fillDirXSlider = CreateSlider(ID_FILL_DIR_X_SLIDER, xPadding, y, slider3W);
	SendMessageW(m_fillDirXSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillDirYSlider = CreateSlider(ID_FILL_DIR_Y_SLIDER, xPadding + 150, y, slider3W);
	SendMessageW(m_fillDirYSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	m_fillDirZSlider = CreateSlider(ID_FILL_DIR_Z_SLIDER, xPadding + 300, y, slider3W);
	SendMessageW(m_fillDirZSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
	y += rowH + 20;

	// Physics settings
	const int physicsLabelW = 230;
	const int physicsEditW = 80;

	m_sectionYPhysics = y;
	CreateSectionHeader(L"物理演算", xPadding, y, sectionW);
	CreateDivider(xPadding, y + 24, sectionW);
	y += 30;

	auto label = CreateLabel(L"固定タイムステップ:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"1ステップの時間(秒)。小さいほど精度↑/負荷↑。標準: 0.016前後。");
	m_physicsFixedTimeStepEdit = CreateEdit(ID_PHYS_FIXED_TIMESTEP, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsFixedTimeStepEdit, L"1ステップの時間(秒)。小さいほど精度↑/負荷↑。標準: 0.016前後。");
	y += rowH;

	label = CreateLabel(L"サブステップ数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"1フレームを分割する回数。増やすと安定性↑/負荷↑。標準: 1〜4。");
	m_physicsMaxSubStepsEdit = CreateEdit(ID_PHYS_MAX_SUBSTEPS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsMaxSubStepsEdit, L"1フレームを分割する回数。増やすと安定性↑/負荷↑。標準: 1〜4。");
	y += rowH;

	label = CreateLabel(L"キャッチアップ上限:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"処理遅延時の追従上限ステップ数。高いほど追従↑/負荷↑。標準: 2〜6。");
	m_physicsMaxCatchUpStepsEdit = CreateEdit(ID_PHYS_MAX_CATCHUP, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsMaxCatchUpStepsEdit, L"処理遅延時の追従上限ステップ数。高いほど追従↑/負荷↑。標準: 2〜6。");
	y += rowH;

	label = CreateLabel(L"重力 (X/Y/Z):", xPadding, y, physicsLabelW);
	AddTooltip(label, L"重力加速度。Yが下方向。標準: (0, -9.8, 0)。");
	m_physicsGravityXEdit = CreateEdit(ID_PHYS_GRAVITY_X, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsGravityYEdit = CreateEdit(ID_PHYS_GRAVITY_Y, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	m_physicsGravityZEdit = CreateEdit(ID_PHYS_GRAVITY_Z, xPadding + physicsLabelW + (physicsEditW + 5) * 2, y, physicsEditW);
	AddTooltip(m_physicsGravityXEdit, L"重力加速度。Yが下方向。標準: (0, -9.8, 0)。");
	AddTooltip(m_physicsGravityYEdit, L"重力加速度。Yが下方向。標準: (0, -9.8, 0)。");
	AddTooltip(m_physicsGravityZEdit, L"重力加速度。Yが下方向。標準: (0, -9.8, 0)。");
	y += rowH;

	label = CreateLabel(L"地面Y:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"地面の基準高さ。通常はモデルより十分低い値。");
	m_physicsGroundYEdit = CreateEdit(ID_PHYS_GROUND_Y, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsGroundYEdit, L"地面の基準高さ。通常はモデルより十分低い値。");
	y += rowH;

	label = CreateLabel(L"関節コンプライアンス:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"関節の柔らかさ。0で硬い。大きいほど柔らかい。");
	m_physicsJointComplianceEdit = CreateEdit(ID_PHYS_JOINT_COMPLIANCE, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsJointComplianceEdit, L"関節の柔らかさ。0で硬い。大きいほど柔らかい。");
	y += rowH;

	label = CreateLabel(L"接触コンプライアンス:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"衝突の柔らかさ。小さいほど硬い。標準: 0.0005〜0.005。");
	m_physicsContactComplianceEdit = CreateEdit(ID_PHYS_CONTACT_COMPLIANCE, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsContactComplianceEdit, L"衝突の柔らかさ。小さいほど硬い。標準: 0.0005〜0.005。");
	y += rowH;

	label = CreateLabel(L"関節ウォームスタート:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"前フレームの解を引き継ぐ割合。0でリセット。");
	m_physicsJointWarmStartEdit = CreateEdit(ID_PHYS_JOINT_WARMSTART, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsJointWarmStartEdit, L"前フレームの解を引き継ぐ割合。0でリセット。");
	y += rowH;

	label = CreateLabel(L"速度/角速度ブレンド:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"ポストソルブで速度を混ぜる割合。大きいほど揺れ抑制。");
	m_physicsPostSolveVelocityBlendEdit = CreateEdit(ID_PHYS_POST_SOLVE_VEL_BLEND, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsPostSolveAngularBlendEdit = CreateEdit(ID_PHYS_POST_SOLVE_ANG_BLEND, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsPostSolveVelocityBlendEdit, L"ポストソルブで速度を混ぜる割合。大きいほど揺れ抑制。");
	AddTooltip(m_physicsPostSolveAngularBlendEdit, L"ポストソルブで角速度を混ぜる割合。大きいほど揺れ抑制。");
	y += rowH;

	label = CreateLabel(L"最大角補正:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"衝突で許可する最大角度補正。大きいほど貫通修正が強い。");
	m_physicsMaxContactAngularCorrectionEdit = CreateEdit(ID_PHYS_MAX_CONTACT_ANG_CORR, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsMaxContactAngularCorrectionEdit, L"衝突で許可する最大角度補正。大きいほど貫通修正が強い。");
	y += rowH;

	m_physicsEnableRigidBodyCollisionsCheck = CreateCheck(ID_PHYS_ENABLE_RB_COLLISIONS, L"剛体同士の衝突を有効", xPadding, y, 240);
	AddTooltip(m_physicsEnableRigidBodyCollisionsCheck, L"剛体同士の衝突判定を有効にします。");
	y += rowH;

	label = CreateLabel(L"衝突グループ意味:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"衝突グループマスクの解釈方式。0=標準。");
	m_physicsCollisionGroupMaskSemanticsEdit = CreateEdit(ID_PHYS_COLLISION_GROUP_SEMANTICS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsCollisionGroupMaskSemanticsEdit, L"衝突グループマスクの解釈方式。0=標準。");
	y += rowH;

	m_physicsCollideJointConnectedBodiesCheck = CreateCheck(ID_PHYS_COLLIDE_JOINT_CONNECTED, L"関節で接続された剛体も衝突", xPadding, y, 260);
	AddTooltip(m_physicsCollideJointConnectedBodiesCheck, L"関節で繋がる剛体同士も衝突判定します。");
	y += rowH;

	m_physicsRespectCollisionGroupsCheck = CreateCheck(ID_PHYS_RESPECT_COLLISION_GROUPS, L"衝突グループを尊重", xPadding, y, 220);
	AddTooltip(m_physicsRespectCollisionGroupsCheck, L"衝突グループ設定を考慮します。");
	y += rowH;

	m_physicsRequireAfterPhysicsFlagCheck = CreateCheck(ID_PHYS_REQUIRE_AFTER_PHYSICS, L"AfterPhysicsボーンのみ反映", xPadding, y, 260);
	AddTooltip(m_physicsRequireAfterPhysicsFlagCheck, L"AfterPhysicsボーンのみ物理結果を反映します。");
	y += rowH;

	m_physicsWritebackFallbackCheck = CreateCheck(ID_PHYS_WRITEBACK_FALLBACK, L"AfterPhysics未検出時は位置補正のみ", xPadding, y, 320);
	AddTooltip(m_physicsWritebackFallbackCheck, L"AfterPhysicsが無い場合の安全な書き戻し方法。");
	y += rowH;

	m_physicsGenerateBodyCollidersCheck = CreateCheck(ID_PHYS_GENERATE_BODY_COLLIDERS, L"自動ボディコライダー生成", xPadding, y, 260);
	AddTooltip(m_physicsGenerateBodyCollidersCheck, L"ボーンからキャラクタ本体用コライダーを自動生成します。");
	y += rowH;

	label = CreateLabel(L"既存ボディ最小数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"既存ボディがこの数未満なら自動生成を実行。");
	m_physicsMinExistingBodyCollidersEdit = CreateEdit(ID_PHYS_MIN_EXISTING_COLLIDERS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsMinExistingBodyCollidersEdit, L"既存ボディがこの数未満なら自動生成を実行。");
	y += rowH;

	label = CreateLabel(L"生成ボディ最大数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"自動生成の上限数。多いほど精密/負荷↑。標準: 100〜300。");
	m_physicsMaxGeneratedBodyCollidersEdit = CreateEdit(ID_PHYS_MAX_GENERATED_COLLIDERS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsMaxGeneratedBodyCollidersEdit, L"自動生成の上限数。多いほど精密/負荷↑。標準: 100〜300。");
	y += rowH;

	label = CreateLabel(L"ボーン長最小:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"短すぎるボーンは生成対象外。小さすぎるとノイズが増えます。");
	m_physicsGeneratedMinBoneLengthEdit = CreateEdit(ID_PHYS_GENERATED_MIN_BONE_LEN, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsGeneratedMinBoneLengthEdit, L"短すぎるボーンは生成対象外。小さすぎるとノイズが増えます。");
	y += rowH;

	label = CreateLabel(L"半径比率:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"生成カプセルの太さ倍率。大きいほど太い。標準: 0.1〜0.3。");
	m_physicsGeneratedRadiusRatioEdit = CreateEdit(ID_PHYS_GENERATED_RADIUS_RATIO, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsGeneratedRadiusRatioEdit, L"生成カプセルの太さ倍率。大きいほど太い。標準: 0.1〜0.3。");
	y += rowH;

	label = CreateLabel(L"半径最小/最大:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"生成半径の下限/上限。極端な値は避けるのが無難。");
	m_physicsGeneratedMinRadiusEdit = CreateEdit(ID_PHYS_GENERATED_MIN_RADIUS, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsGeneratedMaxRadiusEdit = CreateEdit(ID_PHYS_GENERATED_MAX_RADIUS, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsGeneratedMinRadiusEdit, L"生成半径の下限。小さすぎると薄いコライダーになります。");
	AddTooltip(m_physicsGeneratedMaxRadiusEdit, L"生成半径の上限。大きすぎると体積過大。");
	y += rowH;

	label = CreateLabel(L"外れ値距離係数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"中心から遠いボーンを除外する係数。大きいほど除外しにくい。");
	m_physicsGeneratedOutlierDistanceFactorEdit = CreateEdit(ID_PHYS_GENERATED_OUTLIER_FACTOR, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsGeneratedOutlierDistanceFactorEdit, L"中心から遠いボーンを除外する係数。大きいほど除外しにくい。");
	y += rowH;

	label = CreateLabel(L"生成摩擦/反発:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"自動生成コライダーの摩擦/反発係数。摩擦は0〜1程度。");
	m_physicsGeneratedFrictionEdit = CreateEdit(ID_PHYS_GENERATED_FRICTION, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsGeneratedRestitutionEdit = CreateEdit(ID_PHYS_GENERATED_RESTITUTION, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsGeneratedFrictionEdit, L"自動生成の摩擦係数。0で滑りやすい。");
	AddTooltip(m_physicsGeneratedRestitutionEdit, L"自動生成の反発係数。0で跳ねない。");
	y += rowH;

	label = CreateLabel(L"ソルバ反復数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"拘束解決の反復回数。多いほど安定/負荷↑。標準: 2〜6。");
	m_physicsSolverIterationsEdit = CreateEdit(ID_PHYS_SOLVER_ITERATIONS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsSolverIterationsEdit, L"拘束解決の反復回数。多いほど安定/負荷↑。標準: 2〜6。");
	y += rowH;

	label = CreateLabel(L"衝突反復数:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"衝突解決の反復回数。多いほど貫通しにくい。");
	m_physicsCollisionIterationsEdit = CreateEdit(ID_PHYS_COLLISION_ITERATIONS, xPadding + physicsLabelW, y, physicsEditW, true);
	AddTooltip(m_physicsCollisionIterationsEdit, L"衝突解決の反復回数。多いほど貫通しにくい。");
	y += rowH;

	label = CreateLabel(L"衝突マージン:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"衝突の当たり判定に足す余裕。大きいほど離れ気味。");
	m_physicsCollisionMarginEdit = CreateEdit(ID_PHYS_COLLISION_MARGIN, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsCollisionMarginEdit, L"衝突の当たり判定に足す余裕。大きいほど離れ気味。");
	y += rowH;

	label = CreateLabel(L"ファントムマージン:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"静的 vs 動的の追加マージン。大きいと接触が増えます。");
	m_physicsPhantomMarginEdit = CreateEdit(ID_PHYS_PHANTOM_MARGIN, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsPhantomMarginEdit, L"静的 vs 動的の追加マージン。大きいと接触が増えます。");
	y += rowH;

	label = CreateLabel(L"接触スロップ:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"小さなめり込みを許容する値。大きいほどジッタ軽減。");
	m_physicsContactSlopEdit = CreateEdit(ID_PHYS_CONTACT_SLOP, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsContactSlopEdit, L"小さなめり込みを許容する値。大きいほどジッタ軽減。");
	y += rowH;

	label = CreateLabel(L"衝突半径スケール:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"剛体の半径倍率。大きいほど当たりが大きくなる。");
	m_physicsCollisionRadiusScaleEdit = CreateEdit(ID_PHYS_COLLISION_RADIUS_SCALE, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsCollisionRadiusScaleEdit, L"剛体の半径倍率。大きいほど当たりが大きくなる。");
	y += rowH;

	label = CreateLabel(L"最大線/角速度:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"速度の上限。大きいほど暴れやすい/小さいほど抑制。");
	m_physicsMaxLinearSpeedEdit = CreateEdit(ID_PHYS_MAX_LINEAR_SPEED, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsMaxAngularSpeedEdit = CreateEdit(ID_PHYS_MAX_ANGULAR_SPEED, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsMaxLinearSpeedEdit, L"線速度の上限。大きいほど暴れやすい。");
	AddTooltip(m_physicsMaxAngularSpeedEdit, L"角速度の上限。大きいほど回転が速い。");
	y += rowH;

	label = CreateLabel(L"関節補正(位置/角):", xPadding, y, physicsLabelW);
	AddTooltip(label, L"関節の補正量の上限。小さいほど柔らかい。");
	m_physicsMaxJointPositionCorrectionEdit = CreateEdit(ID_PHYS_MAX_JOINT_POS_CORR, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsMaxJointAngularCorrectionEdit = CreateEdit(ID_PHYS_MAX_JOINT_ANG_CORR, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsMaxJointPositionCorrectionEdit, L"位置補正の上限。大きいほど関節が硬い。");
	AddTooltip(m_physicsMaxJointAngularCorrectionEdit, L"角度補正の上限。大きいほど硬い。");
	y += rowH;

	label = CreateLabel(L"最大押し戻し速度:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"貫通解消の最大速度。大きいと反発が強くなる。");
	m_physicsMaxDepenetrationVelocityEdit = CreateEdit(ID_PHYS_MAX_DEPENETRATION, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsMaxDepenetrationVelocityEdit, L"貫通解消の最大速度。大きいと反発が強くなる。");
	y += rowH;

	label = CreateLabel(L"ばね補正率:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"ばね補正の上限割合。0〜1程度。大きいほど強い。");
	m_physicsMaxSpringCorrectionRateEdit = CreateEdit(ID_PHYS_MAX_SPRING_RATE, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsMaxSpringCorrectionRateEdit, L"ばね補正の上限割合。0〜1程度。大きいほど強い。");
	y += rowH;

	label = CreateLabel(L"ばね剛性倍率:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"回転ばねの強さ倍率。0.2〜0.6程度が目安。");
	m_physicsSpringStiffnessScaleEdit = CreateEdit(ID_PHYS_SPRING_STIFFNESS, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsSpringStiffnessScaleEdit, L"回転ばねの強さ倍率。0.2〜0.6程度が目安。");
	y += rowH;

	label = CreateLabel(L"最小減衰(線/角):", xPadding, y, physicsLabelW);
	AddTooltip(label, L"最低限の減衰量。大きいほど揺れが早く収まる。");
	m_physicsMinLinearDampingEdit = CreateEdit(ID_PHYS_MIN_LINEAR_DAMPING, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsMinAngularDampingEdit = CreateEdit(ID_PHYS_MIN_ANGULAR_DAMPING, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsMinLinearDampingEdit, L"線速度の最小減衰。");
	AddTooltip(m_physicsMinAngularDampingEdit, L"角速度の最小減衰。");
	y += rowH;

	label = CreateLabel(L"最大逆慣性:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"回転のしやすさ上限。大きいほど軽く回る。");
	m_physicsMaxInvInertiaEdit = CreateEdit(ID_PHYS_MAX_INV_INERTIA, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsMaxInvInertiaEdit, L"回転のしやすさ上限。大きいほど軽く回る。");
	y += rowH;

	label = CreateLabel(L"スリープ速度(線/角):", xPadding, y, physicsLabelW);
	AddTooltip(label, L"これ以下の速度で休止判定。0で無効。");
	m_physicsSleepLinearSpeedEdit = CreateEdit(ID_PHYS_SLEEP_LINEAR_SPEED, xPadding + physicsLabelW, y, physicsEditW);
	m_physicsSleepAngularSpeedEdit = CreateEdit(ID_PHYS_SLEEP_ANGULAR_SPEED, xPadding + physicsLabelW + physicsEditW + 5, y, physicsEditW);
	AddTooltip(m_physicsSleepLinearSpeedEdit, L"線速度のスリープ閾値。0で無効。");
	AddTooltip(m_physicsSleepAngularSpeedEdit, L"角速度のスリープ閾値。0で無効。");
	y += rowH;

	label = CreateLabel(L"最大逆質量:", xPadding, y, physicsLabelW);
	AddTooltip(label, L"質量の上限(逆数)。0で制限なし。");
	m_physicsMaxInvMassEdit = CreateEdit(ID_PHYS_MAX_INV_MASS, xPadding + physicsLabelW, y, physicsEditW);
	AddTooltip(m_physicsMaxInvMassEdit, L"質量の上限(逆数)。0で制限なし。");
	y += rowH + 20;



	// Actions (preset / light)
	{
		const int actionGap = 10;
		const int actionH = 32;

		int resetW = 170;
		int loadW = 170;
		int saveW = sectionW - resetW - loadW - actionGap * 2;

		// Fallback to equal widths if the window is narrower than expected.
		if (saveW < 200)
		{
			const int each = std::max(0, (sectionW - actionGap * 2) / 3);
			resetW = each;
			loadW = each;
			saveW = sectionW - resetW - loadW - actionGap * 2;
		}

		const int xReset = xPadding;
		const int xLoad = xReset + resetW + actionGap;
		const int xSave = xLoad + loadW + actionGap;

		m_resetLightBtn = CreateWindowExW(0, L"BUTTON", L"ライト設定をリセット", WS_CHILD | WS_VISIBLE,
										  xReset, y, resetW, actionH, parent, reinterpret_cast<HMENU>(ID_RESET_LIGHT), m_hInst, nullptr);
		SetModernFont(m_resetLightBtn);
		SetDarkTheme(m_resetLightBtn);

		m_loadPresetBtn = CreateWindowExW(0, L"BUTTON", L"プリセットを読み込む", WS_CHILD | WS_VISIBLE,
										  xLoad, y, loadW, actionH, parent, reinterpret_cast<HMENU>(ID_LOAD_PRESET), m_hInst, nullptr);
		SetModernFont(m_loadPresetBtn);
		SetDarkTheme(m_loadPresetBtn);

		m_savePresetBtn = CreateWindowExW(0, L"BUTTON", L"このモデルの設定を保存", WS_CHILD | WS_VISIBLE,
										  xSave, y, saveW, actionH, parent, reinterpret_cast<HMENU>(ID_SAVE_PRESET), m_hInst, nullptr);
		SetModernFont(m_savePresetBtn);
		SetDarkTheme(m_savePresetBtn);

		y += actionH + 24;
	}

	m_totalContentHeight = y + 18;
	UpdateScrollInfo();
}

void SettingsWindow::UpdateScrollInfo()
{
	if (!m_content) return;

	RECT rc;
	GetClientRect(m_content, &rc);
	const int clientHeight = rc.bottom - rc.top;

	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	si.nMin = 0;
	si.nMax = m_totalContentHeight;
	si.nPage = static_cast<UINT>(clientHeight);
	si.nPos = m_scrollY;

	SetScrollInfo(m_content, SB_VERT, &si, TRUE);
}

void SettingsWindow::OnVScroll(WPARAM wParam)
{
	if (!m_content) return;

	const int action = LOWORD(wParam);
	int newY = m_scrollY;

	RECT rc;
	GetClientRect(m_content, &rc);
	const int page = rc.bottom - rc.top;
	const int maxPos = std::max(0, m_totalContentHeight - page);

	switch (action)
	{
		case SB_TOP:      newY = 0; break;
		case SB_BOTTOM:   newY = maxPos; break;
		case SB_LINEUP:   newY -= 40; break;
		case SB_LINEDOWN: newY += 40; break;
		case SB_PAGEUP:   newY -= (page - 40); break;
		case SB_PAGEDOWN: newY += (page - 40); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION:
		{
			SCROLLINFO si{};
			si.cbSize = sizeof(si);
			si.fMask = SIF_TRACKPOS;
			GetScrollInfo(m_content, SB_VERT, &si);
			newY = si.nTrackPos;
			break;
		}
		default: return;
	}

	ScrollTo(newY);
}

void SettingsWindow::OnMouseWheel(int delta)
{
	if (!m_content) return;
	const int scrollAmount = -(delta / WHEEL_DELTA) * 60;
	ScrollTo(m_scrollY + scrollAmount);
}

void SettingsWindow::ScrollTo(int targetY)
{
	if (!m_content) return;

	RECT rc;
	GetClientRect(m_content, &rc);
	const int page = rc.bottom - rc.top;
	const int maxPos = std::max(0, m_totalContentHeight - page);

	int newY = targetY;
	if (newY < 0) newY = 0;
	if (newY > maxPos) newY = maxPos;

	if (newY != m_scrollY)
	{
		ScrollWindowEx(m_content, 0, m_scrollY - newY, nullptr, nullptr, nullptr, nullptr,
					   SW_INVALIDATE | SW_SCROLLCHILDREN | SW_ERASE);
		m_scrollY = newY;
	}
	UpdateScrollInfo();
	UpdateNavHighlightFromScroll();
	UpdateWindow(m_content);
}


void SettingsWindow::UpdateNavHighlightFromScroll()
{
	if (!m_tabs || !m_content) return;

	// Determine which section the user is most likely looking at.
	// Use a focus point slightly below the top of the viewport to avoid rapid flipping near boundaries.
	RECT rc{};
	GetClientRect(m_content, &rc);
	const int page = (rc.bottom - rc.top);
	const int focusOffset = std::min(120, std::max(24, page / 4));
	const int focusY = m_scrollY + focusOffset;

	int idx = 0;
	if (focusY >= m_sectionYPhysics) idx = 3;
	else if (focusY >= m_sectionYToon) idx = 2;
	else if (focusY >= m_sectionYLight) idx = 1;
	else idx = 0;

	if (idx != m_lastAutoNavIndex)
	{
		m_lastAutoNavIndex = idx;
		SendMessageW(m_tabs, SEGMSG_SETSEL, static_cast<WPARAM>(idx), 0);
	}
}

void SettingsWindow::LoadCurrentSettings()
{
	const auto& settings = m_app.Settings();
	const auto& light = settings.light;

	SetWindowTextW(m_modelPathEdit, settings.modelPath.wstring().c_str());
	SendMessageW(m_topmostCheck, BM_SETCHECK, settings.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_unlimitedFpsCheck, BM_SETCHECK, settings.unlimitedFps ? BST_CHECKED : BST_UNCHECKED, 0);
	std::wostringstream fpsStr; fpsStr << settings.targetFps;
	SetWindowTextW(m_fpsEdit, fpsStr.str().c_str());
	SendMessageW(m_fpsSpin, UDM_SETPOS32, 0, settings.targetFps);
	UpdateFpsControlState();

	SendMessageW(m_presetModeCombo, CB_SETCURSEL, static_cast<WPARAM>(settings.globalPresetMode), 0);

	SendMessageW(m_scaleSlider, TBM_SETPOS, TRUE, static_cast<int>(light.modelScale * 100));
	SendMessageW(m_brightnessSlider, TBM_SETPOS, TRUE, static_cast<int>(light.brightness * 100));
	SendMessageW(m_ambientSlider, TBM_SETPOS, TRUE, static_cast<int>(light.ambientStrength * 100));
	SendMessageW(m_globalSatSlider, TBM_SETPOS, TRUE, static_cast<int>(light.globalSaturation * 100));
	SendMessageW(m_keyIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.keyLightIntensity * 100));
	SendMessageW(m_fillIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.fillLightIntensity * 100));

	SendMessageW(m_toonEnableCheck, BM_SETCHECK, light.toonEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_toonContrastSlider, TBM_SETPOS, TRUE, static_cast<int>(light.toonContrast * 100));
	SendMessageW(m_shadowHueSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowHueShiftDeg + 45.0f));
	SendMessageW(m_shadowSatSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowSaturationBoost * 100));
	int rampPos = static_cast<int>((light.shadowRampShift + 0.5f) * 100.0f);
	SendMessageW(m_shadowRampSlider, TBM_SETPOS, TRUE, rampPos);

	SendMessageW(m_shadowDeepThresholdSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepThreshold * 100.0f));
	SendMessageW(m_shadowDeepSoftSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepSoftness * 100.0f));
	SendMessageW(m_shadowDeepMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.shadowDeepMul * 100.0f));
	SendMessageW(m_faceShadowMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.faceShadowMul * 100.0f));
	SendMessageW(m_faceContrastMulSlider, TBM_SETPOS, TRUE, static_cast<int>(light.faceToonContrastMul * 100.0f));

	SendMessageW(m_rimWidthSlider, TBM_SETPOS, TRUE, static_cast<int>(light.rimWidth * 100));
	SendMessageW(m_rimIntensitySlider, TBM_SETPOS, TRUE, static_cast<int>(light.rimIntensity * 100));
	SendMessageW(m_specularStepSlider, TBM_SETPOS, TRUE, static_cast<int>(light.specularStep * 100));

	SendMessageW(m_keyDirXSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirX + 1.0f) * 100));
	SendMessageW(m_keyDirYSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirY + 1.0f) * 100));
	SendMessageW(m_keyDirZSlider, TBM_SETPOS, TRUE, static_cast<int>((light.keyLightDirZ + 1.0f) * 100));
	SendMessageW(m_fillDirXSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirX + 1.0f) * 100));
	SendMessageW(m_fillDirYSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirY + 1.0f) * 100));
	SendMessageW(m_fillDirZSlider, TBM_SETPOS, TRUE, static_cast<int>((light.fillLightDirZ + 1.0f) * 100));

	const auto& physics = settings.physics;
	SetWindowTextW(m_physicsFixedTimeStepEdit, FormatFloatPrec(physics.fixedTimeStep, 5).c_str());
	SetWindowTextW(m_physicsMaxSubStepsEdit, std::to_wstring(physics.maxSubSteps).c_str());
	SetWindowTextW(m_physicsMaxCatchUpStepsEdit, std::to_wstring(physics.maxCatchUpSteps).c_str());
	SetWindowTextW(m_physicsGravityXEdit, FormatFloatPrec(physics.gravity.x, 4).c_str());
	SetWindowTextW(m_physicsGravityYEdit, FormatFloatPrec(physics.gravity.y, 4).c_str());
	SetWindowTextW(m_physicsGravityZEdit, FormatFloatPrec(physics.gravity.z, 4).c_str());
	SetWindowTextW(m_physicsGroundYEdit, FormatFloatPrec(physics.groundY, 4).c_str());
	SetWindowTextW(m_physicsJointComplianceEdit, FormatFloatPrec(physics.jointCompliance, 5).c_str());
	SetWindowTextW(m_physicsContactComplianceEdit, FormatFloatPrec(physics.contactCompliance, 5).c_str());
	SetWindowTextW(m_physicsJointWarmStartEdit, FormatFloatPrec(physics.jointWarmStart, 5).c_str());
	SetWindowTextW(m_physicsPostSolveVelocityBlendEdit, FormatFloatPrec(physics.postSolveVelocityBlend, 4).c_str());
	SetWindowTextW(m_physicsPostSolveAngularBlendEdit, FormatFloatPrec(physics.postSolveAngularVelocityBlend, 4).c_str());
	SetWindowTextW(m_physicsMaxContactAngularCorrectionEdit, FormatFloatPrec(physics.maxContactAngularCorrection, 4).c_str());
	SendMessageW(m_physicsEnableRigidBodyCollisionsCheck, BM_SETCHECK, physics.enableRigidBodyCollisions ? BST_CHECKED : BST_UNCHECKED, 0);
	SetWindowTextW(m_physicsCollisionGroupMaskSemanticsEdit, std::to_wstring(physics.collisionGroupMaskSemantics).c_str());
	SendMessageW(m_physicsCollideJointConnectedBodiesCheck, BM_SETCHECK, physics.collideJointConnectedBodies ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_physicsRespectCollisionGroupsCheck, BM_SETCHECK, physics.respectCollisionGroups ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_physicsRequireAfterPhysicsFlagCheck, BM_SETCHECK, physics.requireAfterPhysicsFlag ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_physicsWritebackFallbackCheck, BM_SETCHECK, physics.writebackFallbackPositionAdjustOnly ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(m_physicsGenerateBodyCollidersCheck, BM_SETCHECK, physics.generateBodyCollidersIfMissing ? BST_CHECKED : BST_UNCHECKED, 0);
	SetWindowTextW(m_physicsMinExistingBodyCollidersEdit, std::to_wstring(physics.minExistingBodyColliders).c_str());
	SetWindowTextW(m_physicsMaxGeneratedBodyCollidersEdit, std::to_wstring(physics.maxGeneratedBodyColliders).c_str());
	SetWindowTextW(m_physicsGeneratedMinBoneLengthEdit, FormatFloatPrec(physics.generatedBodyColliderMinBoneLength, 4).c_str());
	SetWindowTextW(m_physicsGeneratedRadiusRatioEdit, FormatFloatPrec(physics.generatedBodyColliderRadiusRatio, 4).c_str());
	SetWindowTextW(m_physicsGeneratedMinRadiusEdit, FormatFloatPrec(physics.generatedBodyColliderMinRadius, 4).c_str());
	SetWindowTextW(m_physicsGeneratedMaxRadiusEdit, FormatFloatPrec(physics.generatedBodyColliderMaxRadius, 4).c_str());
	SetWindowTextW(m_physicsGeneratedOutlierDistanceFactorEdit, FormatFloatPrec(physics.generatedBodyColliderOutlierDistanceFactor, 4).c_str());
	SetWindowTextW(m_physicsGeneratedFrictionEdit, FormatFloatPrec(physics.generatedBodyColliderFriction, 4).c_str());
	SetWindowTextW(m_physicsGeneratedRestitutionEdit, FormatFloatPrec(physics.generatedBodyColliderRestitution, 4).c_str());
	SetWindowTextW(m_physicsSolverIterationsEdit, std::to_wstring(physics.solverIterations).c_str());
	SetWindowTextW(m_physicsCollisionIterationsEdit, std::to_wstring(physics.collisionIterations).c_str());
	SetWindowTextW(m_physicsCollisionMarginEdit, FormatFloatPrec(physics.collisionMargin, 5).c_str());
	SetWindowTextW(m_physicsPhantomMarginEdit, FormatFloatPrec(physics.phantomMargin, 5).c_str());
	SetWindowTextW(m_physicsContactSlopEdit, FormatFloatPrec(physics.contactSlop, 5).c_str());
	SetWindowTextW(m_physicsCollisionRadiusScaleEdit, FormatFloatPrec(physics.collisionRadiusScale, 4).c_str());
	SetWindowTextW(m_physicsMaxLinearSpeedEdit, FormatFloatPrec(physics.maxLinearSpeed, 3).c_str());
	SetWindowTextW(m_physicsMaxAngularSpeedEdit, FormatFloatPrec(physics.maxAngularSpeed, 3).c_str());
	SetWindowTextW(m_physicsMaxJointPositionCorrectionEdit, FormatFloatPrec(physics.maxJointPositionCorrection, 4).c_str());
	SetWindowTextW(m_physicsMaxJointAngularCorrectionEdit, FormatFloatPrec(physics.maxJointAngularCorrection, 4).c_str());
	SetWindowTextW(m_physicsMaxDepenetrationVelocityEdit, FormatFloatPrec(physics.maxDepenetrationVelocity, 4).c_str());
	SetWindowTextW(m_physicsMaxSpringCorrectionRateEdit, FormatFloatPrec(physics.maxSpringCorrectionRate, 4).c_str());
	SetWindowTextW(m_physicsSpringStiffnessScaleEdit, FormatFloatPrec(physics.springStiffnessScale, 4).c_str());
	SetWindowTextW(m_physicsMinLinearDampingEdit, FormatFloatPrec(physics.minLinearDamping, 4).c_str());
	SetWindowTextW(m_physicsMinAngularDampingEdit, FormatFloatPrec(physics.minAngularDamping, 4).c_str());
	SetWindowTextW(m_physicsMaxInvInertiaEdit, FormatFloatPrec(physics.maxInvInertia, 4).c_str());
	SetWindowTextW(m_physicsSleepLinearSpeedEdit, FormatFloatPrec(physics.sleepLinearSpeed, 4).c_str());
	SetWindowTextW(m_physicsSleepAngularSpeedEdit, FormatFloatPrec(physics.sleepAngularSpeed, 4).c_str());
	SetWindowTextW(m_physicsMaxInvMassEdit, FormatFloatPrec(physics.maxInvMass, 4).c_str());

	UpdateLightPreview();
	InvalidateRect(m_hwnd, nullptr, TRUE);
}

void SettingsWindow::UpdateLightPreview()
{
	auto& light = m_app.LightSettingsRef();

	auto GetVal = [&](HWND s) { return static_cast<float>(SendMessageW(s, TBM_GETPOS, 0, 0)); };

	light.modelScale = GetVal(m_scaleSlider) / 100.0f;
	light.brightness = GetVal(m_brightnessSlider) / 100.0f;
	light.ambientStrength = GetVal(m_ambientSlider) / 100.0f;
	light.globalSaturation = GetVal(m_globalSatSlider) / 100.0f;
	light.keyLightIntensity = GetVal(m_keyIntensitySlider) / 100.0f;
	light.fillLightIntensity = GetVal(m_fillIntensitySlider) / 100.0f;

	light.toonEnabled = (SendMessageW(m_toonEnableCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	light.toonContrast = GetVal(m_toonContrastSlider) / 100.0f;
	light.shadowHueShiftDeg = GetVal(m_shadowHueSlider) - 45.0f;
	light.shadowSaturationBoost = GetVal(m_shadowSatSlider) / 100.0f;
	light.shadowRampShift = (GetVal(m_shadowRampSlider) / 100.0f) - 0.5f;

	light.shadowDeepThreshold = GetVal(m_shadowDeepThresholdSlider) / 100.0f;
	light.shadowDeepSoftness = GetVal(m_shadowDeepSoftSlider) / 100.0f;
	light.shadowDeepMul = GetVal(m_shadowDeepMulSlider) / 100.0f;
	light.faceShadowMul = GetVal(m_faceShadowMulSlider) / 100.0f;
	light.faceToonContrastMul = GetVal(m_faceContrastMulSlider) / 100.0f;

	light.rimWidth = GetVal(m_rimWidthSlider) / 100.0f;
	light.rimIntensity = GetVal(m_rimIntensitySlider) / 100.0f;
	light.specularStep = GetVal(m_specularStepSlider) / 100.0f;

	light.keyLightDirX = GetVal(m_keyDirXSlider) / 100.0f - 1.0f;
	light.keyLightDirY = GetVal(m_keyDirYSlider) / 100.0f - 1.0f;
	light.keyLightDirZ = GetVal(m_keyDirZSlider) / 100.0f - 1.0f;
	light.fillLightDirX = GetVal(m_fillDirXSlider) / 100.0f - 1.0f;
	light.fillLightDirY = GetVal(m_fillDirYSlider) / 100.0f - 1.0f;
	light.fillLightDirZ = GetVal(m_fillDirZSlider) / 100.0f - 1.0f;

	SetWindowTextW(m_scaleLabel, FormatFloat(light.modelScale).c_str());
	SetWindowTextW(m_brightnessLabel, FormatFloat(light.brightness).c_str());
	SetWindowTextW(m_ambientLabel, FormatFloat(light.ambientStrength).c_str());
	SetWindowTextW(m_globalSatLabel, FormatFloat(light.globalSaturation).c_str());
	SetWindowTextW(m_keyIntensityLabel, FormatFloat(light.keyLightIntensity).c_str());
	SetWindowTextW(m_fillIntensityLabel, FormatFloat(light.fillLightIntensity).c_str());
	SetWindowTextW(m_toonContrastLabel, FormatFloat(light.toonContrast).c_str());
	SetWindowTextW(m_shadowHueLabel, FormatFloat(light.shadowHueShiftDeg).c_str());
	SetWindowTextW(m_shadowSatLabel, FormatFloat(light.shadowSaturationBoost).c_str());
	SetWindowTextW(m_shadowRampLabel, FormatFloat(light.shadowRampShift).c_str());
	SetWindowTextW(m_rimWidthLabel, FormatFloat(light.rimWidth).c_str());
	SetWindowTextW(m_rimIntensityLabel, FormatFloat(light.rimIntensity).c_str());
	SetWindowTextW(m_specularStepLabel, FormatFloat(light.specularStep).c_str());

	SetWindowTextW(m_shadowDeepThresholdLabel, FormatFloat(light.shadowDeepThreshold).c_str());
	SetWindowTextW(m_shadowDeepSoftLabel, FormatFloat(light.shadowDeepSoftness).c_str());
	SetWindowTextW(m_shadowDeepMulLabel, FormatFloat(light.shadowDeepMul).c_str());
	SetWindowTextW(m_faceShadowMulLabel, FormatFloat(light.faceShadowMul).c_str());
	SetWindowTextW(m_faceContrastMulLabel, FormatFloat(light.faceToonContrastMul).c_str());

	m_app.ApplyLightSettings();
}

void SettingsWindow::PickColor(float& r, float& g, float& b, HWND)
{
	CHOOSECOLORW cc{};
	cc.lStructSize = sizeof(cc);
	cc.hwndOwner = m_hwnd;
	cc.lpCustColors = m_customColors;
	cc.rgbResult = FloatToColorRef(r, g, b);
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	if (ChooseColorW(&cc))
	{
		r = GetRValue(cc.rgbResult) / 255.0f;
		g = GetGValue(cc.rgbResult) / 255.0f;
		b = GetBValue(cc.rgbResult) / 255.0f;

		m_app.ApplyLightSettings();
		InvalidateRect(m_hwnd, nullptr, TRUE);
	}
}

void SettingsWindow::ApplyAndSave()
{
	AppSettings newSettings = m_app.Settings();

	wchar_t buf[MAX_PATH]{};
	GetWindowTextW(m_modelPathEdit, buf, MAX_PATH);
	newSettings.modelPath = buf;

	newSettings.alwaysOnTop = (SendMessageW(m_topmostCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	newSettings.unlimitedFps = (SendMessageW(m_unlimitedFpsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

	int fps = GetEditBoxInt(m_fpsEdit, newSettings.targetFps);
	if (fps < 10) fps = 10;
	if (fps > 240) fps = 240;
	newSettings.targetFps = fps;

	// PresetMode
	int sel = (int)SendMessageW(m_presetModeCombo, CB_GETCURSEL, 0, 0);
	if (sel >= 0) newSettings.globalPresetMode = static_cast<PresetMode>(sel);

	newSettings.light = m_app.LightSettingsRef();
	auto& physics = newSettings.physics;

	physics.fixedTimeStep = std::max(0.0001f, GetEditBoxFloat(m_physicsFixedTimeStepEdit, physics.fixedTimeStep));
	physics.maxSubSteps = std::max(1, GetEditBoxInt(m_physicsMaxSubStepsEdit, physics.maxSubSteps));
	physics.maxCatchUpSteps = std::max(0, GetEditBoxInt(m_physicsMaxCatchUpStepsEdit, physics.maxCatchUpSteps));
	physics.gravity.x = GetEditBoxFloat(m_physicsGravityXEdit, physics.gravity.x);
	physics.gravity.y = GetEditBoxFloat(m_physicsGravityYEdit, physics.gravity.y);
	physics.gravity.z = GetEditBoxFloat(m_physicsGravityZEdit, physics.gravity.z);
	physics.groundY = GetEditBoxFloat(m_physicsGroundYEdit, physics.groundY);
	physics.jointCompliance = std::max(0.0f, GetEditBoxFloat(m_physicsJointComplianceEdit, physics.jointCompliance));
	physics.contactCompliance = std::max(0.0f, GetEditBoxFloat(m_physicsContactComplianceEdit, physics.contactCompliance));
	physics.jointWarmStart = std::max(0.0f, GetEditBoxFloat(m_physicsJointWarmStartEdit, physics.jointWarmStart));
	physics.postSolveVelocityBlend = std::max(0.0f, GetEditBoxFloat(m_physicsPostSolveVelocityBlendEdit, physics.postSolveVelocityBlend));
	physics.postSolveAngularVelocityBlend = std::max(0.0f, GetEditBoxFloat(m_physicsPostSolveAngularBlendEdit, physics.postSolveAngularVelocityBlend));
	physics.maxContactAngularCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxContactAngularCorrectionEdit, physics.maxContactAngularCorrection));
	physics.enableRigidBodyCollisions = (SendMessageW(m_physicsEnableRigidBodyCollisionsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.collisionGroupMaskSemantics = GetEditBoxInt(m_physicsCollisionGroupMaskSemanticsEdit, physics.collisionGroupMaskSemantics);
	physics.collideJointConnectedBodies = (SendMessageW(m_physicsCollideJointConnectedBodiesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.respectCollisionGroups = (SendMessageW(m_physicsRespectCollisionGroupsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.requireAfterPhysicsFlag = (SendMessageW(m_physicsRequireAfterPhysicsFlagCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.writebackFallbackPositionAdjustOnly = (SendMessageW(m_physicsWritebackFallbackCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.generateBodyCollidersIfMissing = (SendMessageW(m_physicsGenerateBodyCollidersCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.minExistingBodyColliders = std::max(0, GetEditBoxInt(m_physicsMinExistingBodyCollidersEdit, physics.minExistingBodyColliders));
	physics.maxGeneratedBodyColliders = std::max(0, GetEditBoxInt(m_physicsMaxGeneratedBodyCollidersEdit, physics.maxGeneratedBodyColliders));
	physics.generatedBodyColliderMinBoneLength = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMinBoneLengthEdit, physics.generatedBodyColliderMinBoneLength));
	physics.generatedBodyColliderRadiusRatio = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedRadiusRatioEdit, physics.generatedBodyColliderRadiusRatio));
	physics.generatedBodyColliderMinRadius = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMinRadiusEdit, physics.generatedBodyColliderMinRadius));
	physics.generatedBodyColliderMaxRadius = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMaxRadiusEdit, physics.generatedBodyColliderMaxRadius));
	physics.generatedBodyColliderOutlierDistanceFactor = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedOutlierDistanceFactorEdit, physics.generatedBodyColliderOutlierDistanceFactor));
	physics.generatedBodyColliderFriction = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedFrictionEdit, physics.generatedBodyColliderFriction));
	physics.generatedBodyColliderRestitution = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedRestitutionEdit, physics.generatedBodyColliderRestitution));
	physics.solverIterations = std::max(0, GetEditBoxInt(m_physicsSolverIterationsEdit, physics.solverIterations));
	physics.collisionIterations = std::max(0, GetEditBoxInt(m_physicsCollisionIterationsEdit, physics.collisionIterations));
	physics.collisionMargin = std::max(0.0f, GetEditBoxFloat(m_physicsCollisionMarginEdit, physics.collisionMargin));
	physics.phantomMargin = std::max(0.0f, GetEditBoxFloat(m_physicsPhantomMarginEdit, physics.phantomMargin));
	physics.contactSlop = std::max(0.0f, GetEditBoxFloat(m_physicsContactSlopEdit, physics.contactSlop));
	physics.collisionRadiusScale = std::max(0.0f, GetEditBoxFloat(m_physicsCollisionRadiusScaleEdit, physics.collisionRadiusScale));
	physics.maxLinearSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsMaxLinearSpeedEdit, physics.maxLinearSpeed));
	physics.maxAngularSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsMaxAngularSpeedEdit, physics.maxAngularSpeed));
	physics.maxJointPositionCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxJointPositionCorrectionEdit, physics.maxJointPositionCorrection));
	physics.maxJointAngularCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxJointAngularCorrectionEdit, physics.maxJointAngularCorrection));
	physics.maxDepenetrationVelocity = std::max(0.0f, GetEditBoxFloat(m_physicsMaxDepenetrationVelocityEdit, physics.maxDepenetrationVelocity));
	physics.maxSpringCorrectionRate = std::max(0.0f, GetEditBoxFloat(m_physicsMaxSpringCorrectionRateEdit, physics.maxSpringCorrectionRate));
	physics.springStiffnessScale = std::max(0.0f, GetEditBoxFloat(m_physicsSpringStiffnessScaleEdit, physics.springStiffnessScale));
	physics.minLinearDamping = std::max(0.0f, GetEditBoxFloat(m_physicsMinLinearDampingEdit, physics.minLinearDamping));
	physics.minAngularDamping = std::max(0.0f, GetEditBoxFloat(m_physicsMinAngularDampingEdit, physics.minAngularDamping));
	physics.maxInvInertia = std::max(0.0f, GetEditBoxFloat(m_physicsMaxInvInertiaEdit, physics.maxInvInertia));
	physics.sleepLinearSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsSleepLinearSpeedEdit, physics.sleepLinearSpeed));
	physics.sleepAngularSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsSleepAngularSpeedEdit, physics.sleepAngularSpeed));
	physics.maxInvMass = std::max(0.0f, GetEditBoxFloat(m_physicsMaxInvMassEdit, physics.maxInvMass));


	m_app.ApplySettings(newSettings, true);
	m_backupSettings = m_app.Settings();
}

void SettingsWindow::UpdateFpsControlState()
{
	const bool unlimited = (SendMessageW(m_unlimitedFpsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	EnableWindow(m_fpsEdit, !unlimited);
	EnableWindow(m_fpsSpin, !unlimited);
}

bool SettingsWindow::HasUnsavedChanges() const
{
	if (!m_hwnd) return false;

	// UIの現在値をAppSettingsに再構築して、起動時(Show時)のバックアップと比較する。
	AppSettings current = m_backupSettings;

	wchar_t buf[MAX_PATH]{};
	GetWindowTextW(m_modelPathEdit, buf, MAX_PATH);
	current.modelPath = buf;

	current.alwaysOnTop = (SendMessageW(m_topmostCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	current.unlimitedFps = (SendMessageW(m_unlimitedFpsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

	int fps = GetEditBoxInt(m_fpsEdit, current.targetFps);
	fps = std::clamp(fps, 10, 240);
	current.targetFps = fps;

	int sel = (int)SendMessageW(m_presetModeCombo, CB_GETCURSEL, 0, 0);
	if (sel >= 0) current.globalPresetMode = static_cast<PresetMode>(sel);

	// ライト設定はプレビュー更新でリアルタイムに反映されるため、現在の参照を採用
	current.light = m_app.LightSettingsRef();

	// 物理設定はUIから読み戻す
	auto& physics = current.physics;
	physics.fixedTimeStep = std::max(0.0001f, GetEditBoxFloat(m_physicsFixedTimeStepEdit, physics.fixedTimeStep));
	physics.maxSubSteps = std::max(1, GetEditBoxInt(m_physicsMaxSubStepsEdit, physics.maxSubSteps));
	physics.maxCatchUpSteps = std::max(0, GetEditBoxInt(m_physicsMaxCatchUpStepsEdit, physics.maxCatchUpSteps));
	physics.gravity.x = GetEditBoxFloat(m_physicsGravityXEdit, physics.gravity.x);
	physics.gravity.y = GetEditBoxFloat(m_physicsGravityYEdit, physics.gravity.y);
	physics.gravity.z = GetEditBoxFloat(m_physicsGravityZEdit, physics.gravity.z);
	physics.groundY = GetEditBoxFloat(m_physicsGroundYEdit, physics.groundY);
	physics.jointCompliance = std::max(0.0f, GetEditBoxFloat(m_physicsJointComplianceEdit, physics.jointCompliance));
	physics.contactCompliance = std::max(0.0f, GetEditBoxFloat(m_physicsContactComplianceEdit, physics.contactCompliance));
	physics.jointWarmStart = std::max(0.0f, GetEditBoxFloat(m_physicsJointWarmStartEdit, physics.jointWarmStart));
	physics.postSolveVelocityBlend = std::max(0.0f, GetEditBoxFloat(m_physicsPostSolveVelocityBlendEdit, physics.postSolveVelocityBlend));
	physics.postSolveAngularVelocityBlend = std::max(0.0f, GetEditBoxFloat(m_physicsPostSolveAngularBlendEdit, physics.postSolveAngularVelocityBlend));
	physics.maxContactAngularCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxContactAngularCorrectionEdit, physics.maxContactAngularCorrection));
	physics.enableRigidBodyCollisions = (SendMessageW(m_physicsEnableRigidBodyCollisionsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.collisionGroupMaskSemantics = GetEditBoxInt(m_physicsCollisionGroupMaskSemanticsEdit, physics.collisionGroupMaskSemantics);
	physics.collideJointConnectedBodies = (SendMessageW(m_physicsCollideJointConnectedBodiesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.respectCollisionGroups = (SendMessageW(m_physicsRespectCollisionGroupsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.requireAfterPhysicsFlag = (SendMessageW(m_physicsRequireAfterPhysicsFlagCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.writebackFallbackPositionAdjustOnly = (SendMessageW(m_physicsWritebackFallbackCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.generateBodyCollidersIfMissing = (SendMessageW(m_physicsGenerateBodyCollidersCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
	physics.minExistingBodyColliders = std::max(0, GetEditBoxInt(m_physicsMinExistingBodyCollidersEdit, physics.minExistingBodyColliders));
	physics.maxGeneratedBodyColliders = std::max(0, GetEditBoxInt(m_physicsMaxGeneratedBodyCollidersEdit, physics.maxGeneratedBodyColliders));
	physics.generatedBodyColliderMinBoneLength = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMinBoneLengthEdit, physics.generatedBodyColliderMinBoneLength));
	physics.generatedBodyColliderRadiusRatio = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedRadiusRatioEdit, physics.generatedBodyColliderRadiusRatio));
	physics.generatedBodyColliderMinRadius = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMinRadiusEdit, physics.generatedBodyColliderMinRadius));
	physics.generatedBodyColliderMaxRadius = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedMaxRadiusEdit, physics.generatedBodyColliderMaxRadius));
	physics.generatedBodyColliderOutlierDistanceFactor = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedOutlierDistanceFactorEdit, physics.generatedBodyColliderOutlierDistanceFactor));
	physics.generatedBodyColliderFriction = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedFrictionEdit, physics.generatedBodyColliderFriction));
	physics.generatedBodyColliderRestitution = std::max(0.0f, GetEditBoxFloat(m_physicsGeneratedRestitutionEdit, physics.generatedBodyColliderRestitution));
	physics.solverIterations = std::max(0, GetEditBoxInt(m_physicsSolverIterationsEdit, physics.solverIterations));
	physics.collisionIterations = std::max(0, GetEditBoxInt(m_physicsCollisionIterationsEdit, physics.collisionIterations));
	physics.collisionMargin = std::max(0.0f, GetEditBoxFloat(m_physicsCollisionMarginEdit, physics.collisionMargin));
	physics.phantomMargin = std::max(0.0f, GetEditBoxFloat(m_physicsPhantomMarginEdit, physics.phantomMargin));
	physics.contactSlop = std::max(0.0f, GetEditBoxFloat(m_physicsContactSlopEdit, physics.contactSlop));
	physics.collisionRadiusScale = std::max(0.0f, GetEditBoxFloat(m_physicsCollisionRadiusScaleEdit, physics.collisionRadiusScale));
	physics.maxLinearSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsMaxLinearSpeedEdit, physics.maxLinearSpeed));
	physics.maxAngularSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsMaxAngularSpeedEdit, physics.maxAngularSpeed));
	physics.maxJointPositionCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxJointPositionCorrectionEdit, physics.maxJointPositionCorrection));
	physics.maxJointAngularCorrection = std::max(0.0f, GetEditBoxFloat(m_physicsMaxJointAngularCorrectionEdit, physics.maxJointAngularCorrection));
	physics.maxDepenetrationVelocity = std::max(0.0f, GetEditBoxFloat(m_physicsMaxDepenetrationVelocityEdit, physics.maxDepenetrationVelocity));
	physics.maxSpringCorrectionRate = std::max(0.0f, GetEditBoxFloat(m_physicsMaxSpringCorrectionRateEdit, physics.maxSpringCorrectionRate));
	physics.springStiffnessScale = std::max(0.0f, GetEditBoxFloat(m_physicsSpringStiffnessScaleEdit, physics.springStiffnessScale));
	physics.minLinearDamping = std::max(0.0f, GetEditBoxFloat(m_physicsMinLinearDampingEdit, physics.minLinearDamping));
	physics.minAngularDamping = std::max(0.0f, GetEditBoxFloat(m_physicsMinAngularDampingEdit, physics.minAngularDamping));
	physics.maxInvInertia = std::max(0.0f, GetEditBoxFloat(m_physicsMaxInvInertiaEdit, physics.maxInvInertia));
	physics.sleepLinearSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsSleepLinearSpeedEdit, physics.sleepLinearSpeed));
	physics.sleepAngularSpeed = std::max(0.0f, GetEditBoxFloat(m_physicsSleepAngularSpeedEdit, physics.sleepAngularSpeed));
	physics.maxInvMass = std::max(0.0f, GetEditBoxFloat(m_physicsMaxInvMassEdit, physics.maxInvMass));

	const AppSettings& base = m_backupSettings;
	if (current.modelPath != base.modelPath) return true;
	if (current.alwaysOnTop != base.alwaysOnTop) return true;
	if (current.unlimitedFps != base.unlimitedFps) return true;
	if (current.targetFps != base.targetFps) return true;
	if (static_cast<int>(current.globalPresetMode) != static_cast<int>(base.globalPresetMode)) return true;
	if (!LightSettingsEqual(current.light, base.light)) return true;
	if (!PhysicsSettingsEqual(current.physics, base.physics)) return true;

	return false;
}

LRESULT CALLBACK SettingsWindow::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SettingsWindow* self = nullptr;
	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<SettingsWindow*>(cs->lpCreateParams);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}
	if (self) return self->WndProc(hWnd, msg, wParam, lParam);
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::ContentProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SettingsWindow* self = nullptr;
	if (msg == WM_NCCREATE)
	{
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<SettingsWindow*>(cs->lpCreateParams);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else
	{
		self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}
	if (self) return self->ContentProc(hWnd, msg, wParam, lParam);
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::ContentProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_VSCROLL:
			OnVScroll(wParam);
			return 0;
		case WM_MOUSEWHEEL:
			OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
			return 0;
		case WM_COMMAND:
		case WM_NOTIFY:
		case WM_HSCROLL:
		case WM_DRAWITEM:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			return SendMessageW(m_hwnd, msg, wParam, lParam);
		default:
			break;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_GETMINMAXINFO:
		{
			// 横幅固定(縦のみリサイズ可)
			auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
			if (mmi && m_fixedWindowWidth > 0)
			{
				mmi->ptMinTrackSize.x = m_fixedWindowWidth;
				mmi->ptMaxTrackSize.x = m_fixedWindowWidth;
			}
			return 0;
		}

		case WM_NCHITTEST:
		{
			// 左右(および角)のリサイズヒットテストを無効化して、縦方向のみ許可
			LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
			switch (hit)
			{
				case HTLEFT:
				case HTRIGHT:
					return HTCLIENT;
				case HTTOPLEFT:
				case HTTOPRIGHT:
					return HTTOP;
				case HTBOTTOMLEFT:
				case HTBOTTOMRIGHT:
					return HTBOTTOM;
				default:
					return hit;
			}
		}

		case WM_APP_NAV_CHANGED:
		{
			const int idx = static_cast<int>(wParam);
			const int pad = 6;
			switch (idx)
			{
				case 0: ScrollTo(m_sectionYBasic - pad); break;
				case 1: ScrollTo(m_sectionYLight - pad); break;
				case 2: ScrollTo(m_sectionYToon - pad); break;
				case 3: ScrollTo(m_sectionYPhysics - pad); break;
				default: break;
			}
			return 0;
		}

		case WM_NOTIFY:
			break;

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, kTextColor);
			SetBkMode(hdc, TRANSPARENT);
			return (LRESULT)m_darkBrush;
		}
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, kTextColor);
			SetBkColor(hdc, kDarkBkColor);
			return (LRESULT)m_darkBrush;
		}

		case WM_VSCROLL:
			OnVScroll(wParam);
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
			return 0;

		case WM_SIZE:
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			const int clientW = rc.right - rc.left;
			const int clientH = rc.bottom - rc.top;

			const int outerPadX = 16;
			const int outerPadY = 10;
			const int tabH = 32;
			m_headerHeight = tabH + outerPadY * 2;
			const int footerTop = std::max(m_headerHeight, clientH - m_footerHeight);

			if (m_tabs)
			{
				SetWindowPos(m_tabs, nullptr, outerPadX, outerPadY, std::max(0, clientW - outerPadX * 2), tabH, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (m_footerDivider)
			{
				SetWindowPos(m_footerDivider, nullptr, 0, footerTop, clientW, 1, SWP_NOZORDER | SWP_NOACTIVATE);
			}

			const int btnW = 92;
			const int btnH = 30;
			const int footerPad = 16;
			const int gap = 10;
			const int footerY = footerTop + (m_footerHeight - btnH) / 2;
			int right = clientW - footerPad;
			int xApply = right - btnW;
			int xCancel = xApply - gap - btnW;
			int xOk = xCancel - gap - btnW;
			if (m_okBtn) SetWindowPos(m_okBtn, nullptr, xOk, footerY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
			if (m_cancelBtn) SetWindowPos(m_cancelBtn, nullptr, xCancel, footerY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
			if (m_applyBtn) SetWindowPos(m_applyBtn, nullptr, xApply, footerY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

			if (m_content)
			{
				const int contentY = m_headerHeight;
				const int contentH = std::max(0, footerTop - contentY);
				SetWindowPos(m_content, nullptr, 0, contentY, clientW, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
			}

			ScrollTo(m_scrollY);
			return 0;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case ID_BROWSE: {
					wchar_t path[MAX_PATH]{};
					OPENFILENAMEW ofn{};
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = hWnd;
					ofn.lpstrFilter = L"PMXモデル (*.pmx)\0*.pmx\0すべてのファイル\0*.*\0";
					ofn.lpstrFile = path;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_FILEMUSTEXIST;
					if (GetOpenFileNameW(&ofn)) SetWindowTextW(m_modelPathEdit, path);
					return 0;
				}
				case ID_KEY_COLOR_BTN: {
					auto& light = m_app.LightSettingsRef();
					PickColor(light.keyLightColorR, light.keyLightColorG, light.keyLightColorB, m_keyColorBtn);
					return 0;
				}
				case ID_FILL_COLOR_BTN: {
					auto& light = m_app.LightSettingsRef();
					PickColor(light.fillLightColorR, light.fillLightColorG, light.fillLightColorB, m_fillColorBtn);
					return 0;
				}
				case ID_TOON_ENABLE: UpdateLightPreview(); return 0;
				case ID_FPS_UNLIMITED: UpdateFpsControlState(); return 0;
				case ID_RESET_LIGHT: {
					LightSettings defaultLight;
					m_app.LightSettingsRef() = defaultLight;
					LoadCurrentSettings();
					m_app.ApplyLightSettings();
					return 0;
				}
				case ID_SAVE_PRESET: {
					const auto& settings = m_app.Settings();
					if (settings.modelPath.empty())
					{
						MessageBoxW(m_hwnd, L"モデルが読み込まれていません。", L"エラー", MB_ICONWARNING);
					}
					else
					{
						SettingsManager::SavePreset(m_app.BaseDir(), settings.modelPath, settings.light, settings.physics);
						MessageBoxW(m_hwnd, L"現在のライト/物理設定をモデル用プリセットとして保存しました。", L"保存完了", MB_ICONINFORMATION);
					}
					return 0;
				}
				case ID_LOAD_PRESET: {
					const auto& settings = m_app.Settings();
					if (settings.modelPath.empty())
					{
						MessageBoxW(m_hwnd, L"モデルが読み込まれていません。", L"エラー", MB_ICONWARNING);
					}
					else
					{
						if (SettingsManager::LoadPreset(m_app.BaseDir(), settings.modelPath, m_app.LightSettingsRef(), m_app.PhysicsSettingsRef()))
						{
							LoadCurrentSettings(); // UI反映
							m_app.ApplyLightSettings();
							m_app.ApplyPhysicsSettings();
							MessageBoxW(m_hwnd, L"プリセットを読み込みました。", L"完了", MB_ICONINFORMATION);
						}
						else
						{
							MessageBoxW(m_hwnd, L"このモデル用のプリセットが見つかりません。", L"エラー", MB_ICONWARNING);
						}
					}
					return 0;
				}
				case ID_OK: ApplyAndSave(); Hide(); return 0;
				case ID_CANCEL: m_app.ApplySettings(m_backupSettings, false); LoadCurrentSettings(); Hide(); return 0;
				case ID_APPLY: ApplyAndSave(); return 0;
			}
			break;

		case WM_HSCROLL:
			UpdateLightPreview();
			return 0;

		case WM_DRAWITEM: {
			auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
			if (dis->hwndItem == m_keyColorPreview || dis->hwndItem == m_fillColorPreview)
			{
				FillRect(dis->hDC, &dis->rcItem, m_darkBrush);
				RECT rc = dis->rcItem; InflateRect(&rc, -2, -2);
				HBRUSH brush = nullptr;
				auto& light = m_app.LightSettingsRef();
				if (dis->hwndItem == m_keyColorPreview) brush = CreateSolidBrush(FloatToColorRef(light.keyLightColorR, light.keyLightColorG, light.keyLightColorB));
				else brush = CreateSolidBrush(FloatToColorRef(light.fillLightColorR, light.fillLightColorG, light.fillLightColorB));
				FillRect(dis->hDC, &rc, brush);
				DeleteObject(brush);
				HBRUSH frameBrush = CreateSolidBrush(RGB(100, 100, 100));
				FrameRect(dis->hDC, &rc, frameBrush);
				DeleteObject(frameBrush);
				return TRUE;
			}
			break;
		}

		case WM_CLOSE:
		{
			if (!HasUnsavedChanges())
			{
				Hide();
				return 0;
			}

			const int result = MessageBoxW(
				m_hwnd,
				L"設定を適用して閉じますか？",
				L"確認",
				MB_YESNOCANCEL | MB_ICONQUESTION);

			if (result == IDYES)
			{
				ApplyAndSave();
				Hide();
				return 0;
			}
			if (result == IDNO)
			{
				m_app.ApplySettings(m_backupSettings, false);
				LoadCurrentSettings();
				Hide();
				return 0;
			}
			return 0;
		}


		case WM_THEMECHANGED:
		case WM_SETTINGCHANGE:
		{
			if (auto v = reinterpret_cast<UINT_PTR>(GetPropW(hWnd, kPropIgnoreThemeCount)); v)
			{
				if (v <= 1) RemovePropW(hWnd, kPropIgnoreThemeCount);
				else SetPropW(hWnd, kPropIgnoreThemeCount, reinterpret_cast<HANDLE>(v - 1));
				return DefWindowProcW(hWnd, msg, wParam, lParam);
			}

			const LRESULT r = DefWindowProcW(hWnd, msg, wParam, lParam);

			// Real OS theme change: allow re-applying DarkMode_Explorer once.
			if (m_content) RemovePropW(m_content, kPropThemeApplied);

			ScheduleDarkScrollbarsRefresh(hWnd);
			RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
			return r;
		}

		case WM_APP_REFRESH_DARKSCROLLBARS:
		{
			RemovePropW(hWnd, kPropThemePending);
			if (m_content) EnableDarkScrollbarsBestEffort(m_content);

			RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
			return 0;
		}

		default: break;
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}
