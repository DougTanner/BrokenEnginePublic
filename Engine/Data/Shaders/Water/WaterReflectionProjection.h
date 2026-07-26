float Fresnel(vec3 f3CameraPosition, vec3 f3Position, vec3 f3InNormal, float fReduction)
{
	vec3 f3Normal = normalize(f3InNormal);

	// Schlick's approximation fresnel
	float fCosTheta = dot(f3Normal, normalize(f3CameraPosition - f3Position));
	float fF0 = globalLayout.fWaterFresnel;
	float fPow = 1.0f - fCosTheta;
	fPow = fPow * fPow * fPow * fPow; // Note: ^4 instead of ^5
	return clamp(fF0 + (fReduction - fF0) * fPow, 0.0f, 1.0f);
}

vec2 ProjectWaterReflection(WaterNormalSamplingResult normalResult, vec2 f2WorldInitialPosition, vec2 f2PositionAtBaseHeight)
{
	vec2 f2PositionAtBaseHeightFinal = f2PositionAtBaseHeight;
	float fReflectedScale = mainLayout.fLightingWaterReflectedAmount * mainLayout.fLightingWaterReflectedIntensity;

	if (fReflectedScale > 0.0f)
	{
		// Reflected base-height lighting-texture sample: reflect the eye ray about the water normal and
		// project the reflected ray to fBaseHeight. Distortion scales the normal's XY
		// before renormalization so wave tilt (not the slow eye-to-point gradient) is
		// the dominant contributor to the reflected sample position.
		vec3 f3ReflectedNormal = mix(normalResult.f3SampledNormal, f3InNormal, mainLayout.fLightingWaterReflectedNormalBlendWave);
		f3ReflectedNormal = normalize(vec3(f3ReflectedNormal.xy * mainLayout.fLightingWaterReflectedDistortion, f3ReflectedNormal.z));
		vec3 f3WaterWorld = vec3(f2WorldInitialPosition, 0.0f);
		vec3 f3EyeToPoint = normalize(f3WaterWorld - mainLayout.f4EyePosition.xyz);
		vec3 f3ReflectedRay = reflect(f3EyeToPoint, f3ReflectedNormal);
		// Guard grazing-normal divide: clamp z away from zero so fReflectedMult can't overflow to +Inf
		float fReflectedMult = (globalLayout.fBaseHeight - f3WaterWorld.z) / max(f3ReflectedRay.z, 1e-4f);
		vec2 f2PositionAtBaseHeightReflected = (f3WaterWorld + max(fReflectedMult, 0.0f) * f3ReflectedRay).xy;

		// Power-curve compression on the XY offset above FalloffStart so heavily-bent
		// normals don't sample hundreds of world units away. Power=1 is passthrough.
		vec2 f2ReflectedOffset = f2PositionAtBaseHeightReflected - f3WaterWorld.xy;
		float fOffsetDistance = length(f2ReflectedOffset);
		float fFalloffStart = mainLayout.fLightingWaterReflectedFalloffStart;
		if (fOffsetDistance > fFalloffStart)
		{
			float fNewDistance = fFalloffStart + pow(fOffsetDistance - fFalloffStart, mainLayout.fLightingWaterReflectedFalloffPower);
			f2PositionAtBaseHeightReflected = f3WaterWorld.xy + f2ReflectedOffset * (fNewDistance / fOffsetDistance);
			fOffsetDistance = fNewDistance;
		}

		float fReflectedFresnel = mix(1.0f, Fresnel(mainLayout.f4EyePosition.xyz, f3WaterWorld, f3ReflectedNormal, 1.0f), mainLayout.fLightingWaterReflectedFresnel);
		float fReflectedAmount = clamp(fReflectedScale * fReflectedFresnel, 0.0f, 1.0f);
		f2PositionAtBaseHeightFinal = mix(f2PositionAtBaseHeight, f2PositionAtBaseHeightReflected, fReflectedAmount);
	}

	return f2PositionAtBaseHeightFinal;
}
