#ifdef VR
cbuffer VRValues : register(b13)
{
	float AlphaTestRefRS : packoffset(c0);
	float StereoEnabled : packoffset(c0.y);
	float2 EyeOffsetScale : packoffset(c0.z);
	float4 EyeClipEdge[2] : packoffset(c1);
}
#endif

/**
Converts to the eye specific uv [0,1].
In VR, texture buffers include the left and right eye in the same buffer. Flat
only has a single camera for the entire width. This means the x value [0, .5]
represents the left eye, and the x value (.5, 1] are the right eye. This returns
the adjusted value
@param uv - uv coords [0,1] to be encoded for VR
@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
@param a_invertY Whether to invert the Y direction
@returns uv with x coords adjusted for the VR texture buffer
*/
float2 ConvertToStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
{
#ifdef VR
	// convert [0,1] to eye specific [0,.5] and [.5, 1] dependent on a_eyeIndex
	uv.x = (uv.x + (float)a_eyeIndex) / 2;
	if (a_invertY)
		uv.y = 1 - uv.y;
#endif
	return uv;
}

/**
Converts from eye specific uv to general uv [0,1].
In VR, texture buffers include the left and right eye in the same buffer. 
This means the x value [0, .5] represents the left eye, and the x value (.5, 1] are the right eye.
This returns the adjusted value
@param uv - eye specific uv coords [0,1]; if uv.x < 0.5, it's a left eye; otherwise right
@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
@param a_invertY Whether to invert the Y direction
@returns uv with x coords adjusted to full range for either left or right eye
*/
float2 ConvertFromStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
{
#ifdef VR
	// convert [0,.5] to [0, 1] and [.5, 1] to [0,1]
	uv.x = 2 * uv.x - (float)a_eyeIndex;
	if (a_invertY)
		uv.y = 1 - uv.y;
#endif
	return uv;
}

/**
Converts to the eye specific screenposition [0,Resolution].
In VR, texture buffers include the left and right eye in the same buffer. Flat only has a single camera for the entire width.
This means the x value [0, resx/2] represents the left eye, and the x value (resx/2, x] are the right eye.
This returns the adjusted value
@param screenPosition - Screenposition coords ([0,resx], [0,resy]) to be encoded for VR
@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
@param a_resolution The resolution of the screen
@returns screenPosition with x coords adjusted for the VR texture buffer
*/
float2 ConvertToStereoSP(float2 screenPosition, uint a_eyeIndex, float2 a_resolution)
{
	screenPosition.x /= a_resolution.x;
	return ConvertToStereoUV(screenPosition, a_eyeIndex) * a_resolution;
}

#ifdef PSHADER
/**
Gets the eyeIndex for PSHADER
@returns eyeIndex (0 left, 1 right)
*/
uint GetEyeIndexPS(float4 position, float4 offset = 0.0.xxxx)
{
#	if !defined(VR)
	uint eyeIndex = 0;
#	else
	float4 stereoUV;
	stereoUV.xy = position.xy * offset.xy + offset.zw;
	stereoUV.x = DynamicResolutionParams2.x * stereoUV.x;
	stereoUV.x = (stereoUV.x >= 0.5);
	uint eyeIndex = (uint)(((int)((uint)StereoEnabled)) * (int)stereoUV.x);
#	endif
	return eyeIndex;
}
#endif  //PSHADER

/**
Gets the eyeIndex for Compute Shaders
@param texCoord Texcoord on the screen [0,1]
@returns eyeIndex (0 left, 1 right)
*/
uint GetEyeIndexFromTexCoord(float2 texCoord)
{
#ifdef VR
	return (texCoord.x >= 0.5) ? 1 : 0;
#endif  // VR
	return 0;
}

struct VR_OUTPUT
{
	float4 VRPosition;
	float ClipDistance;
	float CullDistance;
};

#ifdef VSHADER
/**
Gets the eyeIndex for VSHADER
@returns eyeIndex (0 left, 1 right)
*/
uint GetEyeIndexVS(uint instanceID = 0)
{
#	ifdef VR
	return StereoEnabled * (instanceID & 1);
#	endif  // VR
	return 0;
}

/**
Gets VR Output
@param texCoord Texcoord on the screen [0,1]
@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
@returns VR_OUTPUT with VR values
*/
VR_OUTPUT GetVRVSOutput(float4 clipPos, uint a_eyeIndex = 0)
{
	VR_OUTPUT vsout;
	vsout.VRPosition = 0.0.xxxx;
	vsout.ClipDistance = 0.0;
	vsout.CullDistance = 0.0;
#	ifdef VR
	float4 r0;
	r0.xyzw = 0;
	if (0 < StereoEnabled) {
		r0.yz = dot(clipPos, EyeClipEdge[a_eyeIndex]);
	} else {
		r0.yz = float2(1, 1);
	}

	r0.w = 2 + -StereoEnabled;
	r0.x = dot(EyeOffsetScale, M_IdentityMatrix[a_eyeIndex].xy);
	r0.xw = r0.xw * clipPos.wx;
	r0.x = StereoEnabled * r0.x;

	vsout.VRPosition.x = r0.w * 0.5 + r0.x;
	vsout.VRPosition.yzw = clipPos.yzw;

	vsout.ClipDistance.x = r0.z;
	vsout.CullDistance.x = r0.y;
#	endif  // VR
	return vsout;
}
#endif