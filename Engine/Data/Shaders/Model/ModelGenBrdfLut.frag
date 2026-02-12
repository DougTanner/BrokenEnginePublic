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
// Returns half-vector H in world space weighted by GGX distribution
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

// Schlick-GGX geometry function for a single direction
// k = (roughness^2) / 2 for IBL
float G_SchlickGGX(float NdotX, float k)
{
	return NdotX / (NdotX * (1.0 - k) + k);
}

// Smith geometry function combining masking and shadowing
float G_Smith(float NdotV, float NdotL, float roughness)
{
	float k = (roughness * roughness) / 2.0;
	return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

void main()
{
	// Map UV to BRDF parameters
	float NdotV = f2InTexcoord.x;
	float roughness = 1.0 - f2InTexcoord.y;

	// Clamp NdotV away from zero to avoid numerical issues
	NdotV = max(NdotV, 0.001);

	// Reconstruct view vector from NdotV
	// N is fixed at (0, 0, 1), so V = (sqrt(1 - NdotV^2), 0, NdotV)
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	vec3 N = vec3(0.0, 0.0, 1.0);

	float scale = 0.0;
	float bias = 0.0;

	for (uint i = 0u; i < NUM_SAMPLES; i++)
	{
		vec2 Xi = hammersley(i, NUM_SAMPLES);
		vec3 H = importanceSample_GGX(Xi, roughness, N);
		vec3 L = 2.0 * dot(V, H) * H - V;

		float NdotL = max(L.z, 0.0);
		if (NdotL > 0.0)
		{
			float NdotH = max(H.z, 0.0);
			float VdotH = max(dot(V, H), 0.0);

			// Geometry term
			float G = G_Smith(NdotV, NdotL, roughness);

			// Visibility term: (G * VdotH) / (NdotH * NdotV)
			float G_Vis = (G * VdotH) / max(NdotH * NdotV, 0.001);

			// Fresnel term (Schlick power only)
			float Fc = pow(1.0 - VdotH, 5.0);

			// Accumulate scale and bias
			scale += (1.0 - Fc) * G_Vis;
			bias += Fc * G_Vis;
		}
	}

	// Normalize by sample count
	scale /= float(NUM_SAMPLES);
	bias /= float(NUM_SAMPLES);

	f4OutColor = vec4(scale, bias, 0.0, 1.0);
}
