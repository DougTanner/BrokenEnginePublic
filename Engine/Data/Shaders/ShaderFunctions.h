vec4 CalculateDirectionalLight(vec2 f2Position)
{
	const vec2 f2Center = vec2(0.5f, 0.5f);
	vec2 f2Direction = f2Position - f2Center;
	vec2 f2AbsDirection = abs(f2Direction);
	float fMaxDistance = max(f2AbsDirection.x, f2AbsDirection.y);
	vec2 f2NormalizedDirection = fMaxDistance > 0.0f ? f2Direction / fMaxDistance : vec2(0.0f, 0.0f);
	return vec4(max(f2NormalizedDirection.x, 0.0f), max(-f2NormalizedDirection.x, 0.0f), max(f2NormalizedDirection.y, 0.0f), max(-f2NormalizedDirection.y, 0.0f));
}

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

vec4 ShadowStretchProjection(GlobalLayout globalLayout, vec3 f3WorldPosition, vec3 f3ObjectPosition)
{
	float fSunriseOffset = globalLayout.fShadowSunriseStretch;
	float fSunsetOffset = globalLayout.fShadowSunsetStretch;
	float fSunriseOffsetCubed = fSunriseOffset * fSunriseOffset * fSunriseOffset;
	float fSunsetOffsetCubed = fSunsetOffset * fSunsetOffset * fSunsetOffset;
	vec2 f2Translation = (fSunriseOffsetCubed + fSunsetOffsetCubed) * -globalLayout.f4SunNormal.xy;
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

vec3 SampleNormal(GlobalLayout globalLayout, sampler2D normalSampler, vec2 f2Position, float fSize, float fSpeed, vec2 f2Offset)
{
	vec3 f3SampledNormal = texture(normalSampler, f2Offset + fSize * f2Position + fSpeed * vec2(globalLayout.fElapsedTime, globalLayout.fElapsedTime)).xyz;
	return vec3(1.0f - 2.0f * f3SampledNormal.x, 1.0f - 2.0f * f3SampledNormal.y, f3SampledNormal.z);
}

vec3 SunLighting(vec3 f3MaterialColor, GlobalLayout globalLayout, vec4 f4Position, vec3 f3Normal, float fShadow, float fAmbientOcclusion)
{
	vec3 f3SunLight = fShadow * max(0.0f, dot(normalize(f3Normal), globalLayout.f4SunNormal.xyz)) * globalLayout.f4SunColor.xyz;
	float fShadowAffectAmbient = globalLayout.fShadowAffectAmbient;
	return f3MaterialColor * fAmbientOcclusion * (f3SunLight + (1.0f - fShadowAffectAmbient) * globalLayout.f4AmbientColor.xyz + fShadowAffectAmbient * fShadow * globalLayout.f4AmbientColor.xyz);
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

void ReadLighting(inout vec4 pf4Lighting[3], sampler2D pLightingSamplers[3], vec2 f2Texcoord)
{
	pf4Lighting[0] = texture(pLightingSamplers[0], f2Texcoord);
	pf4Lighting[1] = texture(pLightingSamplers[1], f2Texcoord);
	pf4Lighting[2] = texture(pLightingSamplers[2], f2Texcoord);
}

float Sum(vec4 pf4Lighting[3])
{
	return pf4Lighting[0].x + pf4Lighting[0].y + pf4Lighting[0].z + pf4Lighting[0].w + pf4Lighting[1].x + pf4Lighting[1].y + pf4Lighting[1].z + pf4Lighting[1].w + pf4Lighting[2].x + pf4Lighting[2].y + pf4Lighting[2].z + pf4Lighting[2].w;
}

vec3 Lighting(GlobalLayout globalLayout, vec3 f3Color, float fHeight, vec3 f3Normal, vec4 pf4Lighting[3], float fIntensity, float fAdd)
{
	float fDirectionalAdd = globalLayout.fLightingDirectional * (1.0f - f3Normal.z);

	const float fBaseHeight = globalLayout.fBaseHeight;
	const float fFalloff = 2.0f * fBaseHeight;
	float fHeightPercent = clamp(abs(fHeight - fBaseHeight) / fFalloff, 0.0f, 1.0f);

	// Direct — component extraction instead of dot(normalize(axis), normal)
	float fDirectE = max(0.0f, -f3Normal.x);
	float fDirectW = max(0.0f, f3Normal.x);
	float fDirectN = max(0.0f, -f3Normal.y);
	float fDirectS = max(0.0f, f3Normal.y);

	// Indirect — bent normals (normalize needed on right operand only)
	float fHeightBend = globalLayout.fLightingIndirect;
	float fIndirectE = max(0.0f, -normalize(f3Normal + vec3(-fHeightBend, 0.0f, 0.0f)).x);
	float fIndirectW = max(0.0f, normalize(f3Normal + vec3(fHeightBend, 0.0f, 0.0f)).x);
	float fIndirectN = max(0.0f, -normalize(f3Normal + vec3(0.0f, -fHeightBend, 0.0f)).y);
	float fIndirectS = max(0.0f, normalize(f3Normal + vec3(0.0f, fHeightBend, 0.0f)).y);

	// Weight — combines direct, indirect, and directional add
	float fOneMinusHeight = 1.0f - fHeightPercent;
	float fWeightE = fOneMinusHeight * (fOneMinusHeight * fDirectE + fHeightPercent * fIndirectE + fDirectionalAdd * fDirectE);
	float fWeightW = fOneMinusHeight * (fOneMinusHeight * fDirectW + fHeightPercent * fIndirectW + fDirectionalAdd * fDirectW);
	float fWeightN = fOneMinusHeight * (fOneMinusHeight * fDirectN + fHeightPercent * fIndirectN + fDirectionalAdd * fDirectN);
	float fWeightS = fOneMinusHeight * (fOneMinusHeight * fDirectS + fHeightPercent * fIndirectS + fDirectionalAdd * fDirectS);

	// Apply weights to all 3 channels at once
	vec3 f3LightingColor = fIntensity * vec3(
		pf4Lighting[0].x * fWeightE + pf4Lighting[0].y * fWeightW + pf4Lighting[0].z * fWeightN + pf4Lighting[0].w * fWeightS,
		pf4Lighting[1].x * fWeightE + pf4Lighting[1].y * fWeightW + pf4Lighting[1].z * fWeightN + pf4Lighting[1].w * fWeightS,
		pf4Lighting[2].x * fWeightE + pf4Lighting[2].y * fWeightW + pf4Lighting[2].z * fWeightN + pf4Lighting[2].w * fWeightS);

	vec3 f3Final = fAdd * f3LightingColor + (1.0f - fAdd) * f3LightingColor * f3Color;
	float fPower = globalLayout.fLightingCombinePower;
	f3Final = pow(vec3(1.0f) + f3Final, vec3(fPower)) - vec3(1.0f);
	f3Final = min(f3Final, vec3(1.0f));
	return f3Final;
}

vec3 SpecularLighting(GlobalLayout globalLayout, MainLayout mainLayout, vec3 f3Color, vec3 f3Position, vec3 fDirect3Normal, vec3 f3SpecularNormal, vec4 pf4Lighting[3], float fIntensity, float fAdd)
{
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f3Position);

	// Diffuse (sum of EWNS weights per channel)
	vec3 f3Diffuse = vec3(
		pf4Lighting[0].x + pf4Lighting[0].y + pf4Lighting[0].z + pf4Lighting[0].w,
		pf4Lighting[1].x + pf4Lighting[1].y + pf4Lighting[1].z + pf4Lighting[1].w,
		pf4Lighting[2].x + pf4Lighting[2].y + pf4Lighting[2].z + pf4Lighting[2].w);

	// Direct — component extraction instead of dot(normalize(axis), normal)
	float fDirectE = max(0.0f, -fDirect3Normal.x);
	float fDirectW = max(0.0f, fDirect3Normal.x);
	float fDirectN = max(0.0f, -fDirect3Normal.y);
	float fDirectS = max(0.0f, fDirect3Normal.y);
	vec3 f3Direct = vec3(
		pf4Lighting[0].x * fDirectE + pf4Lighting[0].y * fDirectW + pf4Lighting[0].z * fDirectN + pf4Lighting[0].w * fDirectS,
		pf4Lighting[1].x * fDirectE + pf4Lighting[1].y * fDirectW + pf4Lighting[1].z * fDirectN + pf4Lighting[1].w * fDirectS,
		pf4Lighting[2].x * fDirectE + pf4Lighting[2].y * fDirectW + pf4Lighting[2].z * fDirectN + pf4Lighting[2].w * fDirectS);

	// Specular — 4 directions, computed once (geometry-only, channel-independent)
	float fSpecE = max(0.0f, Specular(f3ToEyeNormal, vec3( 1.0f, 0.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	float fSpecW = max(0.0f, Specular(f3ToEyeNormal, vec3(-1.0f, 0.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	float fSpecN = max(0.0f, Specular(f3ToEyeNormal, vec3( 0.0f, 1.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	float fSpecS = max(0.0f, Specular(f3ToEyeNormal, vec3( 0.0f,-1.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	vec3 f3Specular = vec3(
		pf4Lighting[0].x * fSpecE + pf4Lighting[0].y * fSpecW + pf4Lighting[0].z * fSpecN + pf4Lighting[0].w * fSpecS,
		pf4Lighting[1].x * fSpecE + pf4Lighting[1].y * fSpecW + pf4Lighting[1].z * fSpecN + pf4Lighting[1].w * fSpecS,
		pf4Lighting[2].x * fSpecE + pf4Lighting[2].y * fSpecW + pf4Lighting[2].z * fSpecN + pf4Lighting[2].w * fSpecS);

	// Combine
	vec3 f3LightingColor = fIntensity * (mainLayout.fLightingWaterSpecularDiffuse * f3Diffuse + mainLayout.fLightingWaterSpecularDirect * f3Direct + mainLayout.fLightingWaterSpecular * f3Specular);

	vec3 f3Final = fAdd * f3LightingColor + (1.0f - fAdd) * f3LightingColor * f3Color;
	float fPower = globalLayout.fLightingCombinePower;
	f3Final = pow(vec3(1.0f) + f3Final, vec3(fPower)) - vec3(1.0f);
	f3Final = min(f3Final, vec3(1.0f));
	return f3Final;
}

vec2 WorldToSmokeTexcoord(vec4 f4SmokeArea, vec2 f2Position)
{
	return vec2(
		(f2Position.x - f4SmokeArea.x) / (f4SmokeArea.z - f4SmokeArea.x),
		(f2Position.y - f4SmokeArea.y) / (f4SmokeArea.w - f4SmokeArea.y));
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
	vec3 f3Final = vec3(fRed, fGreen, fBlue);
	float fPower = globalLayout.fLightingCombinePower;
	f3Final = pow(f3Final + vec3(1.0f, 1.0f, 1.0f), vec3(fPower, fPower, fPower)) - vec3(1.0f, 1.0f, 1.0f);
	f3Final *= 0.5f;

	f3Final += max(vec3(0.1f, 0.1f, 0.1f), globalLayout.f4SunColor.xyz + globalLayout.f4AmbientColor.xyz);

	f3Final = min(vec3(1.0f, 1.0f, 1.0f), f3Final);
	return (1.0f - fSmoke) * f3InColor + fSmoke * f3Final * min(vec3(1.25f, 1.25f, 1.25f), vec3(fDensity, fDensity, fDensity));
}

vec3 BlendSmoke(vec3 f3Color, float fSmokePow, vec4 pf4Lighting[3], GlobalLayout globalLayout)
{
	float fSmokeDensity = globalLayout.fSmokeColorMin + globalLayout.fSmokeColorMultiplier * fSmokePow;
	float fRed = IntensityLighting(pf4Lighting[0]);
	float fGreen = IntensityLighting(pf4Lighting[1]);
	float fBlue = IntensityLighting(pf4Lighting[2]);
	vec3 f3SmokeLighting = vec3(fRed, fGreen, fBlue);
	float fSmokeLightPower = globalLayout.fLightingCombinePower;
	f3SmokeLighting = pow(f3SmokeLighting + vec3(1.0f), vec3(fSmokeLightPower)) - vec3(1.0f);
	f3SmokeLighting *= 0.5f;
	f3SmokeLighting += max(vec3(0.1f), globalLayout.f4SunColor.xyz + globalLayout.f4AmbientColor.xyz);
	f3SmokeLighting = min(vec3(1.0f), f3SmokeLighting);
	return (1.0f - fSmokePow) * f3Color + fSmokePow * f3SmokeLighting * min(vec3(1.25f), vec3(fSmokeDensity));
}

