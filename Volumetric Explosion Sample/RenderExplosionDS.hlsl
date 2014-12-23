#include "RenderExplosion.hlsli"

[domain("quad")]
PS_INPUT main(HS_CONSTANT_DATA_OUTPUT input, float2 UV : SV_DomainLocation, const OutputPatch<HS_OUTPUT, 4> quad)
{
    float2 posClipSpace = UV.xy * 2.0f - 1.0f;
    float2 posClipSpaceAbs = abs(posClipSpace.xy);
    float maxLen = max(posClipSpaceAbs.x, posClipSpaceAbs.y);

    float3 dir = normalize(float3(posClipSpace.xy, (maxLen - 1.0f)));
    float innerRadius = g_ExplosionRadiusWS - g_DisplacementWS;

    // Even though our geometry only extends around the front of the explosion volume,
    //  we can calculate the reverse side of the hull here aswell.

    // First get the front world space position of the hull.
    float3 frontNormDir = dir;
    float3 frontPosWS = mul(g_ViewToWorldMatrix, float4(frontNormDir, 0)).xyz * g_ExplosionRadiusWS + g_ExplosionPositionWS;
    float3 frontDirWS = normalize(frontPosWS);
    // Then perform the shrink wrapping step using sphere tracing.
    for(uint i=0 ; i<g_NumHullSteps ; i++)
    {
        float displacementOut; // na
        float dist = DisplacedPrimitive(frontPosWS, g_ExplosionPositionWS.xyz, innerRadius, g_DisplacementWS, g_NumHullOctaves, displacementOut);
        frontPosWS -= frontDirWS * dist;
    }
    frontPosWS += frontDirWS * g_SkinThickness;
    float4 frontPosVS = mul(g_WorldToViewMatrix, float4(frontPosWS, 1));
    float4 frontPosPS = mul(g_WorldToProjectionMatrix, float4(frontPosWS, 1));

    // Then repeat the process for the back faces.
    float3 backNormDir = dir * float3(1, 1, -1);
    float3 backPosWS = mul(g_ViewToWorldMatrix, float4(backNormDir, 0)).xyz * g_ExplosionRadiusWS + g_ExplosionPositionWS;
    float3 backDirWS = normalize(frontPosWS);
    for(uint j=0 ; j<g_NumHullSteps ; j++)
    {
        float displacementOut; // na
        float dist = DisplacedPrimitive(backPosWS, g_ExplosionPositionWS.xyz, innerRadius, g_DisplacementWS, g_NumHullOctaves, displacementOut);
        backPosWS -= backDirWS * dist;
    }
    backPosWS += backDirWS * g_SkinThickness;
    float4 backPosVS = mul(g_WorldToViewMatrix, float4(backPosWS, 1));
    float4 backPosPS = mul(g_WorldToProjectionMatrix, float4(backPosWS, 1));

    float3 relativePosWS = frontPosWS - g_EyePositionWS;
    float3 rayDirectionWS = relativePosWS/dot(relativePosWS, g_EyeForwardWS);

    PS_INPUT o = (PS_INPUT)0;
    {
        o.PosPS = frontPosPS;
        o.rayHitNearFar = float2(frontPosVS.z, backPosVS.z);
        o.rayDirectionWS = rayDirectionWS;
    }
    return o;
}