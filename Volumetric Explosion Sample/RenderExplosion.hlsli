#include "Common.h"

Texture3D<float>    g_NoiseVolumeRO : register(T_REG(T_NOISE_VOLUME));
Texture2D<float4>   g_GradientTexRO : register(T_REG(T_GRADIENT_TEX));

struct HS_CONSTANT_DATA_OUTPUT
{
    float EdgeTessFactor[4]	: SV_TessFactor; 
    float InsideTessFactor[2]	: SV_InsideTessFactor; 
};

struct HS_OUTPUT {};

struct PS_INPUT
{
    float4 PosPS : SV_Position;
    noperspective float2 rayHitNearFar : RAYHIT;
    noperspective float3 rayDirectionWS : RAYDIR;
};

float Noise( float3 uvw )
{
    const float noiseVal = g_NoiseVolumeRO.SampleLevel(BilinearWrappedSampler, uvw, 0);

    return noiseVal;	
}

float FractalNoiseAtPositionWS( float3 posWS, uint numOctaves )
{
    const float3 animation = g_NoiseAnimationSpeed * g_Time;

    float3 uvw = posWS * g_NoiseScale + animation; 
    float amplitude = g_NoiseInitialAmplitude;
    
    float noiseValue = 0;
    for(uint i=0 ; i<numOctaves ; i++)
    {
        noiseValue += abs(amplitude * Noise( uvw )); 
        amplitude *= g_NoiseAmplitudeFactor; 
        uvw *= g_NoiseFrequencyFactor;
    }

    return noiseValue * g_InvMaxNoiseDisplacement; 
}

float Box( float3 relativePosWS, float3 b )
{
    const float3 d = abs( relativePosWS ) - b;
    return min( max( d.x, max( d.y, d.z ) ), 0.0f ) + length( max( d, 0.0f ) );
}

float Torus( float3 relativePosWS, float radiusWS )
{
    const float2 t = radiusWS.xx * float2( 1, 0.01f );
    float2 q = float2( length( relativePosWS.xz ) - t.x , relativePosWS.y );
    return length( q ) - t.y;
}

float Cone( float3 relativePosWS, float radiusWS )
{
    float d = length( relativePosWS.xz ) - lerp( radiusWS*0.5f, 0, (radiusWS + relativePosWS.y) / (radiusWS) ); 
    d = max( d,-relativePosWS.y - radiusWS ); 
    d = max( d, relativePosWS.y - radiusWS ); 

    return d; 
}

float Cylinder( float3 relativePosWS, float radiusWS)
{
    const float2 h = radiusWS.xx * float2( 0.7f, 1 );
    const float2 d = abs( float2( length( relativePosWS.xz ), relativePosWS.y ) ) - h;
    return min( max( d.x, d.y ), 0.0f) + length( max( d, 0.0f) );
}

float Sphere( float3 relativePosWS, float radiusWS)
{
    return length( relativePosWS ) - radiusWS;
}

float DisplacedPrimitive( float3 posWS, float3 spherePositionWS, float radiusWS, float displacementWS, uint numOctaves, out float displacementOut )
{
    float3 relativePosWS = posWS - spherePositionWS;

    displacementOut = FractalNoiseAtPositionWS( posWS, numOctaves );

    float signedDistanceToPrimitive = 0;

    switch(g_PrimitiveIdx)
    {
    case 0:
        signedDistanceToPrimitive = Sphere( relativePosWS, radiusWS );
        break;
    case 1:
        signedDistanceToPrimitive = Cylinder( relativePosWS, radiusWS );
        break;
    case 2: 
        signedDistanceToPrimitive = Cone( relativePosWS, radiusWS );
        break;
    case 3:
        signedDistanceToPrimitive = Torus( relativePosWS, radiusWS );
        break;
    case 4:
        signedDistanceToPrimitive = Box( relativePosWS, sqrt(radiusWS*radiusWS/2).xxx );
        break;
    } 

    return signedDistanceToPrimitive - displacementOut * displacementWS;
}

float4 MapDisplacementToColour( const float displacement, const float2 uvScaleBias )
{
    float texcoord = saturate( mad(displacement, uvScaleBias.x, uvScaleBias.y) );
    texcoord = 1-(1-texcoord)*(1-texcoord); // These adjustments should be made in the texture itself.

    float4 colour = g_GradientTexRO.SampleLevel(BilinearClampedSampler, texcoord, 0);

    // Apply some more adjustments to the colour post sample.  Again, these should be made in the texture itself.
    colour *= colour;
    colour.a = 0.5f;

    return colour;
}

float4 SceneFunction( const float3 posWS, const float3 spherePositionWS, const float radiusWS, const float displacementWS, const float2 uvScaleBias )
{
    float displacementOut;
    float distance = DisplacedPrimitive( posWS, spherePositionWS, radiusWS, displacementWS, g_NumOctaves, displacementOut );
    float4 colour = MapDisplacementToColour( displacementOut, uvScaleBias );

    // Rather than just using a binary in/out metric, we smooth the edge of the volume using a smoothstep so that we get soft edges.
    float edgeFade = smoothstep( 0.5f + g_EdgeSoftness, 0.5f - g_EdgeSoftness, distance );

    return colour * float4( 1..xxx, edgeFade );
}

float4 Blend( const float4 src, const float4 dst )
{
    return mad(float4(dst.rgb, 1), mad(dst.a, -src.a, dst.a), src);
}