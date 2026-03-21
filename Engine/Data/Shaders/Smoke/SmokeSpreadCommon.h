vec2 SmokeWorldPosition(vec4 f4SmokeArea, vec2 f2Texcoord)
{
	return vec2((1.0f - f2Texcoord.x) * f4SmokeArea.x + f2Texcoord.x * f4SmokeArea.z, (1.0f - f2Texcoord.y) * f4SmokeArea.y + f2Texcoord.y * f4SmokeArea.w);
}

// Spread smoke using noise displacement and wind-driven advection
vec4 SmokeSpread(GlobalLayout globalLayout, sampler2D textureSampler, sampler2D noiseTextureSampler, sampler2D windTextureSamplerOne, sampler2D windTextureSamplerTwo, vec2 f2Texcoord, float fNoiseScale)
{
	vec2 f2WorldPosition = SmokeWorldPosition(globalLayout.f4SmokeArea, f2Texcoord);

	// Wind noise: time-offset world position sampling
	vec2 f2WindWorldPosition = f2WorldPosition + vec2(sin(0.5f * globalLayout.fElapsedTime), cos(0.5f * globalLayout.fElapsedTime));
	float fWindNoiseSample = -1.0f + 2.0f * texture(noiseTextureSampler, globalLayout.fSmokeWindNoiseScale * fNoiseScale * f2WindWorldPosition).x;
	float fWindNoise = max(0.0f, globalLayout.fSmokeWindNoiseQuantity * fWindNoiseSample);

	// Swirl noise: time-modulated sampling with XY and YX coordinates
	vec2 f2TimeNoise = 2.0f * vec2(-1.0f + 2.0f * sin(0.01f * globalLayout.fElapsedTime), -1.0f + 2.0f * cos(0.01f * globalLayout.fElapsedTime));
	float fSwirlNoiseSampleX = -1.0f + 2.0f * texture(noiseTextureSampler, f2TimeNoise + fNoiseScale * f2WorldPosition).x;
	float fSwirlNoiseSampleY = -1.0f + 2.0f * texture(noiseTextureSampler, f2TimeNoise + fNoiseScale * f2WorldPosition.yx).x;
	float fNoiseX = globalLayout.fSmokeNoiseQuantity * fSwirlNoiseSampleX;
	float fNoiseY = globalLayout.fSmokeNoiseQuantity * fSwirlNoiseSampleY;

	// Combine wind-driven and swirl noise displacement
	vec2 f2Noise = (fWindNoise * vec2(0.75f, 1.0f) + vec2(fNoiseX, fNoiseY)) * 0.5f;

	// Sample wind field from the current ping-pong buffer
	vec2 f2WindSample = globalLayout.fWindTextureIndex < 0.5f ? texture(windTextureSamplerOne, f2Texcoord).rg : texture(windTextureSamplerTwo, f2Texcoord).rg;
	float fWindMag = length(f2WindSample);
	float fWindMagSafe = max(fWindMag, 1e-3f);
	float fWindMagNew = globalLayout.fWindToSmokeStrength * pow(fWindMag, globalLayout.fWindToSmokePower);
	vec2 f2WindRescaled = vec2(fWindMagNew) * (f2WindSample / vec2(fWindMagSafe));
	f2WindRescaled.x = -f2WindRescaled.x;  // Additive sampling reverses direction; Y cancels with inverted texcoord Y

	// Branchless wind presence flag (moved up for use by advection)
	float fHasWind = step(1e-3f, fWindMag);
	// Direct wind advection: shift base sampling in wind direction (works in uniform fields)
	vec2 f2WindAdvection = fHasWind * globalLayout.fWindSmokeAdvection * f2WindRescaled;
	// Noise-modulated displacement for visual variation (works in gradient fields)
	vec2 f2WindDisplacement = globalLayout.fWindDisplacementNoiseScale * abs(fWindNoiseSample) * f2WindRescaled;
	// Branchless blend between wind-displaced and stationary smoke
	vec2 f2Base = f2Texcoord + f2Noise + f2WindAdvection;
	float fSmokeStayed = texture(textureSampler, f2Base).x;
	float fSmokeMoved = texture(textureSampler, f2Base + f2WindDisplacement).x;
	return globalLayout.fSmokeDecay * vec4(mix(fSmokeStayed, mix(fSmokeMoved, fSmokeStayed, globalLayout.fWindSmokeRetention), fHasWind));
}
