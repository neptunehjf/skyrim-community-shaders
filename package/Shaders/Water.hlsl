#include "Common/FrameBuffer.hlsl"
#include "Common/MotionBlur.hlsl"
#include "Common/Permutation.hlsl"

#define WATER

#include "Common/LightingData.hlsl"

struct VS_INPUT
{
#if defined(SPECULAR) || defined(UNDERWATER) || defined(STENCIL) || defined(SIMPLE)
	float4 Position : POSITION0;
#	if defined(NORMAL_TEXCOORD)
	float2 TexCoord0 : TEXCOORD0;
#	endif
#	if defined(VC)
	float4 Color : COLOR0;
#	endif
#endif

#if defined(LOD)
	float4 Position : POSITION0;
#	if defined(VC)
	float4 Color : COLOR0;
#	endif
#endif
#if defined(VR)
	uint InstanceID : SV_INSTANCEID;
#endif  // VR
};

struct VS_OUTPUT
{
#if defined(SPECULAR) || defined(UNDERWATER)
	float4 HPosition : SV_POSITION0;
	float4 FogParam : COLOR0;
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
	float4 TexCoord2 : TEXCOORD2;
#	if defined(WADING) || (defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS))) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)) || ((defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0) && defined(FLOWMAP) /*!defined(NORMAL_TEXCOORD) && !defined(BLEND_NORMALS) && !defined(VC)*/)
	float4 TexCoord3 : TEXCOORD3;
#	endif
#	if defined(FLOWMAP)
	nointerpolation float TexCoord4 : TEXCOORD4;
#	endif
#	if NUM_SPECULAR_LIGHTS == 0
	float4 MPosition : TEXCOORD5;
#	endif
#endif

#if defined(SIMPLE)
	float4 HPosition : SV_POSITION0;
	float4 FogParam : COLOR0;
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
	float4 TexCoord2 : TEXCOORD2;
	float4 MPosition : TEXCOORD5;
#endif

#if defined(LOD)
	float4 HPosition : SV_POSITION0;
	float4 FogParam : COLOR0;
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
#endif

#if defined(STENCIL)
	float4 HPosition : SV_POSITION0;
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
#endif

	float4 NormalsScale : TEXCOORD8;
#if defined(VR)
	float ClipDistance : SV_ClipDistance0;  // o11
	float CullDistance : SV_CullDistance0;  // p11
#endif                                      // VR
};

#ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
#	if !defined(VR)
	float4 QPosAdjust[1] : packoffset(c0);
#	else
	float4 QPosAdjust[2] : packoffset(c0);
#	endif  // VR
};

cbuffer PerMaterial : register(b1)
{
	float4 VSFogParam : packoffset(c0);
	float4 VSFogNearColor : packoffset(c1);
	float4 VSFogFarColor : packoffset(c2);
	float4 NormalsScroll0 : packoffset(c3);
	float4 NormalsScroll1 : packoffset(c4);
	float4 NormalsScale : packoffset(c5);
};

cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
	row_major float4x4 World[1] : packoffset(c0);
	row_major float4x4 PreviousWorld[1] : packoffset(c4);
	row_major float4x4 WorldViewProj[1] : packoffset(c8);
	float3 ObjectUV : packoffset(c12);
	float4 CellTexCoordOffset : packoffset(c13);
#	else   // VR has 25 vs 13 entries
	row_major float4x4 World[2] : packoffset(c0);
	row_major float4x4 PreviousWorld[2] : packoffset(c8);
	row_major float4x4 WorldViewProj[2] : packoffset(c16);
	float3 ObjectUV : packoffset(c24);
	float4 CellTexCoordOffset : packoffset(c25);
#	endif  // VR
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif
	);
	vsout.NormalsScale = NormalsScale;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 worldPos = mul(World[eyeIndex], inputPosition);
	float4 worldViewPos = mul(WorldViewProj[eyeIndex], inputPosition);

	float heightMult = min((1.0 / 10000.0) * max(worldViewPos.z - 70000, 0), 1);

	vsout.HPosition.xy = worldViewPos.xy;
	vsout.HPosition.z = heightMult * 0.5 + worldViewPos.z;
	vsout.HPosition.w = worldViewPos.w;

#	if defined(STENCIL)
	vsout.WorldPosition = worldPos;
	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], inputPosition);
#	else
	float fogColorParam = min(VSFogFarColor.w,
		pow(saturate(length(worldViewPos.xyz) * VSFogParam.y - VSFogParam.x), NormalsScale.w));
	vsout.FogParam.xyz = lerp(VSFogNearColor.xyz, VSFogFarColor.xyz, fogColorParam);
	vsout.FogParam.w = fogColorParam;

	vsout.WPosition.xyz = worldPos.xyz;
	vsout.WPosition.w = length(worldPos.xyz);

#		if defined(LOD)
	float4 posAdjust =
		ObjectUV.x ? 0.0 : (QPosAdjust[eyeIndex].xyxy + worldPos.xyxy) / NormalsScale.xxyy;

	vsout.TexCoord1.xyzw = NormalsScroll0 + posAdjust;
#		else
#			if !defined(SPECULAR) || (NUM_SPECULAR_LIGHTS == 0)
	vsout.MPosition.xyzw = inputPosition.xyzw;
#			endif

	float2 posAdjust = worldPos.xy + QPosAdjust[eyeIndex].xy;

	float2 scrollAdjust1 = posAdjust / NormalsScale.xx;
	float2 scrollAdjust2 = posAdjust / NormalsScale.yy;
	float2 scrollAdjust3 = posAdjust / NormalsScale.zz;

#			if !(defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS) || defined(DEPTH) || NUM_SPECULAR_LIGHTS == 0))
#				if defined(NORMAL_TEXCOORD)
	float3 normalsScale = 0.001 * NormalsScale.xyz;
	if (ObjectUV.x) {
		scrollAdjust1 = input.TexCoord0.xy / normalsScale.xx;
		scrollAdjust2 = input.TexCoord0.xy / normalsScale.yy;
		scrollAdjust3 = input.TexCoord0.xy / normalsScale.zz;
	}
#				else
	if (ObjectUV.x) {
		scrollAdjust1 = 0.0;
		scrollAdjust2 = 0.0;
		scrollAdjust3 = 0.0;
	}
#				endif
#			endif

	vsout.TexCoord1 = 0.0;
	vsout.TexCoord2 = 0.0;
#			if defined(FLOWMAP)
#				if !(((defined(SPECULAR) || NUM_SPECULAR_LIGHTS == 0) || (defined(UNDERWATER) && defined(REFRACTIONS))) && !defined(NORMAL_TEXCOORD))
#					if defined(BLEND_NORMALS)
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
#					else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = 0.0;
	vsout.TexCoord2.xy = 0.0;
#					endif
#				endif
#				if !defined(NORMAL_TEXCOORD)
	vsout.TexCoord3 = 0.0;
#				elif defined(WADING)
	vsout.TexCoord2.zw = ((-0.5 + input.TexCoord0.xy) * 0.1 + CellTexCoordOffset.xy) +
	                     float2(CellTexCoordOffset.z, -CellTexCoordOffset.w + ObjectUV.x) / ObjectUV.xx;
	vsout.TexCoord3.xy = -0.25 + (input.TexCoord0.xy * 0.5 + ObjectUV.yz);
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#				elif (defined(REFRACTIONS) || NUM_SPECULAR_LIGHTS == 0 || defined(BLEND_NORMALS))
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + input.TexCoord0.xy) / ObjectUV.xx;
	vsout.TexCoord3.xy = (CellTexCoordOffset.zw + input.TexCoord0.xy);
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#				endif
	vsout.TexCoord4 = ObjectUV.x;
#			else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
	vsout.TexCoord2.z = worldViewPos.w;
	vsout.TexCoord2.w = 0;
#				if (defined(WADING) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)))
	vsout.TexCoord3 = 0.0;
#					if (defined(NORMAL_TEXCOORD) && ((!defined(BLEND_NORMALS) && !defined(VERTEX_ALPHA_DEPTH)) || defined(WADING)))
	vsout.TexCoord3.xy = input.TexCoord0;
#					endif
#					if defined(VERTEX_ALPHA_DEPTH) && defined(VC)
	vsout.TexCoord3.z = input.Color.w;
#					endif
#				endif
#			endif
#		endif
#	endif

#	ifdef VR
	VR_OUTPUT VRout = GetVRVSOutput(vsout.HPosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#	endif  // VR
	return vsout;
}

#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#if defined(UNDERWATER) || defined(SIMPLE) || defined(LOD) || defined(SPECULAR)
	float4 Lighting : SV_Target0;
#endif

#if defined(STENCIL)
	float4 WaterMask : SV_Target0;
	float2 MotionVector : SV_Target1;
#endif
};

#ifdef PSHADER

SamplerState ReflectionSampler : register(s0);
SamplerState RefractionSampler : register(s1);
SamplerState DisplacementSampler : register(s2);
SamplerState CubeMapSampler : register(s3);
SamplerState Normals01Sampler : register(s4);
SamplerState Normals02Sampler : register(s5);
SamplerState Normals03Sampler : register(s6);
SamplerState DepthSampler : register(s7);
SamplerState FlowMapSampler : register(s8);
SamplerState FlowMapNormalsSampler : register(s9);
SamplerState SSRReflectionSampler : register(s10);
SamplerState RawSSRReflectionSampler : register(s11);

Texture2D<float4> ReflectionTex : register(t0);
Texture2D<float4> RefractionTex : register(t1);
Texture2D<float4> DisplacementTex : register(t2);
TextureCube<float4> CubeMapTex : register(t3);
Texture2D<float4> Normals01Tex : register(t4);
Texture2D<float4> Normals02Tex : register(t5);
Texture2D<float4> Normals03Tex : register(t6);
Texture2D<float4> DepthTex : register(t7);
Texture2D<float4> FlowMapTex : register(t8);
Texture2D<float4> FlowMapNormalsTex : register(t9);
Texture2D<float4> SSRReflectionTex : register(t10);
Texture2D<float4> RawSSRReflectionTex : register(t11);

cbuffer PerTechnique : register(b0)
{
#	if !defined(VR)
	float4 VPOSOffset : packoffset(c0);    // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust[1] : packoffset(c1);  // inverse framebuffer range in w
	float4 CameraData : packoffset(c2);
	float4 SunDir : packoffset(c3);
	float4 SunColor : packoffset(c4);
#	else
	float4 VPOSOffset : packoffset(c0);    // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust[2] : packoffset(c1);  // inverse framebuffer range in w
	float4 CameraData : packoffset(c3);
	float4 SunDir : packoffset(c4);
	float4 SunColor : packoffset(c5);
#	endif
}

cbuffer PerMaterial : register(b1)
{
	float4 ShallowColor : packoffset(c0);
	float4 DeepColor : packoffset(c1);
	float4 ReflectionColor : packoffset(c2);
	float4 FresnelRI : packoffset(c3);    // Fresnel amount in x, specular power in z
	float4 BlendRadius : packoffset(c4);  // flowmap scale in y, specular radius in z
	float4 VarAmounts : packoffset(c5);   // Sun specular power in x, reflection amount in y, alpha in z, refraction magnitude in w
	float4 NormalsAmplitude : packoffset(c6);
	float4 WaterParams : packoffset(c7);   // noise falloff in x, reflection magnitude in y, sun sparkle power in z, framebuffer range in w
	float4 FogNearColor : packoffset(c8);  // above water fog amount in w
	float4 FogFarColor : packoffset(c9);
	float4 FogParam : packoffset(c10);      // above water fog distance far in z, above water fog range in w
	float4 DepthControl : packoffset(c11);  // depth reflections factor in x, depth refractions factor in y, depth normals factor in z, depth specular lighting factor in w
	float4 SSRParams : packoffset(c12);     // fWaterSSRIntensity in x, fWaterSSRBlurAmount in y, inverse main render target width and height in zw
	float4 SSRParams2 : packoffset(c13);    // fWaterSSRNormalPerturbationScale in x
}

cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
	float4x4 TextureProj[1] : packoffset(c0);
	float4 ReflectPlane[1] : packoffset(c4);
	float4 ProjData : packoffset(c5);
	float4 LightPos[8] : packoffset(c6);
	float4 LightColor[8] : packoffset(c14);
#	else
	float4x4 TextureProj[2] : packoffset(c0);
	float4 ReflectPlane[2] : packoffset(c8);
	float4 ProjData : packoffset(c10);
	float4 LightPos[8] : packoffset(c11);
	float4 LightColor[8] : packoffset(c19);
#	endif  //VR
}

#	ifdef VR
float GetStencil(float2 uv)
{
	return DepthTex.Load(int3(uv * lightingData[0].BufferDim * DynamicResolutionParams1.xy, 0)).g;
}

/**
Calculates the depthMultiplier as used in water.hlsl

VR appears to require use of CameraProjInverse and does not use projData
@param a_uv uv coords to convert
@param a_depth The calculated depth
@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
@returns depthMultiplier
*/
float calculateDepthMultfromUV(float2 a_uv, float a_depth, uint a_eyeIndex = 0)
{
	float4 temp;
	temp.xy = (a_uv * 2 - 1);
	temp.z = a_depth;
	temp.w = 1;
	temp = mul(CameraProjInverse[a_eyeIndex], temp.xyzw);
	temp.xyz /= temp.w;
	return length(temp);
}
#	endif  // VR

#	define SampColorSampler Normals01Sampler

#	if defined(LOD)
#		undef WATER_CAUSTICS
#	endif

#	if defined(WATER_CAUSTICS)
#		include "WaterCaustics/WaterCaustics.hlsli"
#	endif

#	if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
#		if defined(FLOWMAP)
#			if defined(WATER_CAUSTICS)
float3 GetFlowmapNormal(PS_INPUT input, float2 uvShift, float multiplier, float offset, float2 viewOffset, out float3 caustics, uint a_eyeIndex)
#			else
float3 GetFlowmapNormal(PS_INPUT input, float2 uvShift, float multiplier, float offset, uint a_eyeIndex)
#			endif
{
	float4 flowmapColor = FlowMapTex.Sample(FlowMapSampler, input.TexCoord2.zw + uvShift);
	float2 flowVector = (64 * input.TexCoord3.xy) * sqrt(1.01 - flowmapColor.z);
	float2 flowSinCos = flowmapColor.xy * 2 - 1;
	float2x2 flowRotationMatrix = float2x2(flowSinCos.x, flowSinCos.y, -flowSinCos.y, flowSinCos.x);
	float2 rotatedFlowVector = mul(transpose(flowRotationMatrix), flowVector);
	float2 uv = offset + (rotatedFlowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowmapColor.w), 0));
#			if defined(WATER_CAUSTICS)
	caustics = ComputeWaterCaustics(uv + viewOffset.xy * 0.001);
#			endif
	return float3(FlowMapNormalsTex.Sample(FlowMapNormalsSampler, uv).xy, flowmapColor.z);
}
#		endif

#		if (defined(FLOWMAP) && !defined(BLEND_NORMALS)) || defined(LOD)
#			undef WATER_PARALLAX
#		endif

#		if defined(WATER_PARALLAX)
#			include "WaterParallax/WaterParallax.hlsli"
#		endif

#		if defined(DYNAMIC_CUBEMAPS)
#			include "DynamicCubemaps/DynamicCubemaps.hlsli"
#		endif

#		if defined(WATER_CAUSTICS)
float3 GetWaterNormal(PS_INPUT input, float distanceFactor, float normalsDepthFactor, float3 viewDirection, float depth, inout float3 caustics, uint a_eyeIndex)
#		else
float3 GetWaterNormal(PS_INPUT input, float distanceFactor, float normalsDepthFactor, float3 viewDirection, float depth, uint a_eyeIndex)
#		endif
{
	float3 normalScalesRcp = rcp(input.NormalsScale.xyz);

#		if defined(WATER_PARALLAX)
	float2 parallaxOffset = GetParallaxOffset(input, normalScalesRcp);
#		endif

#		if defined(WATER_CAUSTICS)
#			if defined(VERTEX_ALPHA_DEPTH)
	float depthDifference = 128.0;
#			else
	float depthDifference = depth - length(input.WPosition.xyz);
#			endif
	float2 viewOffset = viewDirection.xy * depthDifference;
#		endif

#		if defined(FLOWMAP)
	float2 normalMul =
		0.5 + -(-0.5 + abs(frac(input.TexCoord2.zw * (64 * input.TexCoord4)) * 2 - 1));
	float uvShift = 1 / (128 * input.TexCoord4);

#			if defined(WATER_CAUSTICS)
	float3 flowmapCaustics0;
	float3 flowmapNormal0 = GetFlowmapNormal(input, uvShift.xx, 9.92, 0, viewOffset, flowmapCaustics0, a_eyeIndex);
	float3 flowmapCaustics1;
	float3 flowmapNormal1 = GetFlowmapNormal(input, float2(0, uvShift), 10.64, 0.27, viewOffset, flowmapCaustics1, a_eyeIndex);
	float3 flowmapCaustics2;
	float3 flowmapNormal2 = GetFlowmapNormal(input, 0.0.xx, 8, 0, viewOffset, flowmapCaustics2, a_eyeIndex);
	float3 flowmapCaustics3;
	float3 flowmapNormal3 = GetFlowmapNormal(input, float2(uvShift, 0), 8.48, 0.62, viewOffset, flowmapCaustics3, a_eyeIndex);

	float3 flowmapCausticsWeighted = normalMul.y * (normalMul.x * flowmapCaustics2 + (1.0 - normalMul.x) * flowmapCaustics3) + (1.0 - normalMul.y) * (normalMul.x * flowmapCaustics1 + (1.0 - normalMul.x) * flowmapCaustics0);
	caustics = flowmapCausticsWeighted;
#			else
	float3 flowmapNormal0 = GetFlowmapNormal(input, uvShift.xx, 9.92, 0, a_eyeIndex);
	float3 flowmapNormal1 = GetFlowmapNormal(input, float2(0, uvShift), 10.64, 0.27, a_eyeIndex);
	float3 flowmapNormal2 = GetFlowmapNormal(input, 0.0.xx, 8, 0, a_eyeIndex);
	float3 flowmapNormal3 = GetFlowmapNormal(input, float2(uvShift, 0), 8.48, 0.62, a_eyeIndex);
#			endif

	float2 flowmapNormalWeighted =
		normalMul.y * (normalMul.x * flowmapNormal2.xy + (1 - normalMul.x) * flowmapNormal3.xy) +
		(1 - normalMul.y) *
			(normalMul.x * flowmapNormal1.xy + (1 - normalMul.x) * flowmapNormal0.xy);
	float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));
	float3 flowmapNormal =
		float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)) *
				   max(0.4, normalsDepthFactor),
			0);
	flowmapNormal.z =
		sqrt(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y);
#		endif

#		if defined(WATER_PARALLAX)
	float3 normals1 = Normals01Tex.Sample(Normals01Sampler, input.TexCoord1.xy + parallaxOffset.xy * normalScalesRcp.x).xyz * 2.0 + float3(-1, -1, -2);
#		else
	float3 normals1 = Normals01Tex.Sample(Normals01Sampler, input.TexCoord1.xy).xyz * 2.0 + float3(-1, -1, -2);
#		endif

#		if defined(FLOWMAP) && !defined(BLEND_NORMALS)
	float3 finalNormal =
		normalize(lerp(normals1 + float3(0, 0, 1), flowmapNormal, distanceFactor));
#		elif !defined(LOD)

#			if defined(WATER_PARALLAX)
	float3 normals2 = Normals02Tex.Sample(Normals02Sampler, input.TexCoord1.zw + parallaxOffset.xy * normalScalesRcp.y).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.Sample(Normals03Sampler, input.TexCoord2.xy + parallaxOffset.xy * normalScalesRcp.z).xyz * 2.0 - 1.0;
#			else
	float3 normals2 = Normals02Tex.Sample(Normals02Sampler, input.TexCoord1.zw).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.Sample(Normals03Sampler, input.TexCoord2.xy).xyz * 2.0 - 1.0;
#			endif

#			if defined(WATER_CAUSTICS) && !defined(VR)  // VR disabled until caustics doesn't move
	float3 caustics1 = ComputeWaterCaustics(input.TexCoord1.xy + viewOffset.xy * normalScalesRcp.x);
	float3 caustics2 = ComputeWaterCaustics(input.TexCoord1.zw + viewOffset.xy * normalScalesRcp.y);
	float3 caustics3 = ComputeWaterCaustics(input.TexCoord2.xy + viewOffset.xy * normalScalesRcp.z);

	float3 blendedCaustics = NormalsAmplitude.xxx * caustics1 + NormalsAmplitude.yyy * caustics2 + NormalsAmplitude.zzz * caustics3;
	caustics = blendedCaustics;
#			endif

	float3 blendedNormal = normalize(float3(0, 0, 1) + NormalsAmplitude.x * normals1 +
									 NormalsAmplitude.y * normals2 + NormalsAmplitude.z * normals3);
#			if defined(UNDERWATER)
	float3 finalNormal = blendedNormal;
#			else
	float3 finalNormal = normalize(lerp(float3(0, 0, 1), blendedNormal, normalsDepthFactor));
#			endif

#			if defined(FLOWMAP)
	float normalBlendFactor =
		normalMul.y * ((1 - normalMul.x) * flowmapNormal3.z + normalMul.x * flowmapNormal2.z) +
		(1 - normalMul.y) * (normalMul.x * flowmapNormal1.z + (1 - normalMul.x) * flowmapNormal0.z);
	finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), normalize(lerp(finalNormal, flowmapNormal, normalBlendFactor)), distanceFactor));
#				if defined(WATER_CAUSTICS) && !defined(VR)
	caustics = lerp(blendedCaustics, flowmapCausticsWeighted, normalBlendFactor);
#				endif
#			endif
#		else
	float3 finalNormal =
		normalize(float3(0, 0, 1) + NormalsAmplitude.xxx * normals1);
#		endif

#		if defined(WADING)
#			if defined(FLOWMAP)
	float2 displacementUv = input.TexCoord3.zw;
#			else
	float2 displacementUv = input.TexCoord3.xy;
#			endif
	float3 displacement = normalize(float3(NormalsAmplitude.w * (-0.5 + DisplacementTex.Sample(DisplacementSampler, displacementUv).zw),
		0.04));
	finalNormal = lerp(displacement, finalNormal, displacement.z);
#		endif

	return finalNormal;
}

float3 GetWaterSpecularColor(PS_INPUT input, float3 normal, float3 viewDirection,
	float distanceFactor, float refractionsDepthFactor, uint a_eyeIndex = 0)
{
	float4 dynamicCubemap = 0.0;
	if (shaderDescriptors[0].PixelShaderDescriptor & _Reflections) {
		float3 finalSsrReflectionColor = 0.0.xxx;
		float ssrFraction = 0;
		float3 reflectionColor = 0;
		if (shaderDescriptors[0].PixelShaderDescriptor & _Cubemap) {
			float3 cubemapUV = reflect(viewDirection, WaterParams.y * normal + float3(0, 0, 1 - WaterParams.y));
#		if defined(DYNAMIC_CUBEMAPS)
			dynamicCubemap = specularTexture.SampleLevel(CubeMapSampler, cubemapUV, 0);
			reflectionColor =
#			if defined(VR)  // use stencil to ignore player character
				GetStencil(cubemapUV.xy) == 0 ? CubeMapTex.SampleLevel(CubeMapSampler, cubemapUV, 0).xyz :
#			endif
												lerp(dynamicCubemap.xyz, CubeMapTex.SampleLevel(CubeMapSampler, cubemapUV, 0).xyz, saturate(length(input.WPosition.xyz) * 0.0001));
#		else
			reflectionColor = CubeMapTex.SampleLevel(CubeMapSampler, cubemapUV, 0).xyz;
#		endif
		} else {
#		if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0
			float4 reflectionNormalRaw = float4((VarAmounts.w * refractionsDepthFactor) * normal.xy + input.MPosition.xy, input.MPosition.z, 1);
#		else
			float4 reflectionNormalRaw = float4(VarAmounts.w * normal.xy, 0, 1);
#		endif

			float4 reflectionNormal = mul(transpose(TextureProj[a_eyeIndex]), reflectionNormalRaw);
			reflectionColor = ReflectionTex.SampleLevel(ReflectionSampler, reflectionNormal.xy / reflectionNormal.ww, 0).xyz;
		}

#		if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0 && !defined(VR)
		if (shaderDescriptors[0].PixelShaderDescriptor & _Cubemap) {
			float2 ssrReflectionUv = GetDynamicResolutionAdjustedScreenPosition((DynamicResolutionParams2.xy * input.HPosition.xy) * SSRParams.zw + SSRParams2.x * normal.xy);
			float4 ssrReflectionColor1 = SSRReflectionTex.Sample(SSRReflectionSampler, ssrReflectionUv);
			float4 ssrReflectionColor2 = RawSSRReflectionTex.Sample(RawSSRReflectionSampler, ssrReflectionUv);
			float4 ssrReflectionColor = lerp(ssrReflectionColor2, ssrReflectionColor1, SSRParams.y);

			finalSsrReflectionColor = max(0, ssrReflectionColor.xyz);
			ssrFraction = saturate(ssrReflectionColor.w * (SSRParams.x * distanceFactor));
		}
#		endif

		float3 finalReflectionColor = lerp(reflectionColor * WaterParams.w, finalSsrReflectionColor, ssrFraction);
		return lerp(ReflectionColor.xyz, finalReflectionColor, VarAmounts.y);
	}
	return ReflectionColor.xyz * VarAmounts.y;
}

#		if defined(DEPTH)
float GetScreenDepthWater(float2 screenPosition, uint a_useVR = 0)
{
	float depth = DepthTex.Load(float3(screenPosition, 0)).x;
	return
#			if defined(VR)  // VR appears to use hard coded values
		a_useVR ? depth * 1.01 + -0.01 :
#			endif
				  (CameraData.w / (-depth * CameraData.z + CameraData.x));
}
#		endif

float3 GetLdotN(float3 normal)
{
#		if defined(UNDERWATER)
	return 1;
#		else
	if (shaderDescriptors[0].PixelShaderDescriptor & _Interior)
		return 1;
	return saturate(dot(SunDir.xyz, normal));
#		endif
}

float GetFresnelValue(float3 normal, float3 viewDirection)
{
#		if defined(UNDERWATER)
	float3 actualNormal = -normal;
#		else
	float3 actualNormal = normal;
#		endif
	float viewAngle = 1 - saturate(dot(-viewDirection, actualNormal));
	return (1 - FresnelRI.x) * pow(viewAngle, 5) + FresnelRI.x;
}

#		if defined(WATER_CAUSTICS)
float3 GetWaterDiffuseColor(PS_INPUT input, float3 normal, float3 viewDirection,
	float4 distanceMul, float refractionsDepthFactor, float fresnel, float3 caustics, uint a_eyeIndex = 0)
#		else
float3 GetWaterDiffuseColor(PS_INPUT input, float3 normal, float3 viewDirection,
	float4 distanceMul, float refractionsDepthFactor, float fresnel, uint a_eyeIndex = 0)
#		endif
{
#		if defined(REFRACTIONS)
	float4 refractionNormal = mul(transpose(TextureProj[a_eyeIndex]),
		float4((VarAmounts.w * refractionsDepthFactor).xx * normal.xy + input.MPosition.xy,
			input.MPosition.z, 1));

	float2 refractionUvRaw =
		float2(refractionNormal.x, refractionNormal.w - refractionNormal.y) / refractionNormal.ww;
	refractionUvRaw = ConvertToStereoUV(refractionUvRaw, a_eyeIndex);  // need to convert here for VR due to refractionNormal values

#			if defined(DEPTH) && !defined(VERTEX_ALPHA_DEPTH)
	float2 screenPosition = DynamicResolutionParams1.xy * (DynamicResolutionParams2.xy * input.HPosition.xy);
	float depth = GetScreenDepthWater(screenPosition);
	float2 refractionScreenPosition = DynamicResolutionParams1.xy * (refractionUvRaw / VPOSOffset.xy);
	float refractionDepth = GetScreenDepthWater(refractionScreenPosition);
	float refractionDepthMul = length(float3((((VPOSOffset.zw + refractionUvRaw) * 2 - 1)) * refractionDepth / ProjData.xy, refractionDepth));

	float3 refractionDepthAdjustedViewDirection = -viewDirection * refractionDepthMul;
	float refractionViewSurfaceAngle = dot(refractionDepthAdjustedViewDirection, ReflectPlane[a_eyeIndex].xyz);

	float refractionPlaneMul =
		sign(refractionViewSurfaceAngle) * (1 - ReflectPlane[a_eyeIndex].w / refractionViewSurfaceAngle);

	if (refractionPlaneMul < 0) {
		refractionUvRaw =
			DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;  // This value is already stereo converted for VR
	}
#			endif

	float2 refractionUV = GetDynamicResolutionAdjustedScreenPosition(refractionUvRaw);
	float3 refractionColor = RefractionTex.Sample(RefractionSampler, refractionUV).xyz;
	float3 refractionDiffuseColor = lerp(ShallowColor.xyz, DeepColor.xyz, distanceMul.y);

#			if defined(UNDERWATER)
	float refractionMul = 0;
#			else
	float refractionMul =
		1 - pow(saturate((-distanceMul.x * FogParam.z + FogParam.z) / FogParam.w), FogNearColor.w);
#			endif

#			if defined(WATER_CAUSTICS)
	if (perPassWaterCaustics[0].EnableWaterCaustics)
		refractionColor *= lerp(1.0, caustics, refractionMul);
#			endif

	return lerp(refractionColor * WaterParams.w, refractionDiffuseColor, refractionMul);
#		else
	return lerp(ShallowColor.xyz, DeepColor.xyz, fresnel) * GetLdotN(normal);
#		endif
}

float3 GetSunColor(float3 normal, float3 viewDirection)
{
#		if defined(UNDERWATER)
	return 0.0.xxx;
#		else
	if (shaderDescriptors[0].PixelShaderDescriptor & _Interior)
		return 0.0.xxx;

	float3 reflectionDirection = reflect(viewDirection, normal);

	float reflectionMul = exp2(VarAmounts.x * log2(saturate(dot(reflectionDirection, SunDir.xyz))));

	float3 sunDirection = SunColor.xyz * SunDir.w;
	float sunMul = pow(saturate(dot(normal, float3(-0.099, -0.099, 0.99))), ShallowColor.w);
	return (reflectionMul * sunDirection * 0.3) * DeepColor.w + WaterParams.z * (sunMul * sunDirection * 0.1);
#		endif
}
#	endif

#	if defined(WATER_BLENDING)
#		include "WaterBlending/WaterBlending.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	uint eyeIndex = GetEyeIndexPS(input.HPosition, VPOSOffset);
#	if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
	float3 viewDirection = normalize(input.WPosition.xyz);

	float distanceFactor = saturate(lerp(FrameParams.w, 1, (input.WPosition.w - 8192) / (WaterParams.x - 8192)));
	float4 distanceMul = saturate(lerp(VarAmounts.z, 1, -(distanceFactor - 1))).xxxx;

	bool isSpecular = false;

	float depth = 0;

#		if defined(DEPTH)
#			if defined(VERTEX_ALPHA_DEPTH)
#				if defined(VC)
	distanceMul = saturate(input.TexCoord3.z);
#				endif
#			else
	distanceMul = 0;

	float2 screenPosition = DynamicResolutionParams1.xy * (DynamicResolutionParams2.xy * input.HPosition.xy);

	depth = GetScreenDepthWater(screenPosition);
	float2 depthOffset =
		DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
#				if !defined(VR)
	float depthMul = length(float3((depthOffset * 2 - 1) * depth / ProjData.xy, depth));
#				else
	float VRDepth = GetScreenDepthWater(screenPosition, 1);  // VR uses special hardcoded depth for this calculation
	float depthMul = calculateDepthMultfromUV(ConvertFromStereoUV(depthOffset, eyeIndex, 1), VRDepth, eyeIndex);
#				endif  //VR
	float3 depthAdjustedViewDirection = -viewDirection * depthMul;
	float viewSurfaceAngle = dot(depthAdjustedViewDirection, ReflectPlane[eyeIndex].xyz);

	float planeMul = (1 - ReflectPlane[eyeIndex].w / viewSurfaceAngle);
	distanceMul = saturate(
		planeMul * float4(length(depthAdjustedViewDirection).xx, abs(viewSurfaceAngle).xx) /
		FogParam.z);
#			endif
#		endif

#		if defined(UNDERWATER)
	float4 depthControl = float4(0, 1, 1, 0);
#		elif defined(LOD)
	float4 depthControl = float4(1, 0, 0, 1);
#		elif defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float4 depthControl = float4(0, 0, 1, 0);
#		else
	float4 depthControl = DepthControl * (distanceMul - 1) + 1;
#		endif

#		if defined(WATER_CAUSTICS)
	float3 caustics = 0.0;
	float3 normal = GetWaterNormal(input, distanceFactor, depthControl.z, viewDirection, depth, caustics, eyeIndex);
#		else
	float3 normal = GetWaterNormal(input, distanceFactor, depthControl.z, viewDirection, depth, eyeIndex);
#		endif

	float fresnel = GetFresnelValue(normal, viewDirection);

#		if defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float3 finalColor = 0.0.xxx;

	for (int lightIndex = 0; lightIndex < NUM_SPECULAR_LIGHTS; ++lightIndex) {
		float3 lightVector = LightPos[lightIndex].xyz - (PosAdjust[eyeIndex].xyz + input.WPosition.xyz, eyeIndex);
		float3 lightDirection = normalize(normalize(lightVector) - viewDirection);
		float lightFade = saturate(length(lightVector) / LightPos[lightIndex].w);
		float lightColorMul = (1 - lightFade * lightFade);
		float LdotN = saturate(dot(lightDirection, normal));
		float3 lightColor = (LightColor[lightIndex].xyz * pow(LdotN, FresnelRI.z)) * lightColorMul;
		finalColor += lightColor;
	}

	finalColor *= fresnel;

	isSpecular = true;
#		else

	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, depthControl.y, eyeIndex);
#			if defined(WATER_CAUSTICS)
	float3 diffuseColor = GetWaterDiffuseColor(input, normal, viewDirection, distanceMul, depthControl.y, fresnel, caustics, eyeIndex);

#			else
	float3 diffuseColor = GetWaterDiffuseColor(input, normal, viewDirection, distanceMul, depthControl.y, fresnel, eyeIndex);
#			endif

	float3 specularLighting = 0;

#			if defined(LIGHT_LIMIT_FIX)
	uint lightCount = 0;

	float3 viewPosition = mul(CameraView[eyeIndex], float4(input.WPosition.xyz, 1)).xyz;
	float2 screenUV = ViewToUV(viewPosition, true, eyeIndex);

	uint clusterIndex = 0;
	if (perPassLLF[0].EnableGlobalLights && GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = lightGrid[clusterIndex].lightCount;
		uint lightOffset = lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint light_index = lightList[lightOffset + i];
			StructuredLight light = lights[light_index];

			float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WPosition.xyz;
			float lightDist = length(lightDirection);
			float intensityFactor = saturate(lightDist / light.radius);

			float intensityMultiplier = 1 - intensityFactor * intensityFactor;

			float3 normalizedLightDirection = normalize(lightDirection);

			float3 H = normalize(normalizedLightDirection - viewDirection);
			float HdotN = saturate(dot(H, normal));

			float3 lightColor = light.color.xyz * pow(HdotN, FresnelRI.z);
			specularLighting += lightColor * intensityMultiplier;
		}
	}
	specularColor += specularLighting * 3;
#			endif

#			if defined(UNDERWATER)
	float3 finalSpecularColor = lerp(ShallowColor.xyz, specularColor, 0.5);
	float3 finalColor = saturate(1 - input.WPosition.w * 0.002) *
	                        ((1 - fresnel) * (diffuseColor - finalSpecularColor)) +
	                    finalSpecularColor;
#			else
	float3 sunColor = GetSunColor(normal, viewDirection);
	float specularFraction = lerp(1, fresnel * depthControl.x, distanceFactor);
	float3 finalColorPreFog = lerp(diffuseColor, specularColor, specularFraction) + sunColor * depthControl.w;
	float3 finalColor = lerp(finalColorPreFog, input.FogParam.xyz, input.FogParam.w);
#			endif
#		endif

	psout.Lighting = saturate(float4(finalColor * PosAdjust[eyeIndex].w, isSpecular));
#		if defined(WATER_BLENDING)
#			if defined(DEPTH)
	if (perPassWaterBlending[0].EnableWaterBlending) {
		float2 screenPosition = DynamicResolutionParams1.xy * (DynamicResolutionParams2.xy * input.HPosition.xy);
#				if defined(VERTEX_ALPHA_DEPTH) && defined(VC)
		float blendFactor = 1 - smoothstep(0.0, 0.025 * perPassWaterBlending[0].WaterBlendRange, input.TexCoord3.z);
#				else
		float blendFactor = 1 - smoothstep(0.0, 0.025 * perPassWaterBlending[0].WaterBlendRange, distanceMul.z);
#				endif  // defined(VERTEX_ALPHA_DEPTH) && defined(VC)
		if (blendFactor > 0.0) {
			float4 background = RefractionTex.Load(float3(screenPosition, 0));
			psout.Lighting.xyz = lerp(psout.Lighting.xyz, background.xyz, blendFactor);
			psout.Lighting.w = lerp(psout.Lighting.w, background.w, blendFactor);
		}
	}
#			endif
#		endif

#	endif

#	if defined(STENCIL)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	float3 normal =
		normalize(cross(ddx_coarse(input.WorldPosition.xyz), ddy_coarse(input.WorldPosition.xyz)));
	float VdotN = dot(viewDirection, normal);
	psout.WaterMask = float4(0, 0, VdotN, 0);

	psout.MotionVector = GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
#	endif

	return psout;
}

#endif
