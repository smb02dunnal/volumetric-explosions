#include "RenderExplosion.hlsli"

float4 main(PS_INPUT i) : SV_TARGET
{
    const float3 rayDirectionWS = i.rayDirectionWS;
    float nearD = i.rayHitNearFar.x, farD = i.rayHitNearFar.y;

    float4 output = 0..xxxx;

    const float3 startWS = mad(rayDirectionWS, nearD, g_EyePositionWS.xyz);
    const float3 endWS	 = mad(rayDirectionWS, farD , g_EyePositionWS.xyz);

    const float3 stepAmountWS = rayDirectionWS * g_StepSizeWS; 
    const float numSteps = min( g_MaxNumSteps, (farD - nearD) / g_StepSizeWS );
    const float innerRadius = g_ExplosionRadiusWS - g_DisplacementWS;

    float3 posWS = startWS;

    float stepsTaken = 0;
    while( stepsTaken++ < numSteps && output.a < g_Opacity )
    {
        float4 colour = SceneFunction( posWS, g_ExplosionPositionWS.xyz, innerRadius, g_DisplacementWS, g_UvScaleBias );
        output = Blend( output, colour );
        
        posWS += stepAmountWS;
    }

    return output * float4( 1..xxx, g_Opacity );
}