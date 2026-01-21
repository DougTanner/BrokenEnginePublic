#version 460

// BRDF LUT Generation Vertex Shader
// Renders a full-screen quad for generating the 2D BRDF lookup texture.
// UV coordinates parameterize NdotV (U) and roughness (V) for integration.

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out vec2 f2OutTexcoord;

void main()
{
	f2OutTexcoord = f2InQuadVertex;

	// Map [0,1] to [-1,+1] for X, [0,1] to [+1,-1] for Y (Vulkan Y-flip)
	gl_Position = vec4(f2InQuadVertex * 2.0 - 1.0, 0.0, 1.0);
	gl_Position.y *= -1.0;
}
