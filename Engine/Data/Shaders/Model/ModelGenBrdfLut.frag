#version 460

// BRDF LUT Generation Fragment Shader
// Performs Monte Carlo integration of Cook-Torrance BRDF for the split-sum approximation.
// Outputs pre-integrated scale (R) and bias (G) terms for specular IBL.

// Input from ModelGenBrdfLut.vert
layout (location = 0) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

// Specialization constant for sample count
layout (constant_id = 0) const uint NUM_SAMPLES = 1024u;

const float PI = 3.1415926535897932384626433832795;

// Low-discrepancy Hammersley sequence
vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i) / float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// GGX importance sampling with Frisvad tangent frame
vec3 SampleGGX(vec2 Xi, float roughness, vec3 N)
{
	float alpha = roughness * roughness;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	float phi = 2.0 * PI * Xi.x;

	// Frisvad's tangent frame (2012)
	vec3 T, B;
	if (N.z < -0.9999)
	{
		T = vec3(0.0, -1.0, 0.0);
		B = vec3(-1.0, 0.0, 0.0);
	}
	else
	{
		float k = 1.0 / (1.0 + N.z);
		float nxny = -N.x * N.y * k;
		T = vec3(1.0 - N.x * N.x * k, nxny, -N.x);
		B = vec3(nxny, 1.0 - N.y * N.y * k, -N.y);
	}

	return normalize(sinTheta * cos(phi) * T + sinTheta * sin(phi) * B + cosTheta * N);
}

// Smith geometry function for IBL
float SmithG(float NdotV, float NdotL, float roughness)
{
	float r2 = roughness * roughness * 0.5;
	return (NdotV / (NdotV * (1.0 - r2) + r2)) * (NdotL / (NdotL * (1.0 - r2) + r2));
}

void main()
{
	float NdotV = max(f2InTexcoord.x, 0.001);
	float roughness = 1.0 - f2InTexcoord.y;
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

	vec2 result = vec2(0.0);
	for (uint i = 0u; i < NUM_SAMPLES; i++)
	{
		vec3 H = SampleGGX(Hammersley(i, NUM_SAMPLES), roughness, vec3(0.0, 0.0, 1.0));
		vec3 L = 2.0 * dot(V, H) * H - V;
		float NdotL = max(L.z, 0.0);
		if (NdotL > 0.0)
		{
			float NdotH = max(H.z, 0.0);
			float VdotH = max(dot(V, H), 0.0);
			float G = SmithG(NdotV, NdotL, roughness);
			float visibility = (G * VdotH) / max(NdotH * NdotV, 0.001);
			float x = 1.0 - VdotH;
			float x2 = x * x;
			float Fc = x2 * x2 * x;
			result += vec2((1.0 - Fc) * visibility, Fc * visibility);
		}
	}
	f4OutColor = vec4(result / float(NUM_SAMPLES), 0.0, 1.0);
}
