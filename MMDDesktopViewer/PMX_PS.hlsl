struct PSIn
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 viewDir : TEXCOORD3;
    float2 uv : TEXCOORD0;
};

cbuffer Scene : register(b0)
{
    float4x4 g_model;
    float4x4 g_view;
    float4x4 g_proj;
    float4x4 g_mvp;

    float3 g_lightDir0;
    float g_ambient;
    float3 g_lightColor0;
    float g_lightInt0;

    float3 g_lightDir1;
    float g_lightInt1;
    float3 g_lightColor1;
    float _pad1;

    float3 g_cameraPos;
    float g_specPower;
    float3 g_specColor;
    float g_specStrength;

    float4 g_normalMatrixRow0;
    float4 g_normalMatrixRow1;
    float4 g_normalMatrixRow2;

    float g_brightness;
    uint g_enableSkinning;
    float g_toonContrast;
    float g_shadowHueShift;

    float g_shadowSaturation;
    float g_rimWidth;
    float g_rimIntensity;
    float g_specularStep;

    uint g_enableToon;
    float g_outlineRefDistance;
    float g_outlineDistanceScale;
    float g_outlineDistancePower;

    float g_shadowRampShift;
    float g_shadowDeepThreshold;
    float g_shadowDeepSoftness;
    float g_shadowDeepMul;
    float g_globalSaturation;
};

cbuffer Material : register(b1)
{
    float4 g_diffuse;
    float3 g_ambientMat;
    float _pad0;
    float3 g_specularMat;
    float g_specPowerMat;

    uint g_sphereMode;
    float g_edgeSize;
    float g_rimMul;
    float g_specMul;

    float4 g_edgeColor;

    uint g_materialType;
    float g_shadowMul;
    float g_toonContrastMul;
    float _pad2;
};

Texture2D g_base : register(t0);
Texture2D g_toon : register(t1);
Texture2D g_sphere : register(t2);
SamplerState g_samp : register(s0);

float3 ApplySaturation(float3 c, float s)
{
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    return lerp(l.xxx, c, s);
}

float3 RgbToHsv(float3 c)
{
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = (c.g < c.b) ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = (c.r < p.x) ? float4(p.xyw, c.r) : float4(c.r, p.yzx);

    float d = q.x - min(q.w, q.y);
    float e = 1e-10;
    float h = abs(q.z + (q.w - q.y) / (6.0 * d + e));
    float s = d / (q.x + e);
    float v = q.x;
    return float3(h, s, v);
}

float3 HsvToRgb(float3 c)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float3 MakeShadowColor(float3 baseLinear)
{
    float3 hsv = RgbToHsv(baseLinear);
    hsv.x = frac(hsv.x + g_shadowHueShift * 0.15915494);
    hsv.y = saturate(hsv.y + g_shadowSaturation);
    hsv.z = hsv.z * 0.8;
    return HsvToRgb(hsv);
}

float StylizeSpecular(float raw, float stepControl)
{
    float edge = saturate(stepControl);
    float width = max(0.05, 0.35 - edge * 0.25);
    float start = saturate(1.0 - width * 2.0);
    float endv = saturate(1.0 - width);
    return smoothstep(start * edge, endv, raw);
}

float3 ApplySphere(float3 base, float3 sphere, uint mode)
{
    // 0: none, 1: multiply, 2: add (想定)
    if (mode == 1)
    {
        return base * sphere;
    }
    else if (mode == 2)
    {
        return base + sphere;
    }
    return base;
}

float4 PSMain(PSIn i) : SV_TARGET
{
    float3 N = normalize(i.worldNormal);
    float3 V = normalize(i.viewDir);

    float4 texColor = g_base.Sample(g_samp, i.uv);
    float4 baseColor = texColor * g_diffuse;
    if (baseColor.a < 0.01)
        discard;

    // テクスチャはsRGB想定で線形化
    float3 albedo = pow(saturate(baseColor.rgb), 2.2);
    float3 shadowColor = MakeShadowColor(albedo);

    float3 L0 = normalize(g_lightDir0);
    float3 L1 = normalize(g_lightDir1);

    float NdotL0 = saturate(dot(N, L0));
    float NdotL1 = saturate(dot(N, L1));

    float toonContrast = g_toonContrast * g_toonContrastMul;

    float shade0 = pow(NdotL0 * 0.5 + 0.5, toonContrast);
    float shade1 = pow(NdotL1 * 0.5 + 0.5, toonContrast * 0.7);

    float shade = saturate(shade0 * g_lightInt0 + shade1 * g_lightInt1 * 0.5);
    float toonCoord = saturate(shade);
    float toonRamp =
        (g_enableToon != 0)
        ? g_toon.Sample(g_samp, float2(toonCoord, 0.5)).r
        : toonCoord;

    toonRamp = saturate(toonRamp - g_shadowRampShift);

    float2 noiseSeed = i.uv * 128.0 + i.worldPos.xy;
    float rnd = frac(sin(dot(noiseSeed, float2(12.9898, 78.233))) * 43758.5453);

    float ditherStrength = 0.02; // 小さめに
    toonRamp = saturate(toonRamp + (rnd - 0.5) * ditherStrength);

    float3 midLit = lerp(shadowColor, albedo, toonRamp);

    float deepT = saturate(g_shadowDeepThreshold);
    float deepSoft = max(0.001, g_shadowDeepSoftness);
    float deepMul = saturate(g_shadowDeepMul);

    float deepMask = 1.0 - smoothstep(deepT - deepSoft, deepT + deepSoft, toonCoord);
    deepMask *= g_shadowMul;
    float3 deepShadow = shadowColor * deepMul;

    float3 litAlbedo = lerp(midLit, deepShadow, deepMask);

    float3 diff =
        litAlbedo * (g_lightColor0 * g_lightInt0 + g_lightColor1 * g_lightInt1 * 0.5);

    float3 ambient = shadowColor * g_ambientMat * (g_ambient + 0.05);

    float3 H0 = normalize(L0 + V);
    float sp = max(g_specPowerMat, 1.0);
    float rawSpec = pow(saturate(dot(N, H0)), sp);
    float specStep = g_specularStep * g_specMul;
    float specBand =
        (g_enableToon != 0) ? StylizeSpecular(rawSpec, specStep) : rawSpec;
    float3 spec = g_specularMat * specBand * 0.18;

    float rim = pow(1.0 - saturate(dot(N, V)), 1.5 + g_rimWidth * 2.0);
    float rimBand = smoothstep(g_rimWidth * 0.35, g_rimWidth, rim);
    float3 rimCol = litAlbedo * g_rimIntensity * rimBand;

    // スフィア（ビュー空間法線からUV生成）
    float3x3 V3 = (float3x3) g_view;
    float3 Nview = normalize(mul(N, V3));
    float2 sphereUV = Nview.xy * 0.5 + 0.5;
    float3 sphereTex = g_sphere.Sample(g_samp, sphereUV).rgb;

    float3 color = diff + ambient + spec + rimCol;
    color = ApplySphere(color, sphereTex, g_sphereMode);

    color *= g_brightness;

    color = ApplySaturation(color, g_globalSaturation);

    color = pow(saturate(color), 1.0 / 2.2);

    return float4(color * baseColor.a, baseColor.a);
}