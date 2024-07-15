vec2 Rotate(vec2 f2, float f)
{
	float s = sin(f);
	float c = cos(f);
	mat2 m = mat2(c, -s, s, c);
	return m * f2;
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

vec2 BaseHeightPosition(GlobalLayout globalLayout, MainLayout mainLayout, vec3 f3InPosition)
{
	const float fBaseHeight = globalLayout.f4Misc.y;

	vec3 f3ToEye = normalize(mainLayout.f4EyePosition.xyz - f3InPosition);
	float fMult = (fBaseHeight - f3InPosition.z) / f3ToEye.z;
	return (f3InPosition + max(fMult, 0.0f) * f3ToEye).xy;
}

vec3 SampleNormal(GlobalLayout globalLayout, sampler2D normalSampler, vec2 f2Position, float fSize, float fSpeed, float fTime, vec2 f2Offset)
{
	vec3 f3SampledNormal = texture(normalSampler, f2Offset + fSize * f2Position + fSpeed * vec2(globalLayout.f4Misc.x, globalLayout.f4Misc.x)).xyz;
	return vec3(1.0f - 2.0f * f3SampledNormal.x, 1.0f - 2.0f * f3SampledNormal.y, f3SampledNormal.z);
}

vec3 SunLighting(vec3 f3MaterialColor, GlobalLayout globalLayout, vec4 f4Position, vec3 f3Normal, float fShadow, float fAmbientOcclusion)
{
    vec3 f3SunLight = fShadow * max(0.0f, dot(normalize(f3Normal), globalLayout.f4SunNormal.xyz)) * globalLayout.f4SunColor.xyz;
	float fShadowAffectAmbient = globalLayout.f4ShadowFour.z;
    return f3MaterialColor * fAmbientOcclusion * (f3SunLight + (1.0f - fShadowAffectAmbient) * globalLayout.f4AmbientColor.xyz + fShadowAffectAmbient * fShadow * globalLayout.f4AmbientColor.xyz);
}

float DirectionalLighting(GlobalLayout globalLayout, vec4 f4Lighting, float fHeight, vec3 f3Normal)
{
	float fDirectionalAdd = globalLayout.f4LightingOne.x * (1.0f - dot(vec3(0.0f, 0.0f, 1.0f), f3Normal));

	const float fBaseHeight = globalLayout.f4Misc.y;
	const float fFalloff = 2.0f * fBaseHeight;
	float fHeightPercent = 0.0f;
	if (fHeight > fBaseHeight)
	{
		fHeightPercent = clamp((fHeight - fBaseHeight) / fFalloff, 0.0f, 1.0f);
	}
	else
	{
		fHeightPercent = clamp((fBaseHeight - fHeight) / fFalloff, 0.0f, 1.0f);
	}

	float fEast = f4Lighting.x * max(0.0f, dot(normalize(vec3(-1.0f, 0.0f, 0.0f)), f3Normal));
	float fWest = f4Lighting.y * max(0.0f, dot(normalize(vec3(1.0f, 0.0f, 0.0f)), f3Normal));
	float fNorth = f4Lighting.z * max(0.0f, dot(normalize(vec3(0.0f, -1.0f, 0.0f)), f3Normal));
	float fSouth = f4Lighting.w * max(0.0f, dot(normalize(vec3(0.0f, 1.0f, 0.0f)), f3Normal));
	float fDirect = (fWest + fEast + fNorth + fSouth);

	float fHeightBend = globalLayout.f4LightingOne.y;
	fEast = f4Lighting.x * max(0.0f, dot(normalize(vec3(-1.0f, 0.0f, 0.0f)), normalize(f3Normal + fHeightBend * vec3(-1.0f, 0.0f, 0.0f))));
	fWest = f4Lighting.y * max(0.0f, dot(normalize(vec3(1.0f, 0.0f, 0.0f)), normalize(f3Normal + fHeightBend * vec3(1.0f, 0.0f, 0.0f))));
	fNorth = f4Lighting.z * max(0.0f, dot(normalize(vec3(0.0f, -1.0f, 0.0f)), normalize(f3Normal + fHeightBend * vec3(0.0f, -1.0f, 0.0f))));
	fSouth = f4Lighting.w * max(0.0f, dot(normalize(vec3(0.0f, 1.0f, 0.0f)), normalize(f3Normal + fHeightBend * vec3(0.0f, 1.0f, 0.0f))));
	float fIndirect = fEast + fWest + fNorth + fSouth;

	return (1.0f - fHeightPercent) * ((1.0f - fHeightPercent) * fDirect + fHeightPercent * fIndirect + fDirectionalAdd * fDirect);
}

float Specular(vec3 f3ToEyeNormal, vec3 f3LightNormal, vec3 f3Normal, float fSpecularOne, float fSpecularOnePower, float fSpecularTwo, float fSpecularTwoPower, float fSpecularThree, float fSpecularThreePower)
{
	vec3 f3LightReflectionNormal = normalize(reflect(f3LightNormal, f3Normal));
	float fSpecularFactor = dot(f3ToEyeNormal, f3LightReflectionNormal);
	return fSpecularFactor > 0.0f ? fSpecularOne   * pow(fSpecularFactor, fSpecularOnePower) +
	                                fSpecularTwo   * pow(fSpecularFactor, fSpecularTwoPower) +
	                                fSpecularThree * pow(fSpecularFactor, fSpecularThreePower)
		                            : 0.0f;
}

float SpecularDirectionalLighting(GlobalLayout globalLayout, MainLayout mainLayout, vec4 f4Lighting, vec3 f3Position, vec3 fDirect3Normal, vec3 f3SpecularNormal)
{
	float fDiffuse = f4Lighting.x + f4Lighting.y + f4Lighting.z + f4Lighting.w;

	float fEast = f4Lighting.x * max(0.0f, dot(normalize(vec3(-1.0f, 0.0f, 0.0f)), fDirect3Normal));
	float fWest = f4Lighting.y * max(0.0f, dot(normalize(vec3(1.0f, 0.0f, 0.0f)), fDirect3Normal));
	float fNorth = f4Lighting.z * max(0.0f, dot(normalize(vec3(0.0f, -1.0f, 0.0f)), fDirect3Normal));
	float fSouth = f4Lighting.w * max(0.0f, dot(normalize(vec3(0.0f, 1.0f, 0.0f)), fDirect3Normal));
	float fDirect = fWest + fEast + fNorth + fSouth;

	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f3Position);
	fEast = f4Lighting.x * max(0.0f, Specular(f3ToEyeNormal, vec3(1.0f, 0.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	fWest = f4Lighting.y * max(0.0f, Specular(f3ToEyeNormal, vec3(-1.0f, 0.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	fNorth = f4Lighting.z * max(0.0f, Specular(f3ToEyeNormal, vec3(0.0f, 1.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	fSouth = f4Lighting.w * max(0.0f, Specular(f3ToEyeNormal, vec3(0.0f, -1.0f, 0.0f), f3SpecularNormal, mainLayout.fLightingWaterSpecularOne, mainLayout.fLightingWaterSpecularOnePower, mainLayout.fLightingWaterSpecularTwo, mainLayout.fLightingWaterSpecularTwoPower, mainLayout.fLightingWaterSpecularThree, mainLayout.fLightingWaterSpecularThreePower));
	float fSpecular = fEast + fWest + fNorth + fSouth;

	return mainLayout.fLightingWaterSpecularDiffuse * fDiffuse + mainLayout.fLightingWaterSpecularDirect * fDirect + mainLayout.fLightingWaterSpecular * fSpecular;
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
	float fRed = DirectionalLighting(globalLayout, pf4Lighting[0], fHeight, f3Normal);
	float fGreen = DirectionalLighting(globalLayout, pf4Lighting[1], fHeight, f3Normal);
	float fBlue = DirectionalLighting(globalLayout, pf4Lighting[2], fHeight, f3Normal);
	vec3 f3LightingColor = fIntensity * vec3(fRed, fGreen, fBlue);

	vec3 f3Final = fAdd * f3LightingColor + (1.0f - fAdd) * f3LightingColor * f3Color;

	float fPower = globalLayout.f4LightingOne.w;
	f3Final = pow(vec3(1.0f, 1.0f, 1.0f) + f3Final, vec3(fPower, fPower, fPower)) - vec3(1.0f, 1.0f, 1.0f);
	f3Final = min(f3Final, vec3(1.0f, 1.0f, 1.0f));
	return f3Final;
}

vec3 SpecularLighting(GlobalLayout globalLayout, MainLayout mainLayout, vec3 f3Color, vec3 f3Position, vec3 fDirect3Normal, vec3 f3SpecularNormal, vec4 pf4Lighting[3], float fIntensity, float fAdd)
{
	float fRed = SpecularDirectionalLighting(globalLayout, mainLayout, pf4Lighting[0], f3Position, fDirect3Normal, f3SpecularNormal);
	float fGreen = SpecularDirectionalLighting(globalLayout, mainLayout, pf4Lighting[1], f3Position, fDirect3Normal, f3SpecularNormal);
	float fBlue = SpecularDirectionalLighting(globalLayout, mainLayout, pf4Lighting[2], f3Position, fDirect3Normal, f3SpecularNormal);
	vec3 f3LightingColor = fIntensity * vec3(fRed, fGreen, fBlue);

	vec3 f3Final = fAdd * f3LightingColor + (1.0f - fAdd) * f3LightingColor * f3Color;

	float fPower = globalLayout.f4LightingOne.w;
	f3Final = pow(vec3(1.0f, 1.0f, 1.0f) + f3Final, vec3(fPower, fPower, fPower)) - vec3(1.0f, 1.0f, 1.0f);
	f3Final = min(f3Final, vec3(1.0f, 1.0f, 1.0f));
	return f3Final;
}

vec2 SmokeWindNoise(GlobalLayout globalLayout, vec2 f2WorldPosition, sampler2D noiseTextureSampler, float fMulti, float fElevation)
{
	f2WorldPosition.x += sin(0.5f * globalLayout.f4Misc.x);
	f2WorldPosition.y += cos(0.5f * globalLayout.f4Misc.x);
	float fWindNoise = max(0.0f, globalLayout.f4SmokeThree.z * (-0.25f + texture(noiseTextureSampler, fMulti * f2WorldPosition).x));
	return fWindNoise * vec2(0.75f, 1.0f);
}

vec2 SmokeNoise(GlobalLayout globalLayout, vec2 f2WorldPosition, sampler2D noiseTextureSampler, float fMulti, float fTexMulti)
{
	vec2 f2TimeNoise = 2.0f * vec2(-1.0f + 2.0f * sin(0.01f * globalLayout.f4Misc.x), -1.0f + 2.0f * cos(0.01f * globalLayout.f4Misc.x));
	float fNoiseX = fMulti * globalLayout.f4SmokeThree.w * (-1.0f + 2.0f * texture(noiseTextureSampler, f2TimeNoise + fTexMulti * f2WorldPosition).x);
	float fNoiseY = fMulti * globalLayout.f4SmokeThree.w * (-1.0f + 2.0f * texture(noiseTextureSampler, f2TimeNoise + fTexMulti * f2WorldPosition.yx).x);
	return vec2(fNoiseX, fNoiseY);
}

float SmokeShadow(GlobalLayout globalLayout, vec3 f3InPosition, sampler2D smokeSampler, float fMulti)
{
	float f2SmokeAreaTexcoordX = (f3InPosition.x - globalLayout.f4SmokeArea.x) / (globalLayout.f4SmokeArea.z - globalLayout.f4SmokeArea.x);
	float f2SmokeAreaTexcoordY = (f3InPosition.y - globalLayout.f4SmokeArea.y) / (globalLayout.f4SmokeArea.w - globalLayout.f4SmokeArea.y);
	float fSmokeShadow = globalLayout.f4SmokeOne.y * texture(smokeSampler, vec2(f2SmokeAreaTexcoordX, f2SmokeAreaTexcoordY)).x;
	fSmokeShadow = clamp(pow(fSmokeShadow, globalLayout.f4SmokeOne.z), 0.0f, 1.0f);
	return 1.0f - fMulti * pow(fSmokeShadow, 0.5f);
}

vec3 AddSmoke(GlobalLayout globalLayout, vec3 f3InColor, vec2 f2InPosition, sampler2D smokeSampler, float fInMax, vec4 pf4Lighting[3])
{
	float f2SmokeAreaTexcoordX = (f2InPosition.x - globalLayout.f4SmokeArea.x) / (globalLayout.f4SmokeArea.z - globalLayout.f4SmokeArea.x);
	float f2SmokeAreaTexcoordY = (f2InPosition.y - globalLayout.f4SmokeArea.y) / (globalLayout.f4SmokeArea.w - globalLayout.f4SmokeArea.y);
	float fSmoke = globalLayout.f4SmokeOne.y * texture(smokeSampler, vec2(f2SmokeAreaTexcoordX, f2SmokeAreaTexcoordY)).x;
	fSmoke = clamp(pow(fSmoke, globalLayout.f4SmokeOne.z), 0.0f, 1.0f);
	float fDensity = globalLayout.f4SmokeTwo.x + globalLayout.f4SmokeTwo.y * fSmoke;
	fSmoke *= fInMax;

	float fRed = IntensityLighting(pf4Lighting[0]);
	float fGreen = IntensityLighting(pf4Lighting[1]);
	float fBlue = IntensityLighting(pf4Lighting[2]);
	vec3 f3Final = vec3(fRed, fGreen, fBlue);
	float fPower = globalLayout.f4LightingOne.w;
	f3Final = pow(f3Final + vec3(1.0f, 1.0f, 1.0f), vec3(fPower, fPower, fPower)) - vec3(1.0f, 1.0f, 1.0f);
	f3Final *= 0.45f;

	f3Final += max(vec3(0.1f, 0.1f, 0.1f), globalLayout.f4SunColor.xyz + globalLayout.f4AmbientColor.xyz);

	f3Final = min(vec3(1.0f, 1.0f, 1.0f), f3Final);
	return (1.0f - fSmoke) * f3InColor + fSmoke * f3Final * min(vec3(1.25f, 1.25f, 1.25f), vec3(fDensity, fDensity, fDensity));
}

#if 0
vec3 AddSmokeToObject(GlobalLayout globalLayout, MainLayout mainLayout, sampler2D smokeSampler, vec3 f3InColor, vec3 f3InPosition, float fInMax)
{
	const float fSmokeHeight = 1.0f;
	const float fInverseSmokeHeight = 1.0f / fSmokeHeight;
	const float fBaseHeight = globalLayout.f4Misc.y;

	if (f3InPosition.z > fBaseHeight + fSmokeHeight)
	{
		return vec3(0.0f, 0.0f, 0.0f);
	}
	else if (f3InPosition.z > fBaseHeight)
	{
		fInMax *= 1.0f - f3InPosition.z * fInverseSmokeHeight;
	}

	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InPosition);

	// Make into a function
	float f2SmokeAreaTexcoordX = (f2PositionAtBaseHeight.x - globalLayout.f4SmokeArea.x) / (globalLayout.f4SmokeArea.z - globalLayout.f4SmokeArea.x);
	float f2SmokeAreaTexcoordY = (f2PositionAtBaseHeight.y - globalLayout.f4SmokeArea.y) / (globalLayout.f4SmokeArea.w - globalLayout.f4SmokeArea.y);
	float fSmoke = globalLayout.f4SmokeOne.y * texture(smokeSampler, vec2(f2SmokeAreaTexcoordX, f2SmokeAreaTexcoordY)).x;
	fSmoke = clamp(pow(fSmoke, globalLayout.f4SmokeOne.z), 0.0f, 1.0f);
	float fDensity = globalLayout.f4SmokeTwo.x + globalLayout.f4SmokeTwo.y * fSmoke;
	fSmoke *= fInMax;

	return (1.0f - fSmoke) * f3InColor + fSmoke * vec3(fDensity, fDensity, fDensity);
}
#endif
