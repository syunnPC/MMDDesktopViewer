cbuffer SceneCB : register(b0)
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

cbuffer BoneCB : register(b2)
{
    float4x4 g_boneMatrices[1024];
};

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
    int4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
    float3 sdefC : TEXCOORD1;
    float3 sdefR0 : TEXCOORD2;
    float3 sdefR1 : TEXCOORD3;
    uint weightType : TEXCOORD4;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4x4 GetSkinMatrix(int4 indices, float4 weights)
{
    float4x4 skinMat = (float4x4) 0;
    float totalWeight = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (indices[i] >= 0 && indices[i] < 1024 && weights[i] > 0.0)
        {
            skinMat += g_boneMatrices[indices[i]] * weights[i];
            totalWeight += weights[i];
        }
    }
    if (totalWeight < 0.001)
    {
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    }
    return skinMat / totalWeight;
}

float4 QuaternionFromMatrix(float3x3 m)
{
    float4 q;
    float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.0)
    {
        float s = sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
    {
        float s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    }
    else if (m[1][1] > m[2][2])
    {
        float s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (m[1][2] + m[2][1]) / s;
    }
    else
    {
        float s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25 * s;
    }
    return normalize(q);
}

float3 RotateByQuaternion(float3 v, float4 q)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void ApplyLinearSkin(VSIn i, out float3 skinnedPos, out float3 skinnedNrm)
{
    float4x4 skinMat = GetSkinMatrix(i.boneIndices, i.boneWeights);
    float4 pos4 = mul(float4(i.pos, 1.0), skinMat);
    skinnedPos = pos4.xyz;
    float3x3 skinMat3x3 = (float3x3) skinMat;
    skinnedNrm = normalize(mul(i.nrm, skinMat3x3));
}

void ApplySdefSkin(VSIn i, out float3 skinnedPos, out float3 skinnedNrm)
{
    int idx0 = i.boneIndices[0];
    int idx1 = i.boneIndices[1];

    if (idx0 < 0 || idx1 < 0 || idx0 >= 1024 || idx1 >= 1024)
    {
        ApplyLinearSkin(i, skinnedPos, skinnedNrm);
        return;
    }

    float w0 = saturate(i.boneWeights[0]);
    float w1_raw = saturate(i.boneWeights[1]);
    float w1 = (w1_raw > 0.0001) ? w1_raw : (1.0 - w0);

    float sumW = w0 + w1;
    if (sumW > 0.0001)
    {
        w0 /= sumW;
        w1 /= sumW;
    }
    else
    {
        w0 = 1.0;
        w1 = 0.0;
    }

    float4x4 matA4 = g_boneMatrices[idx0];
    float4x4 matB4 = g_boneMatrices[idx1];
    float3x3 matA = (float3x3) matA4;
    float3x3 matB = (float3x3) matB4;

    float4 qa = QuaternionFromMatrix(matA);
    float4 qb = QuaternionFromMatrix(matB);
    if (dot(qa, qb) < 0.0)
        qb = -qb;

    float4 q = normalize(qa * w0 + qb * w1);

    float3 C = i.sdefC;
    float3 R0 = i.sdefR0;
    float3 R1 = i.sdefR1;

    float3 Rtilde = R0 * w0 + R1 * w1;
    float3 C0 = C + (R0 - Rtilde) * 0.5;
    float3 C1 = C + (R1 - Rtilde) * 0.5;

    float3 term0 = mul(float4(C0, 1.0), matA4).xyz * w0;
    float3 term1 = mul(float4(C1, 1.0), matB4).xyz * w1;

    float3 rotated = RotateByQuaternion(i.pos - C, q);

    skinnedPos = term0 + term1 + rotated;
    skinnedNrm = normalize(RotateByQuaternion(i.nrm, q));
}


PSIn VSMain(VSIn i)
{
    PSIn o;
    float3 skinnedPos = i.pos;
    float3 skinnedNrm = i.nrm;

    if (g_enableSkinning != 0)
    {
        float totalWeight = 0.0;
        if (i.boneIndices[0] >= 0)
            totalWeight += i.boneWeights[0];
        if (i.boneIndices[1] >= 0)
            totalWeight += i.boneWeights[1];
        if (i.boneIndices[2] >= 0)
            totalWeight += i.boneWeights[2];
        if (i.boneIndices[3] >= 0)
            totalWeight += i.boneWeights[3];

        if (totalWeight > 0.001)
        {
            if (i.weightType == 3)
            {
                ApplySdefSkin(i, skinnedPos, skinnedNrm);
            }
            else
            {
                ApplyLinearSkin(i, skinnedPos, skinnedNrm);
            }
        }
    }

    float3x3 normalMatrix = float3x3(
        g_normalMatrixRow0.xyz,
        g_normalMatrixRow1.xyz,
        g_normalMatrixRow2.xyz
    );

    float3 worldPos = mul(float4(skinnedPos, 1.0), g_model).xyz;
    float3 worldNrm = normalize(mul(normalMatrix, skinnedNrm));
    float3 viewDir = normalize(g_cameraPos - worldPos);

    float ndv = saturate(abs(dot(worldNrm, viewDir)));
    float rim = 1.0 - ndv;

    float rimWeight = smoothstep(0.05, 0.35, rim);

    float widthFactor = lerp(0.15, 1.0, rimWeight);

    float edgeScale = g_edgeSize * 0.01 * widthFactor;
    float3 expandedPos = skinnedPos + skinnedNrm * edgeScale;

    o.pos = mul(float4(expandedPos, 1.0), g_mvp);

    float4 col = g_edgeColor;
    col.a *= lerp(0.2, 1.0, rimWeight);
    o.color = col;
    return o;
}