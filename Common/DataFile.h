#pragma once

#include "Flags.h"

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

	kGltf            = 0x00000002,

	kIsland          = 0x00000004,

	kModel           = 0x00000008,
	kGltfModel       = 0x00000010,
		kSkinned     = 0x00000020,
		kNormals     = 0x00000040,
		kFaceNormals = 0x00000080,
		kTexcoords   = 0x00000100,

	kShader          = 0x00000200,
		kCompute     = 0x00000400,
		kFragment    = 0x00000800,
		kVertex      = 0x00001000,

	kTexture         = 0x00002000,
		kCubemap     = 0x00008000,
		kElevation   = 0x00010000,

	kAudio           = 0x00020000,
	kRaw             = 0x00040000,
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

struct GltfHeader
{
	static constexpr int64_t kiMaxTextures = 32;
	uint32_t uiTextureCount = 0;
	common::crc_t pTextureCrcs[kiMaxTextures] {};

	static constexpr int64_t kiMaxMaterials = 16;
	uint32_t uiMaterialCount = 0;
	uint32_t puiIndexStarts[kiMaxMaterials] {};

	bool bHasAnimation = false;
	uint8_t uiPad[3] {};

	crc_t modelCrc = 0;  // CRC of the .GLTF_MODEL vertex/index chunk
};

// Animation keyframe (single joint at specific time)
struct GltfAnimationKeyframe
{
	float fTime = 0.0f;
	XMFLOAT4 f4Value {};       // Translation (xyz,0), Rotation (quat), or Scale (xyz,1)
	XMFLOAT4 f4InTangent {};   // Incoming tangent (for CUBICSPLINE)
	XMFLOAT4 f4OutTangent {};  // Outgoing tangent (for CUBICSPLINE)
};

// Animation channel (one property of one node)
struct GltfAnimationChannel
{
	uint16_t uiNodeIndex = 0;     // Node index in skeleton.nodes[]
	uint8_t uiTargetPath = 0;     // 0=translation, 1=rotation, 2=scale
	uint8_t uiInterpolation = 0;  // 0=STEP, 1=LINEAR, 2=CUBICSPLINE
	uint32_t uiKeyframeStart = 0; // Index into keyframe array
	uint32_t uiKeyframeCount = 0;
};

// Animation clip
struct GltfAnimation
{
	static constexpr int64_t kiMaxNameLength = 64;
	char pcName[kiMaxNameLength] {};
	float fDuration = 0.0f;
	uint32_t uiChannelStart = 0;
	uint32_t uiChannelCount = 0;
};

// Node in hierarchy (stores all nodes, not just skin joints)
struct GltfNode
{
	int16_t iParentIndex = -1;  // -1 for root (node index, not joint index)
	uint8_t uiPad[2] {};
	XMFLOAT4X4 f4x4BindMatrix {};  // Node matrix property (identity if not present)
	XMFLOAT4 f4BindTranslation {};
	XMFLOAT4 f4BindRotation {};    // Quaternion (x, y, z, w)
	XMFLOAT4 f4BindScale {};
};

// Node hierarchy with skin joint mapping
struct GltfSkeleton
{
	static constexpr int64_t kiMaxNodes = 256;
	static constexpr int64_t kiMaxSkinJoints = 128;

	uint16_t uiNodeCount = 0;
	uint16_t uiSkinJointCount = 0;
	GltfNode nodes[kiMaxNodes] {};
	uint16_t skinJointToNode[kiMaxSkinJoints] {};  // Maps skin joint i to node index
	XMFLOAT4X4 inverseBindMatrices[kiMaxSkinJoints] {};  // For skinned joints only
};

// Per-material skinning info for glTF models
// Non-skinned meshes (child of node but no JOINTS_0 attribute) use parent node transform
struct GltfMaterialInfo
{
	int16_t iParentNodeIndex = -1;  // -1 = use standard skinning, >= 0 = parent node for non-skinned mesh
	uint8_t uiJointCount = 0;       // 0 for non-skinned meshes, >0 for skinned meshes
	uint8_t uiPad {};
	XMFLOAT4X4 f4x4RelativeTransform {};  // meshWorldBind * inverse(nodeWorldBind), transforms mesh-local to animated model space when combined with nodeWorldAnimated
};

// Per-mesh shader data for glTF skeletal animation
// Matches Vulkan-glTF-PBR buffer layout: matrix + jointMatrix[128] + jointCount
struct alignas(16) MeshShaderData
{
	static constexpr int64_t kiMaxJoints = 128;
	XMFLOAT4X4 matrix {};                    // Mesh world matrix
	XMFLOAT4X4 jointMatrix[kiMaxJoints] {};  // Joint matrices
	uint32_t uiJointCount = 0;               // 0 for non-skinned meshes
	uint32_t uiPad[3] {};                    // Align to 16 bytes
};

// Animation header for pack file
struct GltfAnimationHeader
{
	static constexpr int64_t kiMaxAnimations = 64;
	uint32_t uiAnimationCount = 0;
	uint32_t uiChannelCount = 0;
	uint32_t uiKeyframeCount = 0;
	uint32_t uiPad = 0;
	GltfSkeleton skeleton {};
	GltfAnimation animations[kiMaxAnimations] {};
	GltfMaterialInfo materialInfos[GltfHeader::kiMaxMaterials] {};  // Per-material skinning info
	// Followed by: GltfAnimationChannel[] then GltfAnimationKeyframe[]
};

struct GltfShaderData
{
	uint8_t uiColorTextureIndex = 0;
	uint8_t uiPhysicalDescriptorTextureIndex = 0;
	uint8_t uiNormalTextureIndex = 0;
	uint8_t uiOcclusionTextureIndex = 0;
	uint8_t uiEmissiveTextureIndex = 0;

	// Shader material (must exactly match GltfMaterialLayout in engine)
	// Format from https://github.com/SaschaWillems/Vulkan-glTF-PBR
	XMFLOAT4 f4BaseColorFactor {1.0f, 1.0f, 1.0f, 1.0f};
	XMFLOAT4 f4EmissiveFactor {1.0f, 1.0f, 1.0f, 1.0f};
	XMFLOAT4 f4DiffuseFactor {1.0f, 1.0f, 1.0f, 1.0f};
	XMFLOAT4 f4SpecularFactor {1.0f, 1.0f, 1.0f, 1.0f};
	float fWorkflow = 0.0f;
	float fPad1 = 0.0f;
	float fPad2 = 0.0f;
	float fPad3 = 0.0f;
	int32_t iColorTextureSet = -1;
	int32_t iPhysicalDescriptorTextureSet = -1;
	int32_t iNormalTextureSet = -1;
	int32_t iOcclusionTextureSet = -1;
	int32_t iEmissiveTextureSet = -1;
	int32_t iPad1 = 0;
	int32_t iPad2 = 0;
	int32_t iPad3 = 0;
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
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[kiMaxDescriptorSetLayoutBindings];
	int64_t iDescriptorSetLayoutBindings = 0;

	static constexpr int64_t kiMaxVertexInputAttributeDescriptions = 12;
	VkVertexInputAttributeDescription pVkVertexInputAttributeDescriptions[kiMaxVertexInputAttributeDescriptions];
	int64_t iVertexInputAttributeDescriptions = 0;
	int64_t iVertexInputStride = 0;
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

	union
	{
		FontHeader fontHeader;
		GltfHeader gltfHeader;
		IslandHeader islandHeader;
		ModelHeader modelHeader;
		ShaderHeader shaderHeader;
		TextureHeader textureHeader;
		AudioHeader audioHeader;
	};
};

struct DataHeader
{
	static constexpr int64_t kiMagic = 0xDA7AF11E;
	int64_t iMagic = kiMagic;

	static constexpr int64_t kiVersion = 45 + sizeof(ChunkHeader);
	int64_t iVersion = kiVersion;

	int64_t iChunkCount = 0;
};

class VertexPos
{
public:

	VertexPos(float fPositionX, float fPositionY, float fPositionZ)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	{
	}

	XMFLOAT3 mf3Position {};
};

class VertexPosNorm
{
public:

	VertexPosNorm(float fPositionX, float fPositionY, float fPositionZ, float fNormalX, float fNormalY, float fNormalZ)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	, mf3Normal(fNormalX, fNormalY, fNormalZ)
	{
	}

	XMFLOAT3 mf3Position {};
	XMFLOAT3 mf3Normal {};
};

class VertexPosTex
{
public:

	VertexPosTex(float fPositionX, float fPositionY, float fPositionZ, float fTexcoordX, float fTexcoordY)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	, mf2Texcoord(fTexcoordX, fTexcoordY)
	{
	}

	XMFLOAT3 mf3Position {};
	XMFLOAT2 mf2Texcoord {};
};

class VertexPosNormTex
{
public:

	VertexPosNormTex(float fPositionX, float fPositionY, float fPositionZ, float fNormalX, float fNormalY, float fNormalZ, float fTexcoordX, float fTexcoordY)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	, mf3Normal(fNormalX, fNormalY, fNormalZ)
	, mf2Texcoord(fTexcoordX, fTexcoordY)
	{
	}

	XMFLOAT3 mf3Position {};
	XMFLOAT3 mf3Normal {};
	XMFLOAT2 mf2Texcoord {};
};

struct GltfVertex
{
	bool operator==(const GltfVertex& rOther) const
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
