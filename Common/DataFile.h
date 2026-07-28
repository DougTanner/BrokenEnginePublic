#pragma once

#include "Flags.h"
#include "Math/MathUtils.h"

namespace common
{

inline constexpr int64_t kiAlignmentBytes = 16;

// Per-array alignment of the appended animation chunk section (distinct from kiAlignmentBytes).
// Single-sourced across the ExportScene animation writer and the AnimationData reader walk.
inline constexpr int64_t kiAnimationSectionAlignment = 4;

// Unified Sea-Bottom elevation in meters. Drives both the elevation render-target clear
// (Engine/Source/Graphics/Managers/RenderTargetTextures.cpp main + shadow elevation RTTs)
// and the CPU `GlobalElevation()` open-ocean fallback for cells with no island placement.
// Matches Gaea2 convention: Level=0.1 × elevationMeters=100 m = 10 m beach offset → -10 m
// sea floor. DataPacker (BakeIslandIntermediates.cpp) asserts each island's per-bake sea
// floor matches this constant so divergence is caught at bake time.
inline constexpr float kfSeaBottomMeters = -10.0f;

// Heightmap depth (engine-meters) below which island textures are zeroed out at bake time and
// above which a pixel counts toward the island's valid-area convex polygon. Beach = 0, deeper =
// negative. Shared by DataPacker (texture masking + valid-area hull) and the engine debug render.
inline constexpr float kfUnderwaterMaskThresholdMeters = -2.5f;

inline void AlignOutputStream(std::fstream& rFileStream)
{
	static constexpr char kpcPadding[kiAlignmentBytes] {};
	const std::streamoff iPos = rFileStream.tellp();
	ASSERT(iPos >= 0);
	// Double modulo yields the correct [0, kiAlignmentBytes) padding count and collapses the
	// already-aligned case to 0 without relying on a < kiAlignmentBytes branch guard.
	const int64_t iBytesToAlign = (kiAlignmentBytes - (static_cast<int64_t>(iPos) % kiAlignmentBytes)) % kiAlignmentBytes;
	if (iBytesToAlign > 0)
	{
		rFileStream.write(&kpcPadding[0], iBytesToAlign);
	}
}

struct ChunkLocation
{
	crc_t crc = 0;
	uint64_t uiOffset = 0;
	uint64_t uiSize = 0;
	crc_t contentCrc = 0;
};
static_assert(sizeof(ChunkLocation) == 32, "ChunkLocation layout changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkLocation, crc) == 0, "ChunkLocation::crc offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkLocation, uiOffset) == 8, "ChunkLocation::uiOffset offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkLocation, uiSize) == 16, "ChunkLocation::uiSize offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkLocation, contentCrc) == 24, "ChunkLocation::contentCrc offset changed — bump DataHeader::kiVersion");

enum class ChunkFlags : uint64_t
{
	kScene           = 0x00000002,

	kIsland          = 0x00000004,

	kModel           = 0x00000008,
		kSkinned     = 0x00000020,

	kShader          = 0x00000200,
		kCompute     = 0x00000400,
		kFragment    = 0x00000800,
		kVertex      = 0x00001000,

	kTexture         = 0x00002000,
		kCubemap     = 0x00008000,

	kChunkAudio      = 0x00020000,
	kRaw             = 0x00040000,
	kZlibCompressed  = 0x00080000,
	kLz4Compressed   = 0x00100000,
};
using ChunkFlags_t = Flags<ChunkFlags>;

// A chunk payload is compressed on disk (and must be decompressed into the lazy pool at load) when it
// carries either compression flag. Texture chunks are LZ4-compressed today; kZlibCompressed stays a
// legal on-disk form (the chunk decode path still handles it). Single predicate so every
// "is this chunk compressed" test covers both codecs rather than double-checking each flag at each site.
constexpr bool IsCompressed(ChunkFlags_t flags)
{
	return (flags & ChunkFlags::kLz4Compressed) || (flags & ChunkFlags::kZlibCompressed);
}

struct SceneHeader
{
	static constexpr int64_t kiMaxTextures = 64;
	static constexpr int64_t kiMaxMaterials = 128;

	uint32_t uiTextureCount = 0;
	uint32_t uiMaterialCount = 0;
	bool bHasAnimation = false;
	uint8_t uiPad[3] {};
	uint8_t uiPad2[4] {};  // Explicit: fills the compiler gap that 8-byte-aligns modelCrc to offset 16
	crc_t modelCrc = 0;  // CRC of the .MODEL vertex/index chunk
	// Texture CRCs and index starts now in chunk data payload

	// Chunk payload layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData ALIGN16] [AnimationData].
	// Single source for the writer (ExportScene.cpp) and readers (ModelPipeline.cpp,
	// PipelineDescriptorWriter.cpp, AnimationData.cpp) offset math.
	static constexpr int64_t IndexStartsOffset(int64_t iTextureCount)
	{
		return RoundUp<int64_t, kiAlignmentBytes>(iTextureCount * static_cast<int64_t>(sizeof(crc_t)));
	}
	// Offset where the MaterialShaderData array begins (end of the texture+indexStarts scene-arrays prefix).
	static constexpr int64_t MaterialDataOffset(int64_t iTextureCount, int64_t iMaterialCount)
	{
		return IndexStartsOffset(iTextureCount) + RoundUp<int64_t, kiAlignmentBytes>(iMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
	}
	// Offset where the appended animation section begins. Defined out-of-line below — references the
	// MaterialShaderData size, whose struct is declared later in this header.
	static constexpr int64_t AnimationSectionOffset(int64_t iTextureCount, int64_t iMaterialCount);
};
static_assert(sizeof(SceneHeader) == 24, "SceneHeader layout changed — bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportScene::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");
static_assert(BT_OFFSETOF(SceneHeader, modelCrc) == 16, "SceneHeader padding no longer aligns modelCrc to offset 16");

// Compact animation keyframe for STEP/LINEAR interpolation
struct AnimationKeyframe
{
	float fTime = 0.0f;
	XMFLOAT4 f4Value {};       // Translation (xyz,0), Rotation (quat), or Scale (xyz,1)
};
static_assert(sizeof(AnimationKeyframe) == 20, "AnimationKeyframe layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");
static_assert(BT_OFFSETOF(AnimationKeyframe, f4Value) == 4, "AnimationKeyframe padding changed — keyframe stride no longer matches writer/reader");

// Full animation keyframe with tangents for CUBICSPLINE interpolation
struct AnimationKeyframeCubic
{
	float fTime = 0.0f;
	XMFLOAT4 f4Value {};       // Translation (xyz,0), Rotation (quat), or Scale (xyz,1)
	XMFLOAT4 f4InTangent {};   // Incoming tangent
	XMFLOAT4 f4OutTangent {};  // Outgoing tangent
};
static_assert(sizeof(AnimationKeyframeCubic) == 52, "AnimationKeyframeCubic layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");

// Animation channel (one property of one node)
struct AnimationChannel
{
	// glTF channel enums shared by the runtime reader (AnimationData) and the DataPacker writer
	// (SceneAnimationLoader). static constexpr members do not affect sizeof, so the layout static_assert holds.
	static constexpr uint8_t kTargetPathTranslation = 0;
	static constexpr uint8_t kTargetPathRotation    = 1;
	static constexpr uint8_t kTargetPathScale       = 2;
	static constexpr uint8_t kInterpolationStep        = 0;
	static constexpr uint8_t kInterpolationLinear      = 1;
	static constexpr uint8_t kInterpolationCubicSpline = 2;

	uint16_t uiNodeIndex = 0;     // Node index in skeleton.nodes[]
	uint8_t uiTargetPath = 0;     // kTargetPath* (translation/rotation/scale)
	uint8_t uiInterpolation = 0;  // kInterpolation* (STEP/LINEAR/CUBICSPLINE)
	uint32_t uiKeyframeStart = 0; // Index into keyframe array
	uint32_t uiKeyframeCount = 0;
};
static_assert(sizeof(AnimationChannel) == 12, "AnimationChannel layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");

// Animation clip
struct AnimationClip
{
	static constexpr int64_t kiMaxNameLength = 64;
	char pcName[kiMaxNameLength] {};
	float fDuration = 0.0f;
	uint32_t uiChannelStart = 0;
	uint32_t uiChannelCount = 0;
};
static_assert(sizeof(AnimationClip) == 76, "AnimationClip layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");

// Node in hierarchy (stores all nodes, not just skin joints)
struct ModelNode
{
	int16_t iParentIndex = -1;  // -1 for root (node index, not joint index)
	uint8_t uiPad[2] {};
	XMFLOAT4X4 f4x4BindMatrix {};  // Node matrix property (identity if not present)
	XMFLOAT4 f4BindTranslation {};
	XMFLOAT4 f4BindRotation {};    // Quaternion (x, y, z, w)
	XMFLOAT4 f4BindScale {};
};
static_assert(sizeof(ModelNode) == 116, "ModelNode layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");

// Node hierarchy with skin joint mapping
struct Skeleton
{
	static constexpr int64_t kiMaxNodes = 256;
	static constexpr int64_t kiMaxSkinJoints = 256;

	uint16_t uiNodeCount = 0;
	uint16_t uiSkinJointCount = 0;
	// nodes, skinJointToNode, inverseBindMatrices now in data stream
};
static_assert(sizeof(Skeleton) == 4, "Skeleton layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");

// Per-material skinning info for glTF models
// Enables runtime mesh world matrix computation: meshWorld = relativeTransform * worldMatrices[iParentNodeIndex]
struct MaterialInfo
{
	int16_t iParentNodeIndex = -1;  // Node index for mesh world matrix computation (-1 = identity mesh world)
	int16_t iOriginalMaterialIndex = -1;  // Original glTF material index for split materials (-1 = not split)
	uint8_t uiJointCount = 0;       // 0 for non-skinned meshes, >0 for skinned meshes
	uint8_t uiPad[3] {};
	XMFLOAT4X4 f4x4RelativeTransform {};  // Identity for skinned meshes, meshWorldBind * inverse(ancestorWorldBind) for non-skinned
};
static_assert(sizeof(MaterialInfo) == 72, "MaterialInfo layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene/ExportModel GetVersion raw versions (sizeof fold catches size changes only)");

// Per-mesh shader data (small struct without embedded joints)
// Joint matrices are stored in a separate buffer so MeshData stays small and fixed-size
struct MeshData
{
	static constexpr int64_t kiMaxMeshes = 512;  // Buffer capacity (must be >= 2 * SceneHeader::kiMaxMaterials)
	XMFLOAT4X4 matrix {};                    // Mesh world matrix
	XMFLOAT4 normalMatrix[3] {};             // Normal matrix: transpose(inverse(mat3(matrix))), stored as 3 vec4s
	uint32_t uiJointCount = 0;               // 0 for non-skinned meshes
	uint32_t uiJointMatrixOffset = 0;        // Index into joint matrix buffer
};
static_assert(MeshData::kiMaxMeshes >= 2 * SceneHeader::kiMaxMaterials, "MeshData::kiMaxMeshes must cover 2x SceneHeader::kiMaxMaterials");

// Joint matrix: 3 vec4s (48 bytes) instead of full mat4 (64 bytes)
// rows[i].xyz = rotation row i, rows[i].w = translation component (Tx, Ty, Tz)
struct JointMatrix
{
	XMFLOAT4 rows[3] {};
};

// Joint matrix storage constants
inline constexpr int64_t kiMaxJointsPerMesh = 128;
inline constexpr int64_t kiInitialJointMatrixCapacity = 8192;  // Room for multiple skinned model instances

// Animation header for pack file
struct AnimationHeader
{
	static constexpr int64_t kiMaxAnimations = 64;
	uint32_t uiAnimationCount = 0;
	uint32_t uiChannelCount = 0;
	uint32_t uiKeyframeCount = 0;          // Compact keyframes (STEP/LINEAR)
	uint32_t uiCubicKeyframeCount = 0;
	uint32_t uiMaterialCount = 0;          // Per-material skinning info count
	Skeleton skeleton {};
	// animations, materialInfos, and trailing data now in data stream
};
static_assert(sizeof(AnimationHeader) == 24, "AnimationHeader layout changed — bump DataHeader::kiVersion; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");
static_assert(BT_OFFSETOF(AnimationHeader, skeleton) == 20, "AnimationHeader padding changed — embedded Skeleton no longer at offset 20");

struct MaterialShaderData
{
	uint8_t uiColorTextureIndex = 0;
	uint8_t uiPhysicalDescriptorTextureIndex = 0;
	uint8_t uiNormalTextureIndex = 0;
	uint8_t uiOcclusionTextureIndex = 0;
	uint8_t uiEmissiveTextureIndex = 0;

	// Shader material (must exactly match PbrMaterialLayout in engine)
	// PBR material properties per glTF 2.0 metallic-roughness specification
	XMFLOAT4 f4BaseColorFactor {1.0f, 1.0f, 1.0f, 1.0f};
	XMFLOAT4 f4EmissiveFactor {1.0f, 1.0f, 1.0f, 1.0f};
	int32_t iColorTextureSet = -1;
	int32_t iPhysicalDescriptorTextureSet = -1;
	int32_t iNormalTextureSet = -1;
	int32_t iOcclusionTextureSet = -1;
	int32_t iEmissiveTextureSet = -1;
	float fMetallicFactor = 1.0f;
	float fRoughnessFactor = 1.0f;
	float fAlphaMask = 0.0f;
	float fAlphaMaskCutoff = 1.0f;
};
static_assert(sizeof(MaterialShaderData) == 76, "MaterialShaderData layout changed — bump DataHeader::kiVersion and re-check PbrMaterialLayout; same-size reorder also bumps ExportScene::GetVersion's raw version (sizeof fold catches size changes only)");
static_assert(BT_OFFSETOF(MaterialShaderData, f4BaseColorFactor) == 8, "MaterialShaderData padding changed — PBR factor block no longer at offset 8");

constexpr int64_t SceneHeader::AnimationSectionOffset(int64_t iTextureCount, int64_t iMaterialCount)
{
	return MaterialDataOffset(iTextureCount, iMaterialCount) + RoundUp<int64_t, kiAlignmentBytes>(iMaterialCount * static_cast<int64_t>(sizeof(MaterialShaderData)));
}

struct IslandHeader
{
	common::crc_t ambientOcclusionCrc = 0;
	common::crc_t colorsCrc = 0;
	common::crc_t masksCrc = 0;
	common::crc_t normalsCrc = 0;
	// Anisotropic: each island is auto-cropped at bake time to its land bbox > 1.0 m, expanded to
	// a multiple of 4 * kiElevationDivisor (=16) so BC encoding and 4x elevation downsample stay
	// block-aligned. Width/height meters are derived from the original isotropic widthMeters scaled
	// by cropW/iTexturePixels and cropH/iTexturePixels respectively.
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
	float fWorldFootprintXMeters = 0.0f;
	float fWorldFootprintYMeters = 0.0f;
	float fWorldElevationMeters = 0.0f;
	// Actual peak of the downsampled shipped heightmap (engine-meters above beach). Differs from
	// fWorldElevationMeters, which is the configured elevation *range* from Island.json.
	float fMaxHeightMeters = 0.0f;
	// Chunk data payload follows the heightmap R16 halfs:
	// [float2 positions[iMeshVertexCount]][uint32 indices[iMeshIndexCount]][float2 valid-area hull verts[iValidAreaVertexCount]].
	// Mesh Z is omitted — Terrain.vert re-derives world Z from the composite elevation sampler. The
	// valid-area hull is the CCW convex hull (island-local meters, centered) of pixels at or above
	// kfUnderwaterMaskThresholdMeters; consumed client-only by debug render.
	int32_t iMeshVertexCount = 0;
	int32_t iMeshIndexCount = 0;
	int32_t iValidAreaVertexCount = 0;
};
static_assert(sizeof(IslandHeader) == 72, "IslandHeader layout changed — bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportIsland::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");

struct ModelHeader
{
	int64_t iIndexCount = 0;
	int64_t iVertexCount = 0;
	int64_t iStride = 0;

	// Runtime model chunk layout: [indices ALIGN4] [vertices]. Single source for the writer
	// (ExportModel.cpp) and the generic index+vertex bind reader (Buffer::RecordBindVertexBuffer).
	static constexpr int64_t VerticesOffset(int64_t iIndexCount, int64_t iIndexElementSize)
	{
		return RoundUp<int64_t, 4>(iIndexCount * iIndexElementSize);
	}

	// A model mesh stores 16-bit indices when its vertex count fits a uint16, else 32-bit. Single
	// source for the `.MODEL` writer/reader (ExportScene/ExportModel) and the runtime index-type
	// recovery (BufferManager) — all three must agree on the same threshold.
	static constexpr bool UsesU16Indices(int64_t iVertexCount)
	{
		return iVertexCount < UINT16_MAX;
	}
};
static_assert(sizeof(ModelHeader) == 24, "ModelHeader layout changed — bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportModel::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");

struct ShaderHeader
{
	static constexpr int64_t kiMaxDescriptorSetLayoutBindings = 32;
	static constexpr int64_t kiMaxVertexInputAttributeDescriptions = 12;
	// First word of every SPIR-V module; asserted by the DataPacker writer and the engine reader
	static constexpr uint32_t kuiSpirvMagic = 0x07230203;

	int64_t iDescriptorSetLayoutBindings = 0;
	int64_t iVertexInputAttributeDescriptions = 0;
	int64_t iVertexInputStride = 0;
	// Descriptor bindings and vertex attributes now in chunk data payload

	// Chunk payload layout: [bindings ALIGN16] [setIndices ALIGN16] [attrs ALIGN16] [SPIR-V].
	// Single source for the writer (ExportShader.cpp) and reader (PipelineManager.cpp) offset math.
	static constexpr int64_t BindingsOffset() { return 0; }
	static constexpr int64_t SetIndicesOffset(int64_t iBindingCount)
	{
		return BindingsOffset() + RoundUp<int64_t, kiAlignmentBytes>(iBindingCount * static_cast<int64_t>(sizeof(VkDescriptorSetLayoutBinding)));
	}
	static constexpr int64_t AttributesOffset(int64_t iBindingCount)
	{
		return SetIndicesOffset(iBindingCount) + RoundUp<int64_t, kiAlignmentBytes>(iBindingCount * static_cast<int64_t>(sizeof(uint32_t)));
	}
	static constexpr int64_t SpirvOffset(int64_t iBindingCount, int64_t iAttributeCount)
	{
		return AttributesOffset(iBindingCount) + RoundUp<int64_t, kiAlignmentBytes>(iAttributeCount * static_cast<int64_t>(sizeof(VkVertexInputAttributeDescription)));
	}
};
static_assert(sizeof(ShaderHeader) == 24, "ShaderHeader layout changed — bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportShader::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");

struct TextureHeader
{
	// Per-mip Toksvig slope variance mean((1 - |avgN|) / |avgN|), baked by ExportTexture for BC5
	// normal maps on the regular export path (zeros for every other format/path). Entries past the
	// real mip chain are padded with the last real value at write time, so readers index by any
	// clamped LOD without a count. Sized so TextureHeader matches the largest ChunkHeader union
	// member (IslandHeader, 72 bytes) — real BC5 chains max out at 9 mips (1024^2).
	static constexpr int64_t kiMipVarianceCount = 10;

	int64_t iTextureWidth = 0;
	int64_t iTextureHeight = 0;
	int64_t iMipLevels = 0;
	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
	float pfMipVariance[kiMipVarianceCount] {};
};
static_assert(sizeof(TextureHeader) == 72, "TextureHeader layout changed — bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportTexture::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");

// Audio format metadata for PCM WAV files
struct AudioHeader
{
	WAVEFORMATEX waveFormat {};
};
// Lowest-severity lock — non-largest union member wrapping a platform struct, so assert against WAVEFORMATEX rather than a byte literal
static_assert(sizeof(AudioHeader) == sizeof(WAVEFORMATEX), "AudioHeader must wrap WAVEFORMATEX with no padding — on layout change bump DataHeader::kiVersion; unless sizeof(ChunkHeader) also changed, bump ExportAudio::GetVersion's raw version too (cached chunk headers aren't otherwise re-exported)");

struct ChunkHeader
{
	static constexpr int64_t kiMagic = 0xDA7AF22E;
	int64_t iMagic = kiMagic;
	
	ChunkFlags_t flags;
	crc_t crc = 0;
	char pcPath[MAX_PATH] {};
	int64_t iSize = 0;
	int64_t iUncompressedSize = 0;

	union
	{
		SceneHeader sceneHeader;
		IslandHeader islandHeader;
		ModelHeader modelHeader;
		ShaderHeader shaderHeader;
		TextureHeader textureHeader;
		AudioHeader audioHeader;
	};
};
// The engine read path reinterpret_casts mapped pack bytes to ChunkHeader* and copies it by value
// (PackChunks.cpp), so the byte representation must be trivially copyable. The per-union-member
// sizeof asserts above lock the union footprint; a change to the largest member also shifts
// sizeof(ChunkHeader) and so auto-bumps DataHeader::kiVersion.
static_assert(std::is_trivially_copyable_v<ChunkHeader>, "ChunkHeader must be trivially copyable for the reinterpret_cast/memcpy read path");
static_assert(BT_OFFSETOF(ChunkHeader, iMagic) == 0, "ChunkHeader::iMagic offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, flags) == 8, "ChunkHeader::flags offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, crc) == 16, "ChunkHeader::crc offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, pcPath) == 24, "ChunkHeader::pcPath offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, iSize) == 288, "ChunkHeader::iSize offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, iUncompressedSize) == 296, "ChunkHeader::iUncompressedSize offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(ChunkHeader, sceneHeader) == 304, "ChunkHeader union offset changed — bump DataHeader::kiVersion");

inline constexpr int64_t kiChunkDataOffset = RoundUp<int64_t, kiAlignmentBytes>(static_cast<int64_t>(sizeof(ChunkHeader)));

struct DataHeader
{
	static constexpr int64_t kiMagic = 0xDA7AF11E;
	int64_t iMagic = kiMagic;

	// Auto-bumps when sizeof(ChunkHeader) changes (largest-union-member or outer-field edits). Layout
	// edits that DON'T change sizeof — reordering/shrinking a non-largest union member, or changing a
	// non-union payload struct — are instead caught by the per-struct sizeof/offsetof static_asserts
	// beside each header; bump the manual 50 below when one of those fires. A kiVersion bump forces a
	// full re-export: the DataPacker manifest check (Main.cpp RunExportJobs) re-runs everything on a
	// version mismatch, and the engine ASSERTs on a stale-version manifest. Per-job chunk caches are
	// separate — payload-struct SIZE changes auto-dirty them via the sizeof folds in each job's
	// GetVersion (ExportScene/ExportModel); same-size reorders still need that raw version
	// bumped by hand (the static_assert message beside each struct names the owning job).
	static constexpr int64_t kiVersion = 50 + sizeof(ChunkHeader);
	int64_t iVersion = kiVersion;

	int64_t iChunkCount = 0;
};
static_assert(sizeof(DataHeader) == 24, "DataHeader layout changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(DataHeader, iMagic) == 0, "DataHeader::iMagic offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(DataHeader, iVersion) == 8, "DataHeader::iVersion offset changed — bump DataHeader::kiVersion");
static_assert(BT_OFFSETOF(DataHeader, iChunkCount) == 16, "DataHeader::iChunkCount offset changed — bump DataHeader::kiVersion");

struct ModelVertex
{
	XMFLOAT3 f3Pos {};
	XMFLOAT3 f3Normal {};
	XMFLOAT2 f2Uv {};
	XMFLOAT2 f2Uv1 {};
	XMFLOAT2 f2Uv2 {};
	XMFLOAT2 f2Uv3 {};
	XMFLOAT2 f2Uv4 {};
	float fJoint = 0.0f;
	XMFLOAT4 f4Joint0 {};
	XMFLOAT4 f4Weight0 {};
};
// The DataPacker scene loader dedups vertices via meshoptimizer's raw-byte compare
// (meshopt_generateVertexRemap), so this lock proves the layout is padding-free — uninitialized
// padding would otherwise make byte-equal vertices compare unequal and defeat the dedup.
static_assert(sizeof(ModelVertex) == 100, "ModelVertex layout changed — keep it padding-free for the meshoptimizer raw-byte vertex dedup; bump DataHeader::kiVersion; same-size reorder also bumps ExportScene/ExportModel GetVersion raw versions (sizeof fold catches size changes only)");

} // namespace common
