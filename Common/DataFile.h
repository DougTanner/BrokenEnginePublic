#pragma once

#include "Flags.h"

namespace common
{

inline constexpr std::string_view kpcDataFilename("Data.bin");
inline constexpr std::string_view kpcTexturesFilename("Textures.bin");

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

enum class ChunkFlags : uint64_t
{
	kFont            = 0x00000001,

	kGltf            = 0x00000002,

	kIsland          = 0x00000004,

	kModel           = 0x00000008,
	kGltfModel       = 0x00000010,
		kSkinned     = 0x00000020,
	kNormals         = 0x00000040,
		kFaceNormals = 0x00000080,
	kTexcoords       = 0x00000100,

	kShaderCompute   = 0x00000200,
	kShaderFragment  = 0x00000400,
	kShaderVertex    = 0x00000800,

	kTexture         = 0x00001000,
		kRawTexture  = 0x00002000,
	kCubemap         = 0x00004000,
	kElevation       = 0x00008000,

	kAudio           = 0x00010000,
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
	static constexpr int64_t kiMaxTextures = 24;
	uint32_t uiTextureCount = 0;
	common::crc_t pTextureCrcs[kiMaxTextures] {};

	static constexpr int64_t kiMaxMaterials = 6;
	uint32_t uiMaterialCount = 0;
	uint32_t puiIndexStarts[kiMaxMaterials] {};
};

struct GltfShaderData
{
	uint8_t uiColorTextureIndex = 0;
	uint8_t uiPhysicalDescriptorTextureIndex = 0;
	uint8_t uiNormalTextureIndex = 0;
	uint8_t uiOcclusionTextureIndex = 0;
	uint8_t uiEmissiveTextureIndex = 0;

	// Shader material (must exactly match GltfMaterialLayout in engine)
	DirectX::XMFLOAT4 baseColorFactor {1.0f, 1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 emissiveFactor {1.0f, 1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 diffuseFactor {1.0f, 1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 specularFactor {1.0f, 1.0f, 1.0f, 1.0f};
	float workflow = 0.0f;
	float fPad1 = 0.0f;
	float fPad2 = 0.0f;
	float fPad3 = 0.0f;
	int colorTextureSet = -1;
	int physicalDescriptorTextureSet = -1;
	int normalTextureSet = -1;
	int occlusionTextureSet = -1;
	int emissiveTextureSet = -1;
	int iPad1 = 0;
	int iPad2 = 0;
	int iPad3 = 0;
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	float alphaMask = 0.0f;
	float alphaMaskCutoff = 1.0f;
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

	static constexpr int64_t kiMaxVertexInputAttributeDescriptions = 8;
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
	};
};

struct DataHeader
{
	static constexpr int64_t kiMagic = 0xDA7AF11E;
	int64_t iMagic = kiMagic;

	static constexpr int64_t kiVersion = 44 + sizeof(ChunkHeader);
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

	DirectX::XMFLOAT3 mf3Position {};
};

class VertexPosNorm
{
public:

	VertexPosNorm(float fPositionX, float fPositionY, float fPositionZ, float fNormalX, float fNormalY, float fNormalZ)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	, mf3Normal(fNormalX, fNormalY, fNormalZ)
	{
	}

	DirectX::XMFLOAT3 mf3Position {};
	DirectX::XMFLOAT3 mf3Normal {};
};

class VertexPosTex
{
public:

	VertexPosTex(float fPositionX, float fPositionY, float fPositionZ, float fTexcoordX, float fTexcoordY)
	: mf3Position(fPositionX, fPositionY, fPositionZ)
	, mf2Texcoord(fTexcoordX, fTexcoordY)
	{
	}

	DirectX::XMFLOAT3 mf3Position {};
	DirectX::XMFLOAT2 mf2Texcoord {};
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

	DirectX::XMFLOAT3 mf3Position {};
	DirectX::XMFLOAT3 mf3Normal {};
	DirectX::XMFLOAT2 mf2Texcoord {};
};

struct GltfVertex
{
	bool operator==(const GltfVertex& other) const = default;

	DirectX::XMFLOAT3 f3Pos {};
	DirectX::XMFLOAT3 f3Normal {};
	DirectX::XMFLOAT2 f2Uv {};
	float fJoint = 0.0f;
#if defined(GLTF_ANIMATION)
	DirectX::XMFLOAT4 f4Joint0 {};
	DirectX::XMFLOAT4 f4Weight0 {};
#endif
};

} // namespace common
