#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

// Shared with Terrain.frag (set=1 binding=5) — same composite elevation G-buffer the frag samples.
// Used here to optionally override the baked mesh Z below a runtime threshold so vertices conform
// to the per-island heightmap (which Shadow.comp and Water.frag also key off of). Vert+frag sharing
// a sampler binding mirrors Water.vert/Water.frag, which both declare elevationTextureSampler at
// set=1 binding=5; PipelineCreator merges vert/frag stage flags by binding number.
layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;

// Per-island AxisAlignedQuadLayout instance buffer; same SSBO that QuadsAxisAlignedVisibleArea.vert
// reads at set=1 binding=1. Here at binding=19 because the pipeline-creator merge in PipelineCreator.cpp
// keys bindings by NUMBER across vert+frag (asserting same descriptor type when both shaders declare it),
// and Terrain.frag occupies set=0/set=1 bindings 0..18. Picking the next free number (19) keeps the
// SSBO disjoint from frag's samplers. gl_InstanceIndex is supplied per-island via firstInstance at
// draw time (Vulkan 1.2 core).
layout (scalar, set = 1, binding = 19) buffer readonly quadsBuffer
{
	AxisAlignedQuadLayout pQuads[];
};

// Input: per-vertex island-local meters XY (origin at island center). DataPacker re-centered
// XY during BakeIslandIntermediates and stripped Z at the ExportIsland write boundary — Z is
// re-derived below from the composite elevation G-buffer (see fVertexZ below).
layout (location = 0) in vec2 f2InPosition;

// Output 0: visible-area UV for Terrain.frag to sample the composite elevation G-buffer plus
//           the shadow / object-shadow / lighting / smoke / ambient samplers that all live in
//           that coordinate space. Color / normal / AO no longer use it — see location 1.
layout (location = 0) out vec2 f2OutTexcoord;

// Output 1: per-island texture UV used by Terrain.frag to directly sample the per-island
//           color / normal / AO bindless arrays. Derived from the mesh's island-local XY
//           (pre-rotation) and the full island footprint (`f4VertexRect.zw`). Rotation-
//           independent by construction: the per-island textures are baked in island-local
//           axes, so rotation only affects where the textured surface lands in world space.
layout (location = 1) out vec2 f2OutIslandTexcoord;

// Output 2: per-instance bindless texture-slot index for color / normal / AO arrays.
//           Mirrors QuadsAxisAlignedVisibleArea.vert's location-7 forwarding pattern.
layout (location = 2) out flat uint uiOutTextureSlot;

// Output 3: per-instance rotation (cos, sin) so Terrain.frag can rotate the BC5 normal
//           tangent in lockstep with how the deleted TerrainNormal.frag prepass used to.
layout (location = 3) out flat vec2 f2OutRotationCosSin;

void main()
{
	// Direct SSBO field reads (no local struct copy): mirrors QuadsAxisAlignedVisibleArea.vert.
	// glslang's struct-copy from a scalar-block-layout SSBO has been observed to drop trailing
	// fields on some drivers (rotation read as 0); reading each field through pQuads[...] avoids it.
	vec4 f4VertexRect = pQuads[gl_InstanceIndex].f4VertexRect;
	float fRotation = pQuads[gl_InstanceIndex].fRotation;
	uiOutTextureSlot = pQuads[gl_InstanceIndex].uiTextureSlot;

	// f4VertexRect.{x,y} is the rect's top-left corner (Islands.cpp:143-146 packs x = worldX - 0.5*w,
	// y = worldY + 0.5*h, w = footprintX, h = -footprintY), so the island center is at corner + half-extent.
	float fCenterX = f4VertexRect.x + 0.5f * f4VertexRect.z;
	float fCenterY = f4VertexRect.y + 0.5f * f4VertexRect.w;

	// Rotate island-local XY into world space; Z is rotation-invariant. Matches the per-instance
	// rotation that QuadsAxisAlignedVisibleArea.vert still applies for the elevation prepass.
	float fCos = cos(fRotation);
	float fSin = sin(fRotation);
	f2OutRotationCosSin = vec2(fCos, fSin);
	float fWorldX = fCenterX + f2InPosition.x * fCos - f2InPosition.y * fSin;
	float fWorldY = fCenterY + f2InPosition.x * fSin + f2InPosition.y * fCos;

	// Per-island texture UV from raw (pre-rotation) island-local mesh XY. Matches the prepass
	// convention exactly: at f2InPosition == (-w/2, -|h|/2 with h<0 → +|h|/2) we hit (u, v) = (0, 0),
	// and at (+w/2, +|h|/2 with h<0 → -|h|/2) we hit (1, 1). Texture content is in island-local
	// axes, so rotation does not enter the UV.
	f2OutIslandTexcoord = vec2(0.5f + f2InPosition.x / f4VertexRect.z,
	                           0.5f + f2InPosition.y / f4VertexRect.w);

	// Visible-area UV used by Terrain.frag to sample composite G-buffer textures
	// (mTerrainElevationTexture etc., rendered earlier this frame by the per-island G-buffer prepass).
	// Convention matches the original Terrain.vert: u in [0,1] maps to [minX, maxX]; v in [0,1] maps
	// to [maxY, minY] (image-space Y down). f4VisibleArea = {minX, maxY, maxX, minY}.
	f2OutTexcoord = vec2(
		(fWorldX - globalLayout.f4VisibleArea.x) / (globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x),
		(fWorldY - globalLayout.f4VisibleArea.y) / (globalLayout.f4VisibleArea.w - globalLayout.f4VisibleArea.y)
	);

	// Vertex Z always comes from the composite elevation G-buffer (same per-island heightmap that
	// Shadow.comp and Water.frag read). The Gaea mesh's role is purely tessellation: XY layout and
	// triangulation density. Sampling Z from the same source as the water blend guarantees
	// pixel-perfect alignment at the shoreline. textureLod required in vertex shaders (no implicit
	// derivatives).
	float fVertexZ = textureLod(elevationTextureSampler, f2OutTexcoord, 0.0f).x;

	gl_Position = Transform(vec4(fWorldX, fWorldY, fVertexZ, 1.0f), mainLayout.f4x4ViewProjection);
}
