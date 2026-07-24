#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

// HDR scene color (R16G16B16A16_SFLOAT), sampled fullscreen
layout (set = 1, binding = 2) uniform sampler2D hdrSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

// ACES filmic tone mapping (Narkowicz fit)
vec3 ACESFilm(vec3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

void main()
{
	// Exposure -> ACES tone map the accumulated HDR scene
	vec3 hdr = texture(hdrSampler, f2InTexcoord).rgb;
	hdr = min(hdr, vec3(65000.0f)); // Kill F16 Inf
	hdr *= mainLayout.fPbrExposure;
	vec3 mapped = ACESFilm(hdr);

	// Saturation: mix toward Rec.709 luminance
	float luminance = dot(mapped, vec3(0.2126f, 0.7152f, 0.0722f));
	mapped = mix(vec3(luminance), mapped, mainLayout.fColorGradingSaturation);
	mapped = max(mapped, vec3(0.0f)); // Saturation > 1 can extrapolate negative

	// Contrast around 0.18 mid-gray
	mapped = clamp(pow(mapped / 0.18f, vec3(mainLayout.fColorGradingContrast)) * 0.18f, 0.0f, 1.0f);

	// Temperature: warm (+) shifts red up / blue down
	float temp = mainLayout.fColorGradingTemperature;
	mapped.r *= 1.0f + temp * 0.1f;
	mapped.b *= 1.0f - temp * 0.1f;
	mapped = clamp(mapped, 0.0f, 1.0f);

	// Gamma
	f4OutColor = vec4(pow(mapped, vec3(mainLayout.fPbrGammaInv)), 1.0f);
}
