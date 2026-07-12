#pragma once

enum class FileType
{
	kExr,
	kFloat32,
	kImage,
	kUint16Raw,
};

// Per-call options for Texture loading, encoding, and debug output. Callers pass `{}` for no flags
// or e.g. `TextureOptions::kVerifyNoAlpha` to opt in.
enum class TextureOptions : uint8_t
{
	kVerifyNoAlpha = 0x02, // Encode/Export/Save BC7: assert the encoder didn't produce an alpha mode
	kGrayscale     = 0x08, // SaveJpegSidecar: replicate R into G/B (single-channel data)
	kAutoNormalize = 0x10, // SaveJpegSidecar: rescale R from observed [min,max] to byte [0,255]
};
using TextureOptions_t = common::Flags<TextureOptions>;

// Sentinel placed at byte 0 of every Texture::Save'd intermediate file. Reads as "BC7E DA7A"
// in a hex dump. A file lacking this magic is legacy-format and must be migrated through
// MigrateLegacyIntermediates() before any reader touches it.
inline constexpr int64_t kiTextureIntermediateMagic = 0x00000000BC7EDA7A;

// Maps a texture-intermediate VkFormat to its filename suffix -- the extension carried by the emitted
// intermediate and re-matched by ExportTexture's raw-passthrough routing. Single source for every
// producer (scene texture paths, island texture constants, the IBL writers), the consumer routing,
// and the inverse migration map, so the suffix can't drift between the site that builds a chunk's
// texture-CRC path and the site that emits the file (drift silently dangles the CRC against a missing
// intermediate). constexpr so callers can static_assert the embedded-suffix island filename constants.
constexpr const char* TextureIntermediateSuffix(VkFormat vkFormat)
{
	switch (vkFormat)
	{
		case VK_FORMAT_BC4_UNORM_BLOCK:
			return ".BC4_UNORM_BLOCK";
		case VK_FORMAT_BC5_UNORM_BLOCK:
			return ".BC5_UNORM_BLOCK";
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return ".BC7_UNORM_BLOCK";
		case VK_FORMAT_R16_UNORM:
			return ".R16_UNORM";
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			return ".R16G16B16A16_SFLOAT";
		default:
			ASSERT(false);
			return "";
	}
}

// zlib-DEFLATE a byte buffer at Z_BEST_COMPRESSION; returns a size-trimmed vector. Shared by the
// texture-intermediate writers (Texture::Save / MigrateLegacyIntermediates) and the RDO sweep so the
// compression level and Z_OK handling can't drift between them. The on-disk texture intermediates stay
// zlib; the .pack texture chunks themselves are LZ4 (see Lz4Compress) and transcoded from zlib in
// ExportTexture's raw-passthrough path.
std::vector<std::byte> ZlibCompress(const std::byte* puiSource, int64_t iSourceSize);

// LZ4HC-compress a byte buffer at LZ4HC_CLEVEL_MAX; returns a size-trimmed vector. Used for texture
// .pack chunk payloads, which the runtime FileManager LZ4-decompresses (5-10x faster than zlib inflate,
// no adler32 pass). Offline cost of max level is acceptable. Sibling of ZlibCompress.
std::vector<std::byte> Lz4Compress(const std::byte* puiSource, int64_t iSourceSize);

// Loads a gli container (.ktx / .dds / .kmg) through a wide-correct path read. gli::load(path) opens via ANSI
// fopen_s (no UTF-8 conversion), so a non-ASCII path fails or mis-resolves; this reads the whole file with the
// wide-correct std::filesystem::path stream, then hands gli the bytes via its memory overload. gli::load(path)
// itself does the identical read-whole-file-then-memory-parse, so the returned texture is byte-identical for
// valid inputs. Shared by ExportTexture's KTX cubemap path and the IBL cubemap pre-pass (ExportCubemapIbl).
gli::texture LoadGliFromPath(const std::filesystem::path& rPath);

// Parsed header of a Texture::Save'd texture intermediate plus the offset where its payload begins.
// Texture::Save writes [magic][width][height][mipCount][payload]; legacy files omit the magic (a
// 3-qword header). bHadMagic distinguishes the two; the payload occupies [iPayloadOffset, buffer end).
struct TextureIntermediateHeader
{
	int64_t iWidth = 0;
	int64_t iHeight = 0;
	int64_t iMipCount = 0;
	int64_t iPayloadOffset = 0;
	bool bHadMagic = false;
};

// Parse the magic-vs-legacy header from the front of a texture-intermediate byte buffer and locate
// the payload. PARSE/locate only -- imposes no validation policy, so each reader keeps its own
// (ExportTexture trusts, RdoSweep asserts, MigrateLegacyIntermediates branches on bHadMagic to skip
// already-migrated files then plausibility-checks the legacy header). For a magic-prefixed buffer
// `iDataSize` must cover >= 4 qwords; legacy needs >= 3 (callers that can't guarantee that gate the
// call with their own size check). Out-of-range qwords read as 0 rather than overrunning the buffer.
TextureIntermediateHeader ReadTextureIntermediateHeader(const std::byte* puiData, int64_t iDataSize);

// True when the filename carries the `[C]` cubemap tag — the convention marking a .ktx or
// face-image-directory cubemap input. Shared by ExportTexture routing and the IBL pre-pass
// (ExportCubemapIbl) so the tag test stays scoped to filename() at every site.
inline bool HasCubemapTag(const std::filesystem::path& rPath)
{
	return rPath.filename().native().find(L"[C]") != std::wstring::npos;
}

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

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel = 32)
	{
		ASSERT(mData.size() == 1);
		MakeMipmaps(vkFormat, iMaxLevel, 0, miWidth, miHeight);
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

private:

	// The file-loading constructor dispatches to one loader per FileType; each fills mData and (for the
	// image / EXR paths that don't receive dimensions) miWidth / miHeight. kFloat32 / kUint16Raw require
	// caller-supplied miWidth / miHeight.
	void LoadImage(const std::filesystem::path& rPath);
	void LoadFloat32(const std::filesystem::path& rPath);
	void LoadUint16Raw(const std::filesystem::path& rPath);
	void LoadExr(const std::filesystem::path& rPath);
};
