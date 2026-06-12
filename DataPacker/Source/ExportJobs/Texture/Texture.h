#pragma once

enum class FileType
{
	kExr,
	kFloat32,
	kImage,
	kUint16Raw,
};

// Per-call options for Texture loading, encoding, and debug output. Bundled into one enum so
// every Texture method takes the same `common::Flags<TextureOptions>` parameter type — callers
// pass `{}` for no flags or e.g. `TextureOptions::kVerifyNoAlpha` to opt in.
enum class TextureOptions : uint8_t
{
	kVerifyNoAlpha = 0x02, // Encode/Export/Save BC7: assert the encoder didn't produce an alpha mode
	kUseBoxFilter  = 0x04, // MakeMipmaps: STBIR_FILTER_BOX instead of stbir_resize_float_linear
	kGrayscale     = 0x08, // SaveJpegSidecar: replicate R into G/B (single-channel data)
	kAutoNormalize = 0x10, // SaveJpegSidecar: rescale R from observed [min,max] to byte [0,255]
};
using TextureOptions_t = common::Flags<TextureOptions>;

// Sentinel placed at byte 0 of every Texture::Save'd intermediate file. Reads as "BC7E DA7A"
// in a hex dump. A file lacking this magic is legacy-format and must be migrated through
// MigrateLegacyIntermediates() before any reader touches it.
inline constexpr int64_t kiTextureIntermediateMagic = 0x00000000BC7EDA7A;

class Texture
{
public:

	// Callers must hold this mutex around any Texture construction + MakeMipmaps + Save/Export
	// chain that goes through RDO encoding. The encoder spawns up to HardwareCoreCount() - 2 threads
	// internally; the mutex bounds memory by ensuring only one texture's source pixels exist at a time.
	static std::mutex sEncodeMutex;

	static void StaticInit();

	Texture() = delete;
	Texture(const std::filesystem::path& rPath, FileType eFileType, int64_t iWidth = 0, int64_t iHeight = 0);
	Texture(const std::byte* puiPixels, int64_t iWidth, int64_t iHeight, int64_t iStride);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, TextureOptions_t options, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel = 32, TextureOptions_t options = {})
	{
		ASSERT(mData.size() == 1);
		MakeMipmaps(vkFormat, iMaxLevel, options, 0, miWidth, miHeight);
	}

	void Downsize(int64_t iLevels);

	// Reduce mData[0] to the sub-rect [iX, iX+iWidth) × [iY, iY+iHeight) and update miWidth/miHeight.
	// Single-mip only (must be called before MakeMipmaps). Used by ExportIsland to crop full-res
	// Color.png / Normals.exr in-memory to the bake-time auto-crop bbox before BC encoding, since
	// no PNG / EXR writer is wired up in Texture.cpp (load-only paths).
	void Crop(int64_t iX, int64_t iY, int64_t iWidth, int64_t iHeight);

	// Replace every mip-0 pixel whose corresponding heightmap sample is < fThresholdMeters with
	// rFlatValue. Heightmap is at miWidth/iHeightmapDivisor × miHeight/iHeightmapDivisor (the
	// 4×-downsampled engine-meter elevation), so each heightmap sample governs an iHeightmapDivisor²
	// block of texture pixels. Single-mip only — call before MakeMipmaps so mips inherit the
	// flattened regions naturally. Used by ExportIsland to zero out invisible underwater pixels
	// before BC encoding, giving RDO + zlib large constant runs to compress.
	void MaskByHeightmap(const std::vector<float>& rHeightmap, int64_t iHeightmapWidth, int64_t iHeightmapHeight, int64_t iHeightmapDivisor, float fThresholdMeters, const float pfFlatValue[4]);

	static uint32_t PixelToUint32(const std::vector<float>& rIn, int64_t iWidth, int64_t iX, int64_t iY);
	static void ToBc4(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToBc5(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToBc7(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, TextureOptions_t options);
	static void ToR8G8B8A8(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToR16(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	static void ToR32Sfloat(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);

	// Sweep-mode entry point: callers pass the RDO knobs explicitly. Production callers go through ToBc{4,5,7}.
	static void EncodeWithRdo(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, VkFormat vkFormat, float fLambda, uint32_t uiLookbackWindowSize, int iBc7UberLevel, TextureOptions_t options);

	void Export(std::vector<std::byte>& rData, VkFormat vkFormat, TextureOptions_t options);

	std::vector<std::byte> Export(VkFormat vkFormat, TextureOptions_t options)
	{
		std::vector<std::byte> data;
		Export(data, vkFormat, options);
		return data;
	}

	void Save(const std::filesystem::path& rPath, VkFormat vkFormat, TextureOptions_t options);

	// Debug visualizer: writes mData[0] to `rPath` as a JPEG so the bake input can be eyeballed
	// alongside the BC-compressed output. `kGrayscale` replicates R to G/B (set for single-channel
	// data like AO/elevation where G/B are zero); otherwise the RGB channels are used as-is.
	// `kAutoNormalize` rescales the R channel from its actual [min, max] range to byte [0, 255]
	// — required for elevation (raw internal = meters × 255 would saturate at 1 m). Color/normals/AO
	// already live in [0, 255], so leave it off for those.
	void SaveJpegSidecar(const std::filesystem::path& rPath, int iQuality, TextureOptions_t options);

	int64_t miWidth = 0;
	int64_t miHeight = 0;

	std::vector<std::vector<float>> mData;
};
