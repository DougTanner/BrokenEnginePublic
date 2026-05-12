#pragma once

enum class FileType
{
	kExr,
	kFloat32,
	kImage,
	kUint16Raw,
};

// Sentinel placed at byte 0 of every Texture::Save'd intermediate file. Reads as "BC7E DA7A"
// in a hex dump. A file lacking this magic is legacy-format and must be migrated through
// MigrateLegacyIntermediates() (in Main.cpp) before any reader touches it.
inline constexpr int64_t kiTextureIntermediateMagic = 0x00000000BC7EDA7A;

class Texture
{
public:

	// Callers must hold this mutex around any Texture construction + MakeMipmaps + Save/Export
	// chain that goes through RDO encoding. The encoder uses all hardware threads internally;
	// the mutex bounds memory by ensuring only one texture's source pixels exist at a time.
	static std::mutex sEncodeMutex;

	static void StaticInit();

	Texture() = delete;
	Texture(const std::filesystem::path& rPath, FileType eFileType, bool bFromGamma, int64_t iWidth = 0, int64_t iHeight = 0);
	Texture(const std::byte* puiPixels, int64_t iWidth, int64_t iHeight, int64_t iStride);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, bool bUseBoxFilter, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel = 32, bool bUseBoxFilter = false)
	{
		ASSERT(mData.size() == 1);
		MakeMipmaps(vkFormat, iMaxLevel, bUseBoxFilter, 0, miWidth, miHeight);
	}

	void Downsize(int64_t iLevels);

	static uint32_t PixelToUint32(const std::vector<float>& rIn, int64_t iWidth, int64_t iX, int64_t iY);
	static void ToBc4(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToBc5(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToBc7(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, bool bVerifyNoAlpha);
	static void ToR8G8B8A8(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToR16(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToR32Sfloat(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);

	// Sweep-mode entry point: callers pass the RDO knobs explicitly. Production callers go through ToBc{4,5,7}.
	static void EncodeWithRdo(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, VkFormat vkFormat, float fLambda, uint32_t uiLookbackWindowSize, int iBc7UberLevel, bool bVerifyNoAlpha);

	void Export(std::vector<std::byte>& rData, VkFormat vkFormat, bool bVerifyNoAlpha);

	std::vector<std::byte> Export(VkFormat vkFormat, bool bVerifyNoAlpha)
	{
		std::vector<std::byte> data;
		Export(data, vkFormat, bVerifyNoAlpha);
		return data;
	}

	void Save(const std::filesystem::path& rPath, VkFormat vkFormat, bool bVerifyNoAlpha);

	// Debug visualizer: writes mData[0] to `rPath` as a JPEG so the bake input can be eyeballed
	// alongside the BC-compressed output. `bGrayscale` replicates R to G/B (set for single-channel
	// data like AO/elevation where G/B are zero); otherwise the RGB channels are used as-is.
	// `bAutoNormalize` rescales the R channel from its actual [min, max] range to byte [0, 255]
	// — required for elevation (raw internal = meters × 255 would saturate at 1 m). Color/normals/AO
	// already live in [0, 255], so leave it off for those.
	void SaveJpegSidecar(const std::filesystem::path& rPath, int iQuality, bool bGrayscale, bool bAutoNormalize = false);

	int64_t miWidth = 0;
	int64_t miHeight = 0;

	std::vector<std::vector<float>> mData;
};
