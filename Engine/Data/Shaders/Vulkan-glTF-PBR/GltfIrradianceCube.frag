#version 460

layout (location = 0) in vec3 inPos;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform samplerCube samplerEnv;

layout(push_constant) uniform PushConstants
{
	layout (offset = 64) float deltaPhi;
	layout (offset = 68) float deltaTheta;
} pushConstants;

const float PI = 3.141592653589793;

void main()
{
	vec3 N = normalize(inPos);
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = cross(N, right);

	vec3 irradiance = vec3(0.0);
	uint sampleCount = 0u;

	for (float phi = 0.0; phi < 2.0 * PI; phi += pushConstants.deltaPhi)
	{
		for (float theta = 0.0; theta < 0.5 * PI; theta += pushConstants.deltaTheta)
		{
			vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
			irradiance += texture(samplerEnv, sampleVec).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}

	outColor = vec4(PI * irradiance / float(sampleCount), 1.0);
}
