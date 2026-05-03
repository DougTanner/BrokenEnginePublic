#pragma once

#include "Flags.h"
#include "MathUtils.h"

namespace common
{

inline constexpr int64_t kiAlignmentBytes = 16;

inline void AlignOutputStream(std::fstream& rFileStream)
{
	static constexpr char kpcPadding[kiAlignmentBytes] {};
	int64_t iBytesToAlign = kiAlignmentBytes - (rFileStream.tellp() % kiAlignmentBytes);
	if (iBytesToAlign > 0 && iBytesToAlign < kiAlignmentBytes)
	{
		rFileStream.write(&kpcPadding[0], iBytesToAlign);
	}
}

struct ChunkLocation
{
	crc_t crc;
	uint64_t uiOffset = 0;
	uint64_t uiSize = 0;
};

enum class ChunkFlags : uint64_t
{
	kFont            = 0x00000001,

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
		kElevation   = 0x00010000,

	kChunkAudio      = 0x00020000,
	kRaw             = 0x00040000,
	kZlibCompressed  = 0x00080000,
};
using ChunkFlags_t = Flags<ChunkFlags>;

struct FontHeader
{
	int64_t iCharacters = 0;
	int64_t iKerningPairs = 0;

	int64_t iLineHeight = 0;
	int64_t iBase = 0;
	int64_t iScaleW = 0;
	int64_t iScaleH = 0;
};

struct SceneHeader
{
	static constexpr int64_t kiMaxTextures = 64;
	static constexpr int64_t kiMaxMaterials = 128;

	uint32_t uiTextureCount = 0;
	uint32_t uiMaterialCount = 0;
	bool bHasAnimation = false;
	uint8_t uiPad[3] {};
	crc_t modelCrc = 0;  // CRC of the .MODEL vertex/index chunk
	// Texture CRCs and index starts now in chunk data payload
};

// Compact animation keyframe for STEP/LINEAR interpolation
struct AnimationKeyframe
{
	float fTime = 0.0f;
	XMFLOAT4 f4Value {};       // Translation (xyz,0), Rotation (quat), or Scale (xyz,1)
};

// Full animation keyframe with tangents for CUBICSPLINE interpolation
struct AnimationKeyframeCubic
{
	float fTime = 0.0f;
	XMFLOAT4 f4Value {};       // Translation (xyz,0), Rotation (quat), or Scale (xyz,1)
	XMFLOAT4 f4InTangent {};   // Incoming tangent
	XMFLOAT4 f4OutTangent {};  // Outgoing tangent
};

// Animation channel (one property of one node)
struct AnimationChannel
{
	uint16_t uiNodeIndex = 0;     // Node index in skeleton.nodes[]
	uint8_t uiTargetPath = 0;     // 0=translation, 1=rotation, 2=scale
	uint8_t uiInterpolation = 0;  // 0=STEP, 1=LINEAR, 2=CUBICSPLINE
	uint32_t uiKeyframeStart = 0; // Index into keyframe array
	uint32_t uiKeyframeCount = 0;
};

// Animation clip
struct AnimationClip
{
	static constexpr int64_t kiMaxNameLength = 64;
	char pcName[kiMaxNameLength] {};
	float fDuration = 0.0f;
	uint32_t uiChannelStart = 0;
	uint32_t uiChannelCount = 0;
};

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

// Node hierarchy with skin joint mapping
struct Skeleton
{
	static constexpr int64_t kiMaxNodes = 256;
	static constexpr int64_t kiMaxSkinJoints = 256;

	uint16_t uiNodeCount = 0;
	uint16_t uiSkinJointCount = 0;
	// nodes, skinJointToNode, inverseBindMatrices now in data stream
};

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

// Per-mesh shader data (small struct without embedded joints)
// Joint matrices are stored in a separate buffer for NVIDIA driver compatibility
struct MeshData
{
	static constexpr int64_t kiMaxMeshes = 512;  // Buffer capacity (must be >= 2 * SceneHeader::kiMaxMaterials)
	XMFLOAT4X4 matrix {};                    // Mesh world matrix
	XMFLOAT4 normalMatrix[3] {};             // Normal matrix: transpose(inverse(mat3(matrix))), stored as 3 vec4s
	uint32_t uiJointCount = 0;               // 0 for non-skinned meshes
	uint32_t uiJointMatrixOffset = 0;        // Index into joint matrix buffer
};

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

struct Character
{
	uint16_t uiX = 0;
	uint16_t uiY = 0;
	uint16_t uiWidth = 0;
	uint16_t uiHeight = 0;
	int16_t iXOffset = 0;
	int16_t iYOffset = 0;
	int16_t iXAdvance = 0;
};

struct IslandHeader
{
	common::crc_t ambientOcclusionCrc = 0;
	common::crc_t colorsCrc = 0;
	common::crc_t elevationCrc = 0;
	common::crc_t normalsCrc = 0;
	uint16_t uiBeachElevation = 0;
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
};

struct ModelHeader
{
	int64_t iIndexCount = 0;
	int64_t iVertexCount = 0;
	int64_t iStride = 0;
};

struct ShaderHeader
{
	static constexpr int64_t kiMaxDescriptorSetLayoutBindings = 32;
	static constexpr int64_t kiMaxVertexInputAttributeDescriptions = 12;

	int64_t iDescriptorSetLayoutBindings = 0;
	int64_t iVertexInputAttributeDescriptions = 0;
	int64_t iVertexInputStride = 0;
	// Descriptor bindings and vertex attributes now in chunk data payload
};

struct TextureHeader
{
	int64_t iTextureWidth = 0;
	int64_t iTextureHeight = 0;
	int64_t iMipLevels = 0;
	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
};

// Audio format metadata for PCM WAV files
struct AudioHeader
{
	WAVEFORMATEX waveFormat {};
};

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
		FontHeader fontHeader;
		SceneHeader sceneHeader;
		IslandHeader islandHeader;
		ModelHeader modelHeader;
		ShaderHeader shaderHeader;
		TextureHeader textureHeader;
		AudioHeader audioHeader;
	};
};

inline constexpr int64_t kiChunkDataOffset = RoundUp<int64_t, kiAlignmentBytes>(static_cast<int64_t>(sizeof(ChunkHeader)));

struct DataHeader
{
	static constexpr int64_t kiMagic = 0xDA7AF11E;
	int64_t iMagic = kiMagic;

	static constexpr int64_t kiVersion = 46 + sizeof(ChunkHeader);
	int64_t iVersion = kiVersion;

	int64_t iChunkCount = 0;
};

struct ModelVertex
{
	bool operator==(const ModelVertex& rOther) const
	{
		bool bEqual = f3Pos == rOther.f3Pos && f3Normal == rOther.f3Normal;
		bEqual = bEqual && ::operator==(f2Uv, rOther.f2Uv) && ::operator==(f2Uv1, rOther.f2Uv1);
		bEqual = bEqual && ::operator==(f2Uv2, rOther.f2Uv2) && ::operator==(f2Uv3, rOther.f2Uv3) && ::operator==(f2Uv4, rOther.f2Uv4);
		bEqual = bEqual && fJoint == rOther.fJoint;
		bEqual = bEqual && ::operator==(f4Joint0, rOther.f4Joint0) && ::operator==(f4Weight0, rOther.f4Weight0);
		return bEqual;
	}

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

} // namespace common

template<>
struct std::hash<common::ModelVertex>
{
	size_t operator()(const common::ModelVertex& rVertex) const
	{
		return common::Crc(rVertex);
	}
};
