const vec3 kRec709 = vec3(0.2126f, 0.7152f, 0.0722f);

vec2 Rotate(vec2 f2, float f)
{
	float fSin = sin(f);
	float fCos = cos(f);
	mat2 matRotation = mat2(fCos, -fSin, fSin, fCos);
	return matRotation * f2;
}

vec4 Transform(vec4 f4Vec, vec4[4] f4x4Matrix)
{
	return vec4(dot(f4Vec, f4x4Matrix[0]), dot(f4Vec, f4x4Matrix[1]), dot(f4Vec, f4x4Matrix[2]), dot(f4Vec, f4x4Matrix[3]));
}

vec3 Transform(vec4 f4Vec, vec4[3] f3x4Matrix)
{
	return vec3(dot(f4Vec, f3x4Matrix[0]), dot(f4Vec, f3x4Matrix[1]), dot(f4Vec, f3x4Matrix[2]));
}

vec2 WorldToVisibleArea(vec3 f3WorldPosition, vec4 f4VisibleArea)
{
	float fMultiplierX = 1.0f / (f4VisibleArea.z - f4VisibleArea.x);
	float fMultiplierY = 1.0f / (f4VisibleArea.y - f4VisibleArea.w);
	return vec2(fMultiplierX * (f3WorldPosition.x - f4VisibleArea.x), 1.0f - fMultiplierY * (f3WorldPosition.y - f4VisibleArea.w));
}

// Inverse of WorldToVisibleArea (incl. its Y-flip): a [0,1] texel UV back to world XY for the given area.
vec2 VisibleAreaToWorld(vec2 f2Uv, vec4 f4VisibleArea)
{
	return vec2(f4VisibleArea.x + f2Uv.x * (f4VisibleArea.z - f4VisibleArea.x),
	            f4VisibleArea.w + (1.0f - f2Uv.y) * (f4VisibleArea.y - f4VisibleArea.w));
}

vec4 ShadowStretchProjection(GlobalLayout globalLayout, vec3 f3WorldPosition, vec3 f3ObjectPosition)
{
	// Cubes and the -normal.xy base translation are uniform-only and folded CPU-side (GlobalUniforms.cpp
	// PopulateShadowStretch); only the varying fStretchX (position-diff dependent) stays here.
	float fSunriseOffsetCubed = globalLayout.fShadowSunriseStretchCubed;
	float fSunsetOffsetCubed = globalLayout.fShadowSunsetStretchCubed;
	vec2 f2Translation = globalLayout.f2ShadowStretchTranslation;
	float fSunriseDiff = max(0.0f, f3ObjectPosition.x - f3WorldPosition.x);
	float fSunsetDiff = max(0.0f, f3WorldPosition.x - f3ObjectPosition.x);
	float fStretchX = -(0.5f + fSunriseDiff) * fSunriseOffsetCubed + (0.5f + fSunsetDiff) * fSunsetOffsetCubed;
	vec3 f3ShadowPosition = vec3(f3WorldPosition.x + f2Translation.x + fStretchX, f3WorldPosition.y + f2Translation.y, f3WorldPosition.z);
	vec2 f2VisibleAreaUV = WorldToVisibleArea(f3ShadowPosition, globalLayout.f4VisibleArea);
	return vec4(2.0f * f2VisibleAreaUV.x - 1.0f, 1.0f - 2.0f * f2VisibleAreaUV.y, 0.0f, 1.0f);
}

vec2 BaseHeightPosition(GlobalLayout globalLayout, MainLayout mainLayout, vec3 f3InPosition)
{
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f3InPosition);
	const float fBaseHeight = globalLayout.fBaseHeight;
	float fMult = (fBaseHeight - f3InPosition.z) / f3ToEyeNormal.z;
	return (f3InPosition + max(fMult, 0.0f) * f3ToEyeNormal).xy;
}

vec3 SampleNormal(GlobalLayout globalLayout, sampler2D normalSampler, vec2 f2Position, float fSize, float fSpeed, vec2 f2Offset, vec2 f2PositionGradX, vec2 f2PositionGradY)
{
	// BC5 normal map: only RG stored, standard convention (encoded=0.5 -> 0, encoded=1 -> +1).
	vec2 f2RG = textureGrad(normalSampler, f2Offset + fSize * f2Position + fSpeed * vec2(globalLayout.fElapsedTime, globalLayout.fElapsedTime), fSize * f2PositionGradX, fSize * f2PositionGradY).rg;
	vec2 f2XY = 2.0f * f2RG - 1.0f;
	float fZ = sqrt(clamp(1.0f - dot(f2XY, f2XY), 0.0f, 1.0f));
	return vec3(f2XY, fZ);
}

// fShadowSun gates the sun's directional contribution; fShadowMoon gates the moon's directional
// contribution. Callers compute fShadowMoon to bypass the terrain ray-march shadow (per the
// sun/moon split design) so moonlight is unaffected by landform self-shadowing while object/smoke
// shadows still apply to both lights. Ambient gating tracks the dominant light's shadow via a
// Rec.601 luma-weighted blend: at noon by sun-shadow, at midnight by moon-shadow (which excludes
// the terrain ray-march), with a smooth transition in twilight. Without this weighting, terrain
// in self-shadow visibly darkens during sunset/sunrise even though the moon is filling those
// regions.
vec3 SunLighting(vec3 f3MaterialColor, GlobalLayout globalLayout, vec4 f4Position, vec3 f3Normal, float fShadowSun, float fShadowMoon, float fAmbientOcclusion)
{
	// Terrain-specific helper: only Terrain.frag calls SunLighting. Per-target-scaled colors, their Rec.601
	// magnitudes, and the ambient split are folded CPU-side (GlobalUniforms.cpp), so only the shadow-dependent
	// terms remain per-pixel. f3Normal must be unit: the sole caller passes an already-normalized f3SunNormal,
	// so the redundant normalize is skipped (shader AGENTS.md normalization-skip rule).
	vec3 f3Sun  = globalLayout.f4SunColorTerrain.xyz;
	vec3 f3Moon = globalLayout.f4MoonColorTerrain.xyz;
	float fNdotL = max(0.0f, dot(f3Normal, globalLayout.f4SunMoonNormal.xyz));
	vec3 f3SunLight  = fShadowSun  * fNdotL * f3Sun;
	vec3 f3MoonLight = fShadowMoon * fNdotL * f3Moon;
	float fAmbientShadow = (globalLayout.fSunMagTerrain * fShadowSun + globalLayout.fMoonMagTerrain * fShadowMoon) * globalLayout.fSunMoonMagSumInvTerrain;
	return f3MaterialColor * fAmbientOcclusion * (max(f3SunLight, f3MoonLight) + globalLayout.f4AmbientUnshadowed.xyz + fAmbientShadow * globalLayout.f4AmbientShadowed.xyz);
}

float Specular(vec3 f3ToEyeNormal, vec3 f3LightNormal, vec3 f3Normal, float fSpecularOne, float fSpecularOnePower, float fSpecularTwo, float fSpecularTwoPower, float fSpecularThree, float fSpecularThreePower)
{
	vec3 f3LightReflectionNormal = reflect(f3LightNormal, f3Normal);
	float fSpecularFactor = dot(f3ToEyeNormal, f3LightReflectionNormal);
	if (fSpecularFactor <= 0.0f)
		return 0.0f;

	float fLog2 = log2(fSpecularFactor);
	return fSpecularOne   * exp2(fSpecularOnePower   * fLog2) +
	       fSpecularTwo   * exp2(fSpecularTwoPower   * fLog2) +
	       fSpecularThree * exp2(fSpecularThreePower * fLog2);
}

float IntensityLighting(vec4 f4Lighting)
{
	return f4Lighting.x + f4Lighting.y + f4Lighting.z + f4Lighting.w;
}

// The transparent-black border sampler returns no lighting outside the footprint, so these are plain reads.
void ReadLighting(inout vec4 pf4Lighting[3], sampler2D pLightingSamplers[3], vec2 f2Texcoord)
{
	pf4Lighting[0] = texture(pLightingSamplers[0], f2Texcoord);
	pf4Lighting[1] = texture(pLightingSamplers[1], f2Texcoord);
	pf4Lighting[2] = texture(pLightingSamplers[2], f2Texcoord);
}

vec3 ReadAmbientLighting(sampler2D ambientLightingSampler, vec2 f2Texcoord)
{
	return texture(ambientLightingSampler, f2Texcoord).xyz;
}

float Sum(vec4 pf4Lighting[3])
{
	return pf4Lighting[0].x + pf4Lighting[0].y + pf4Lighting[0].z + pf4Lighting[0].w + pf4Lighting[1].x + pf4Lighting[1].y + pf4Lighting[1].z + pf4Lighting[1].w + pf4Lighting[2].x + pf4Lighting[2].y + pf4Lighting[2].z + pf4Lighting[2].w;
}

vec3 DirectionalLighting(vec4 pf4Lighting[3], vec3 f3Normal, float fIntensity, float fPower, float fPowerMode)
{
	// Guard exact (0,0,1) normals (flat-top geometry): normalize() of a zero-length xy NaNs. Zero weights = no
	// directional contribution, the intended degenerate behavior.
	float fLen = length(f3Normal.xy);
	vec2 f2Normal = fLen > kfEpsilon ? f3Normal.xy / fLen : vec2(0.0f);
	float fWeightE = max(0.0f, f2Normal.x);
	float fWeightW = max(0.0f, -f2Normal.x);
	float fWeightN = max(0.0f, f2Normal.y);
	float fWeightS = max(0.0f, -f2Normal.y);

	vec3 f3Result = vec3(pf4Lighting[0].x * fWeightE + pf4Lighting[0].y * fWeightW + pf4Lighting[0].z * fWeightN + pf4Lighting[0].w * fWeightS,
		                 pf4Lighting[1].x * fWeightE + pf4Lighting[1].y * fWeightW + pf4Lighting[1].z * fWeightN + pf4Lighting[1].w * fWeightS,
		                 pf4Lighting[2].x * fWeightE + pf4Lighting[2].y * fWeightW + pf4Lighting[2].z * fWeightN + pf4Lighting[2].w * fWeightS);

	// Skip the pow for whichever branch the mode discards at its extremes (fPowerMode is a uniform, so the branch is warp-coherent)
	bool bNeedLum = fPowerMode < 0.999f;
	bool bNeedAvg = fPowerMode > 0.001f;

	float fLuminance = dot(f3Result, kRec709);
	vec3 f3LumDir = f3Result / max(fLuminance, 0.001f);
	vec3 f3LumResult = bNeedLum ? (fIntensity * pow(fLuminance, fPower) * f3LumDir) : vec3(0.0f);

	float fAverage = (f3Result.x + f3Result.y + f3Result.z) / 3.0f;
	vec3 f3AvgDir = f3Result / max(fAverage, 0.001f);
	vec3 f3AvgResult = bNeedAvg ? (fIntensity * pow(fAverage, fPower) * f3AvgDir) : vec3(0.0f);

	return mix(f3LumResult, f3AvgResult, fPowerMode);
}

vec3 WaterLighting(vec4 pf4Lighting[3], vec3 f3Normal, float fSoften, float fOne, float fOnePower, float fTwo, float fTwoPower, float fThree, float fThreePower, float fPowerMode)
{
	// Guard exact (0,0,1) normals (flat-top geometry): normalize() of a zero-length xy NaNs. Zero weights = no
	// directional contribution, the intended degenerate behavior.
	float fLen = length(f3Normal.xy);
	vec2 f2Normal = fLen > kfEpsilon ? f3Normal.xy / fLen : vec2(0.0f);
	float fWeightE = mix(max(0.0f, f2Normal.x), 0.25f, fSoften);
	float fWeightW = mix(max(0.0f, -f2Normal.x), 0.25f, fSoften);
	float fWeightN = mix(max(0.0f, f2Normal.y), 0.25f, fSoften);
	float fWeightS = mix(max(0.0f, -f2Normal.y), 0.25f, fSoften);

	vec3 f3Result = vec3(pf4Lighting[0].x * fWeightE + pf4Lighting[0].y * fWeightW + pf4Lighting[0].z * fWeightN + pf4Lighting[0].w * fWeightS,
		                 pf4Lighting[1].x * fWeightE + pf4Lighting[1].y * fWeightW + pf4Lighting[1].z * fWeightN + pf4Lighting[1].w * fWeightS,
		                 pf4Lighting[2].x * fWeightE + pf4Lighting[2].y * fWeightW + pf4Lighting[2].z * fWeightN + pf4Lighting[2].w * fWeightS);

	// Skip the three pow calls for whichever branch the mode discards at its extremes (fPowerMode is a uniform, so the branch is warp-coherent)
	bool bNeedLum = fPowerMode < 0.999f;
	bool bNeedAvg = fPowerMode > 0.001f;

	float fLuminance = dot(f3Result, kRec709);
	vec3 f3LumDir = f3Result / max(fLuminance, 0.001f);
	float fLumScalar = bNeedLum ? (fOne * pow(fLuminance, fOnePower) + fTwo * pow(fLuminance, fTwoPower) + fThree * pow(fLuminance, fThreePower)) : 0.0f;
	vec3 f3LumResult = fLumScalar * f3LumDir;

	float fAverage = (f3Result.x + f3Result.y + f3Result.z) / 3.0f;
	vec3 f3AvgDir = f3Result / max(fAverage, 0.001f);
	float fAvgScalar = bNeedAvg ? (fOne * pow(fAverage, fOnePower) + fTwo * pow(fAverage, fTwoPower) + fThree * pow(fAverage, fThreePower)) : 0.0f;
	vec3 f3AvgResult = fAvgScalar * f3AvgDir;

	return mix(f3LumResult, f3AvgResult, fPowerMode);
}

// Operates on a precomputed direction-averaged sum (e.g. mAmbientCombineTexture sample).
vec3 AmbientLightingPrecomputed(vec3 f3Result, float fIntensity, float fPower, float fPowerMode)
{
	// Skip the pow for whichever branch the mode discards at its extremes (fPowerMode is a uniform, so the branch is warp-coherent)
	bool bNeedLum = fPowerMode < 0.999f;
	bool bNeedAvg = fPowerMode > 0.001f;

	float fLuminance = dot(f3Result, kRec709);
	vec3 f3LumDir = f3Result / max(fLuminance, 0.001f);
	vec3 f3LumResult = bNeedLum ? (fIntensity * pow(fLuminance, fPower) * f3LumDir) : vec3(0.0f);

	float fAverage = (f3Result.x + f3Result.y + f3Result.z) / 3.0f;
	vec3 f3AvgDir = f3Result / max(fAverage, 0.001f);
	vec3 f3AvgResult = bNeedAvg ? (fIntensity * pow(fAverage, fPower) * f3AvgDir) : vec3(0.0f);

	return mix(f3LumResult, f3AvgResult, fPowerMode);
}

vec3 AmbientLighting(vec4 pf4Lighting[3], float fIntensity, float fPower, float fPowerMode)
{
	vec3 f3Result = 0.25f * vec3(pf4Lighting[0].x + pf4Lighting[0].y + pf4Lighting[0].z + pf4Lighting[0].w,
		                         pf4Lighting[1].x + pf4Lighting[1].y + pf4Lighting[1].z + pf4Lighting[1].w,
		                         pf4Lighting[2].x + pf4Lighting[2].y + pf4Lighting[2].z + pf4Lighting[2].w);
	return AmbientLightingPrecomputed(f3Result, fIntensity, fPower, fPowerMode);
}

vec2 WorldToSmokeTexcoord(vec4 f4SmokeArea, vec2 f2Position)
{
	return vec2((f2Position.x - f4SmokeArea.x) / (f4SmokeArea.z - f4SmokeArea.x),
		        (f2Position.y - f4SmokeArea.y) / (f4SmokeArea.w - f4SmokeArea.y));
}

// The border-white sampler returns unshadowed outside the footprint, so this is a plain read.
float SampleTerrainShadow(sampler2D shadowTextureSampler, vec2 f2Uv)
{
	return textureLod(shadowTextureSampler, f2Uv, 0.0f).x;
}

float SmokeShadow(GlobalLayout globalLayout, vec3 f3InPosition, sampler2D smokeSampler, float fMulti)
{
	vec2 f2SmokeTexcoord = WorldToSmokeTexcoord(globalLayout.f4SmokeArea, f3InPosition.xy);
	float fSmokeShadow = globalLayout.fSmokeMax * texture(smokeSampler, f2SmokeTexcoord).x;
	fSmokeShadow = clamp(pow(fSmokeShadow, globalLayout.fSmokePower), 0.0f, 1.0f);
	return 1.0f - fMulti * fSmokeShadow;
}

vec3 AddSmoke(GlobalLayout globalLayout, vec3 f3InColor, vec2 f2InPosition, sampler2D smokeSampler, float fInMax, vec4 pf4Lighting[3])
{
	vec2 f2SmokeTexcoord = WorldToSmokeTexcoord(globalLayout.f4SmokeArea, f2InPosition);
	float fSmoke = globalLayout.fSmokeMax * texture(smokeSampler, f2SmokeTexcoord).x;
	fSmoke = clamp(pow(fSmoke, globalLayout.fSmokePower), 0.0f, 1.0f);
	float fDensity = globalLayout.fSmokeColorMin + globalLayout.fSmokeColorMultiplier * fSmoke;
	fSmoke *= fInMax;

	float fRed = IntensityLighting(pf4Lighting[0]);
	float fGreen = IntensityLighting(pf4Lighting[1]);
	float fBlue = IntensityLighting(pf4Lighting[2]);
	// fSmokeLightingCombinedMultiplier (= fSmokeLightingMultiplier * fLightingTimeOfDayMultiplier) and
	// f4SmokeBaseLighting (max(sun,moon) sky term + ambient) are folded CPU-side (GlobalUniforms.cpp).
	vec3 f3Final = vec3(fRed, fGreen, fBlue) * globalLayout.fSmokeLightingCombinedMultiplier;
	f3Final += globalLayout.f4SmokeBaseLighting.xyz;

	f3Final = min(vec3(1.0f, 1.0f, 1.0f), f3Final);
	return (1.0f - fSmoke) * f3InColor + fSmoke * f3Final * min(vec3(1.25f, 1.25f, 1.25f), vec3(fDensity, fDensity, fDensity));
}

vec3 BlendSmoke(vec3 f3Color, float fSmokePow, vec4 pf4Lighting[3], GlobalLayout globalLayout)
{
	float fSmokeDensity = globalLayout.fSmokeColorMin + globalLayout.fSmokeColorMultiplier * fSmokePow;
	float fRed = IntensityLighting(pf4Lighting[0]);
	float fGreen = IntensityLighting(pf4Lighting[1]);
	float fBlue = IntensityLighting(pf4Lighting[2]);
	vec3 f3SmokeLighting = vec3(fRed, fGreen, fBlue) * globalLayout.fSmokeLightingMultiplier * globalLayout.fLightingTimeOfDayMultiplier;
	f3SmokeLighting += max(globalLayout.fSunIntensitySmoke * globalLayout.f4SunColor.xyz, globalLayout.fMoonIntensitySmoke * globalLayout.f4MoonColor.xyz) + globalLayout.f4AmbientColor.xyz;
	f3SmokeLighting = min(vec3(1.0f), f3SmokeLighting);
	return (1.0f - fSmokePow) * f3Color + fSmokePow * f3SmokeLighting * min(vec3(1.25f), vec3(fSmokeDensity));
}

// Caller passes 4.0 * mAmbientCombineTexture sample (recovers IntensityLighting magnitude — the texture stores 0.25*sum).
vec3 BlendSmokePrecomputed(vec3 f3Color, float fSmokePow, vec3 f3LightingSum, GlobalLayout globalLayout)
{
	float fSmokeDensity = globalLayout.fSmokeColorMin + globalLayout.fSmokeColorMultiplier * fSmokePow;
	vec3 f3SmokeLighting = f3LightingSum * globalLayout.fSmokeLightingCombinedMultiplier;
	f3SmokeLighting += globalLayout.f4SmokeBaseLighting.xyz;
	f3SmokeLighting = min(vec3(1.0f), f3SmokeLighting);
	return (1.0f - fSmokePow) * f3Color + fSmokePow * f3SmokeLighting * min(vec3(1.25f), vec3(fSmokeDensity));
}

float LightingDepositEdgeFade(vec2 f2FragCoord, vec2 f2SizeInv)
{
	vec2 f2Uv = f2FragCoord * f2SizeInv;
	float fEdgeDist = min(min(f2Uv.x, 1.0f - f2Uv.x), min(f2Uv.y, 1.0f - f2Uv.y));
	return smoothstep(0.0f, 0.05f, fEdgeDist);
}
