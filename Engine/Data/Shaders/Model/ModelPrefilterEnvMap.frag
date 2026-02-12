#version 460

// Pre-filter Environment Map Fragment Shader
// Convolves environment map with GGX distribution at varying roughness levels
// for specular IBL using importance-sampled Monte Carlo integration.

// Input from ModelFilterCube.vert
layout (location = 0) in vec3 inPos;

// Output
layout (location = 0) out vec4 outColor;

// Environment map sampler
layout (binding = 0) uniform samplerCube samplerEnv;

// Push constants (offset 64 accounts for vertex shader's 4x4 matrix)
layout(push_constant) uniform PushConsts {
	layout (offset = 64) float roughness;
	layout (offset = 68) uint numSamples;
} consts;

const float PI = 3.1415926535897932384626433832795;

// Hammersley sequence for low-discrepancy sampling
// Based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
vec2 hammersley(uint i, uint N)
{
	// Van der Corput radical inverse (base 2)
	uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	float rdi = float(bits) * 2.3283064365386963e-10;
	return vec2(float(i) / float(N), rdi);
}

// GGX importance sampling
// Based on http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
vec3 importanceSample_GGX(vec2 Xi, float roughness, vec3 N)
{
	float alpha = roughness * roughness;
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	// Tangent-space half-vector
	vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	// Construct tangent space basis from N
	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangentX = normalize(cross(up, N));
	vec3 tangentY = cross(N, tangentX);

	// Transform from tangent space to world space
	return normalize(tangentX * H.x + tangentY * H.y + N * H.z);
}

// GGX Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
	return alpha2 / (PI * denom * denom);
}

void main()
{
	vec3 N = normalize(inPos);
	vec3 R = N;
	vec3 V = R;

	// Early out for zero roughness - sample directly
	if (consts.roughness == 0.0)
	{
		outColor = vec4(textureLod(samplerEnv, N, 0.0).rgb, 1.0);
		return;
	}

	float totalWeight = 0.0;
	vec3 prefilteredColor = vec3(0.0);

	// Environment map resolution for mip level calculation
	float resolution = float(textureSize(samplerEnv, 0).x);

	for (uint i = 0u; i < consts.numSamples; i++)
	{
		vec2 Xi = hammersley(i, consts.numSamples);
		vec3 H = importanceSample_GGX(Xi, consts.roughness, N);
		vec3 L = 2.0 * dot(V, H) * H - V;

		float NdotL = max(dot(N, L), 0.0);
		if (NdotL > 0.0)
		{
			float NdotH = max(dot(N, H), 0.0);
			float VdotH = max(dot(V, H), 0.0);

			// Compute mip level using filtered importance sampling
			float D = D_GGX(NdotH, consts.roughness);
			float pdf = D * NdotH / (4.0 * VdotH + 0.0001);

			float omegaS = 1.0 / (float(consts.numSamples) * pdf + 0.0001);
			float omegaP = 4.0 * PI / (6.0 * resolution * resolution);
			float mipLevel = max(0.5 * log2(omegaS / omegaP) + 1.0, 0.0);

			prefilteredColor += textureLod(samplerEnv, L, mipLevel).rgb * NdotL;
			totalWeight += NdotL;
		}
	}

	outColor = vec4(prefilteredColor / totalWeight, 1.0);
}
