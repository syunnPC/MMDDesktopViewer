Texture2D g_src : register(t0);
SamplerState g_samp : register(s0);

cbuffer FxaaCB : register(b0)
{
    float2 g_invScreenSize; // (1/width, 1/height)
    float2 _pad;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// FXAA 3.11 Quality Settings
#define FXAA_QUALITY__PRESET 12
#define FXAA_QUALITY__PS 5
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 4.0
#define FXAA_QUALITY__P6 8.0

// Tuning parameters
#define FXAA_EDGE_THRESHOLD      0.166
#define FXAA_EDGE_THRESHOLD_MIN  0.0833

float Luma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

float4 PSMain(PSIn i) : SV_TARGET
{
    float2 rcpFrame = g_invScreenSize;
    float2 uv = i.uv;

    float3 rgbN = g_src.SampleLevel(g_samp, uv + float2(0, -rcpFrame.y), 0).rgb;
    float3 rgbW = g_src.SampleLevel(g_samp, uv + float2(-rcpFrame.x, 0), 0).rgb;
    // 中心ピクセルの取得（アルファ値も必要なので float4 で取得）
    float4 rgbaM = g_src.SampleLevel(g_samp, uv, 0);
    float3 rgbM = rgbaM.rgb;

    float3 rgbE = g_src.SampleLevel(g_samp, uv + float2(rcpFrame.x, 0), 0).rgb;
    float3 rgbS = g_src.SampleLevel(g_samp, uv + float2(0, rcpFrame.y), 0).rgb;

    float lumaN = Luma(rgbN);
    float lumaW = Luma(rgbW);
    float lumaM = Luma(rgbM);
    float lumaE = Luma(rgbE);
    float lumaS = Luma(rgbS);

    float rangeMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
    float range = rangeMax - rangeMin;

    if (range < max(FXAA_EDGE_THRESHOLD_MIN, rangeMax * FXAA_EDGE_THRESHOLD))
    {
        // エッジでない場合は中心ピクセルをそのまま返す（アルファ値含む）
        return rgbaM;
    }

    float3 rgbL = rgbN + rgbW + rgbM + rgbE + rgbS;
    float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25;
    float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0, (rangeL / range) - 0.5);

    float lumaNW = Luma(g_src.SampleLevel(g_samp, uv + float2(-rcpFrame.x, -rcpFrame.y), 0).rgb);
    float lumaNE = Luma(g_src.SampleLevel(g_samp, uv + float2(rcpFrame.x, -rcpFrame.y), 0).rgb);
    float lumaSW = Luma(g_src.SampleLevel(g_samp, uv + float2(-rcpFrame.x, rcpFrame.y), 0).rgb);
    float lumaSE = Luma(g_src.SampleLevel(g_samp, uv + float2(rcpFrame.x, rcpFrame.y), 0).rgb);

    float edgeVert =
        abs((0.25 * lumaNW) + (-0.5 * lumaN) + (0.25 * lumaNE)) +
        abs((0.50 * lumaW) + (-1.0 * lumaM) + (0.50 * lumaE)) +
        abs((0.25 * lumaSW) + (-0.5 * lumaS) + (0.25 * lumaSE));
    float edgeHorz =
        abs((0.25 * lumaNW) + (-0.5 * lumaW) + (0.25 * lumaSW)) +
        abs((0.50 * lumaN) + (-1.0 * lumaM) + (0.50 * lumaS)) +
        abs((0.25 * lumaNE) + (-0.5 * lumaE) + (0.25 * lumaSE));

    bool horzSpan = edgeHorz >= edgeVert;
    float lengthSign = horzSpan ? -rcpFrame.y : -rcpFrame.x;
    if (!horzSpan)
        lumaN = lumaW;
    if (!horzSpan)
        lumaS = lumaE;

    float gradientN = abs(lumaN - lumaM);
    float gradientS = abs(lumaS - lumaM);
    lumaN = (lumaN + lumaM) * 0.5;
    lumaS = (lumaS + lumaM) * 0.5;

    bool pairN = gradientN >= gradientS;
    if (!pairN)
        lumaN = lumaS;
    if (!pairN)
        gradientN = gradientS;
    if (!pairN)
        lengthSign *= -1.0;

    float2 posN;
    posN.x = uv.x + (horzSpan ? 0.0 : lengthSign * 0.5);
    posN.y = uv.y + (horzSpan ? lengthSign * 0.5 : 0.0);

    gradientN *= 0.25;

    float2 posP = posN;
    float2 offNP = horzSpan ? float2(rcpFrame.x, 0.0) : float2(0.0, rcpFrame.y);

    float lumaEndN = lumaN;
    float lumaEndP = lumaN;
    bool doneN = false;
    bool doneP = false;

    posN -= offNP * 1.0;
    posP += offNP * 1.0;

    for (int i = 0; i < FXAA_QUALITY__PS; i++)
    {
        if (!doneN)
            lumaEndN = Luma(g_src.SampleLevel(g_samp, posN, 0).rgb) - lumaN;
        if (!doneP)
            lumaEndP = Luma(g_src.SampleLevel(g_samp, posP, 0).rgb) - lumaN;

        doneN = abs(lumaEndN) >= gradientN;
        doneP = abs(lumaEndP) >= gradientN;

        if (doneN && doneP)
            break;
        if (!doneN)
            posN -= offNP * (i == 0 ? FXAA_QUALITY__P0 : (i == 1 ? FXAA_QUALITY__P1 : FXAA_QUALITY__P2));
        if (!doneP)
            posP += offNP * (i == 0 ? FXAA_QUALITY__P0 : (i == 1 ? FXAA_QUALITY__P1 : FXAA_QUALITY__P2));
    }

    float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
    float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;

    bool directionN = dstN < dstP;
    lumaEndN = directionN ? lumaEndN : lumaEndP;

    if (((lumaM - lumaN) < 0.0) == (lumaEndN < 0.0))
        lengthSign = 0.0;

    float spanLength = (dstP + dstN);
    dstN = directionN ? dstN : dstP;
    float subPixelOffset = (0.5 + (dstN * (-1.0 / spanLength))) * lengthSign;

    // 最終サンプリング（ここもアルファ値を含めて取得して返す）
    float4 rgbaF = g_src.SampleLevel(g_samp, float2(
        uv.x + (horzSpan ? 0.0 : subPixelOffset),
        uv.y + (horzSpan ? subPixelOffset : 0.0)), 0);
    return rgbaF;
}