// f2InputTexcoord: UV in the previous-frame wind texture's coord system (= previous-area UV).
// f2WorldPosition: world position of this output texel (computed from current area + output UV).
// Decoupling these two lets the wind world-area shift AND scale per frame between calls.
vec2 WindSpread(GlobalLayout globalLayout, sampler2D windTextureSampler, sampler2D noiseTextureSampler, vec2 f2InputTexcoord, vec2 f2WorldPosition)
{
	float fTexelSize = globalLayout.fWindTexelSize;
	float fTimeScale = globalLayout.fWindTimeScale;

	// Semi-Lagrangian advection: trace back along wind direction to find source
	vec2 f2Wind = textureLod(windTextureSampler, f2InputTexcoord, 0.0f).rg;

	// Neighbor reads (shared by vorticity and diffusion)
	vec2 f2Right = textureLod(windTextureSampler, f2InputTexcoord + vec2(fTexelSize, 0.0f), 0.0f).rg;
	vec2 f2Left  = textureLod(windTextureSampler, f2InputTexcoord - vec2(fTexelSize, 0.0f), 0.0f).rg;
	vec2 f2Up    = textureLod(windTextureSampler, f2InputTexcoord + vec2(0.0f, fTexelSize), 0.0f).rg;
	vec2 f2Down  = textureLod(windTextureSampler, f2InputTexcoord - vec2(0.0f, fTexelSize), 0.0f).rg;

	// Magnitude-dependent behavior: weak wind is laminar, strong wind is turbulent
	float fMag = length(f2Wind);
	float fMagFactor = clamp((fMag - globalLayout.fWindThresholdLow) / max(globalLayout.fWindThresholdHigh - globalLayout.fWindThresholdLow, 0.001), 0.0, 1.0);
	// Momentum: slider up = more momentum = less spread/swirl/diffusion
	float fSpread = 1.0 - mix(globalLayout.fWindMomentumLow, globalLayout.fWindMomentumHigh, fMagFactor);

	float fAdvectionScale = mix(globalLayout.fWindAdvectionScaleLow, globalLayout.fWindAdvectionScaleHigh, fMagFactor);
	vec2 f2Displacement = vec2(f2Wind.x, -f2Wind.y) * fAdvectionScale * fSpread * fTimeScale;
	float fMaxStep = fTexelSize * 3.0f;
	float fDispLen = length(f2Displacement);
	if (fDispLen > fMaxStep)
	{
		f2Displacement *= fMaxStep / fDispLen;
	}
	vec2 f2SourceUV = f2InputTexcoord - f2Displacement;
	vec2 f2AdvectedWind = textureLod(windTextureSampler, f2SourceUV, 0.0f).rg;

	// Swirl: perpendicular perturbation via noise (sampled by world position so it stays put under camera motion)
	float fSwirlScale = mix(globalLayout.fWindSwirlScaleLow, globalLayout.fWindSwirlScaleHigh, fMagFactor);
	float fSwirlSpeed = mix(globalLayout.fWindSwirlSpeedLow, globalLayout.fWindSwirlSpeedHigh, fMagFactor);
	float fSwirlAmount = mix(globalLayout.fWindSwirlAmountLow, globalLayout.fWindSwirlAmountHigh, fMagFactor);
	float fSwirlNoise = textureLod(noiseTextureSampler, f2WorldPosition * fSwirlScale + vec2(globalLayout.fWindTime * fSwirlSpeed), 0.0f).r;
	vec2 f2Perpendicular = vec2(-f2AdvectedWind.y, f2AdvectedWind.x);
	f2AdvectedWind += fSwirlAmount * fSpread * fTimeScale * (fSwirlNoise - 0.5f) * f2Perpendicular;

	// Vorticity confinement (simple: perpendicular to wind direction)
	float fVorticityConfinement = mix(globalLayout.fWindVorticityConfinementLow, globalLayout.fWindVorticityConfinementHigh, fMagFactor);
	if (fVorticityConfinement > 0.0f)
	{
		float fOmega = f2Right.y - f2Left.y + f2Up.x - f2Down.x;

		float fAdvectedMag = length(f2AdvectedWind);
		if (fAdvectedMag > kfEpsilon)
		{
			vec2 f2Dir = f2AdvectedWind / fAdvectedMag;
			vec2 f2PerpConfinement = vec2(-f2Dir.y, f2Dir.x);
			f2AdvectedWind += fVorticityConfinement * fOmega * fTimeScale * f2PerpConfinement;
		}
	}

	// Diffusion: average with 4 neighbors for lateral spread
	float fDiffusion = mix(globalLayout.fWindDiffusionLow, globalLayout.fWindDiffusionHigh, fMagFactor);
	if (fDiffusion > 0.0f)
	{
		vec2 f2Avg = 0.25f * (f2Right + f2Left + f2Up + f2Down);
		f2AdvectedWind = mix(f2AdvectedWind, f2Avg, min(1.0f, fDiffusion * fSpread * fTimeScale));
	}

	// Decay: slider up = more decay, so invert (1.0 - value) for the pow base
	float fDecayRate = 1.0 - mix(globalLayout.fWindDecayLow, globalLayout.fWindDecayHigh, fMagFactor);
	f2AdvectedWind *= pow(fDecayRate, fTimeScale);

	// Soft clamp
	float fWindMag = length(f2AdvectedWind);
	if (fWindMag > 0.5f)
	{
		f2AdvectedWind *= tanh(fWindMag) / fWindMag;
		fWindMag = tanh(fWindMag);
	}

	// Constant decay: subtract fixed amount so near-zero values converge to zero quickly
	float fConstDecay = 1e-3f * fTimeScale;
	if (fWindMag > fConstDecay)
	{
		f2AdvectedWind *= (fWindMag - fConstDecay) / fWindMag;
		return f2AdvectedWind;
	}

	return vec2(0.0f);
}
