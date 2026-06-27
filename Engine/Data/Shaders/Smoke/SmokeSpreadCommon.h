// Spread smoke using noise displacement and wind-driven advection.
// f2InputTexcoord:  UV in the previous-frame smoke texture's coord system (= previous-area UV).
// f2WorldPosition:  world position of this output texel (computed from current area + output UV).
// f2WindTexcoord:   UV for sampling the current-frame wind texture (= current-area UV).
// Decoupling these three lets the smoke world-area shift AND scale per frame between calls.
vec4 SmokeSpread(GlobalLayout globalLayout, sampler2D textureSampler, sampler2D noiseTextureSampler, sampler2D windTextureSamplerOne, sampler2D windTextureSamplerTwo, vec2 f2InputTexcoord, vec2 f2WorldPosition, vec2 f2WindTexcoord, float fNoiseScale)
{
	// Wind noise: time-offset world position sampling
	vec2 f2WindWorldPosition = f2WorldPosition + vec2(sin(0.5f * globalLayout.fElapsedTime), cos(0.5f * globalLayout.fElapsedTime));
	float fWindNoiseSample = -1.0f + 2.0f * textureLod(noiseTextureSampler, globalLayout.fSmokeWindNoiseScale * fNoiseScale * f2WindWorldPosition, 0.0f).x;
	float fWindNoise = max(0.0f, globalLayout.fSmokeWindNoiseQuantity * fWindNoiseSample);

	// Swirl noise: time-modulated sampling with XY and YX coordinates
	vec2 f2TimeNoise = 2.0f * vec2(-1.0f + 2.0f * sin(0.01f * globalLayout.fElapsedTime), -1.0f + 2.0f * cos(0.01f * globalLayout.fElapsedTime));
	float fSwirlNoiseSampleX = -1.0f + 2.0f * textureLod(noiseTextureSampler, f2TimeNoise + fNoiseScale * f2WorldPosition, 0.0f).x;
	float fSwirlNoiseSampleY = -1.0f + 2.0f * textureLod(noiseTextureSampler, f2TimeNoise + fNoiseScale * f2WorldPosition.yx, 0.0f).x;
	float fNoiseX = globalLayout.fSmokeNoiseQuantity * fSwirlNoiseSampleX;
	float fNoiseY = globalLayout.fSmokeNoiseQuantity * fSwirlNoiseSampleY;

	// Combine wind-driven and swirl noise displacement
	vec2 f2Noise = (fWindNoise * vec2(0.75f, 1.0f) + vec2(fNoiseX, fNoiseY)) * 0.5f;

	// Sample wind field from the current ping-pong buffer (wind is in current-area UV)
	vec2 f2WindSample = globalLayout.fWindTextureIndex < 0.5f ? textureLod(windTextureSamplerOne, f2WindTexcoord, 0.0f).rg : textureLod(windTextureSamplerTwo, f2WindTexcoord, 0.0f).rg;
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
	// Branchless blend between wind-displaced and stationary smoke (sample previous frame at input UV + displacement)
	vec2 f2Base = f2InputTexcoord + f2Noise + f2WindAdvection;
	float fSmokeStayed = textureLod(textureSampler, f2Base, 0.0f).x;
	float fSmokeMoved = textureLod(textureSampler, f2Base + f2WindDisplacement, 0.0f).x;
	return globalLayout.fSmokeDecay * vec4(mix(fSmokeStayed, mix(fSmokeMoved, fSmokeStayed, globalLayout.fWindSmokeRetention), fHasWind));
}
