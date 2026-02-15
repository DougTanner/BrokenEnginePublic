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
	const float fBaseHeight = globalLayout.fBaseHeight;

	vec3 f3ToEye = normalize(mainLayout.f4EyePosition.xyz - f3InPosition);
	float fMult = (fBaseHeight - f3InPosition.z) / f3ToEye.z;
	return (f3InPosition + max(fMult, 0.0f) * f3ToEye).xy;
}

vec3 SampleNormal(GlobalLayout globalLayout, sampler2D normalSampler, vec2 f2Position, float fSize, float fSpeed, float fTime, vec2 f2Offset)
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

float DirectionalLighting(GlobalLayout globalLayout, vec4 f4Lighting, float fHeight, vec3 f3Normal)
{
	float fDirectionalAdd = globalLayout.fLightingDirectional * (1.0f - dot(vec3(0.0f, 0.0f, 1.0f), f3Normal));

	const float fBaseHeight = globalLayout.fBaseHeight;
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

	float fHeightBend = globalLayout.fLightingIndirect;
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

	float fPower = globalLayout.fLightingCombinePower;
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

	float fPower = globalLayout.fLightingCombinePower;
	f3Final = pow(vec3(1.0f, 1.0f, 1.0f) + f3Final, vec3(fPower, fPower, fPower)) - vec3(1.0f, 1.0f, 1.0f);
	f3Final = min(f3Final, vec3(1.0f, 1.0f, 1.0f));
	return f3Final;
}

float SmokeShadow(GlobalLayout globalLayout, vec3 f3InPosition, sampler2D smokeSampler, float fMulti)
{
	float f2SmokeAreaTexcoordX = (f3InPosition.x - globalLayout.f4SmokeArea.x) / (globalLayout.f4SmokeArea.z - globalLayout.f4SmokeArea.x);
	float f2SmokeAreaTexcoordY = (f3InPosition.y - globalLayout.f4SmokeArea.y) / (globalLayout.f4SmokeArea.w - globalLayout.f4SmokeArea.y);
	float fSmokeShadow = globalLayout.fSmokeMax * texture(smokeSampler, vec2(f2SmokeAreaTexcoordX, f2SmokeAreaTexcoordY)).x;
	fSmokeShadow = clamp(pow(fSmokeShadow, globalLayout.fSmokePower), 0.0f, 1.0f);
	return 1.0f - fMulti * pow(fSmokeShadow, 0.5f);
}

vec3 AddSmoke(GlobalLayout globalLayout, vec3 f3InColor, vec2 f2InPosition, sampler2D smokeSampler, float fInMax, vec4 pf4Lighting[3])
{
	float f2SmokeAreaTexcoordX = (f2InPosition.x - globalLayout.f4SmokeArea.x) / (globalLayout.f4SmokeArea.z - globalLayout.f4SmokeArea.x);
	float f2SmokeAreaTexcoordY = (f2InPosition.y - globalLayout.f4SmokeArea.y) / (globalLayout.f4SmokeArea.w - globalLayout.f4SmokeArea.y);
	float fSmoke = globalLayout.fSmokeMax * texture(smokeSampler, vec2(f2SmokeAreaTexcoordX, f2SmokeAreaTexcoordY)).x;
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

