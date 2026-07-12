#include "ExportTexture.h"

#include "FileManager.h"
#include "Texture/Texture.h"

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportTexture::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (HasCubemapTag(rDirectoryEntry.path()))
	{
		return common::ChunkFlags_t({common::ChunkFlags::kTexture, common::ChunkFlags::kCubemap});
	}

	static constexpr std::string_view kExtensions[] = {".png", ".tga", ".jpg", ".ktx", TextureIntermediateSuffix(VK_FORMAT_BC4_UNORM_BLOCK), TextureIntermediateSuffix(VK_FORMAT_BC5_UNORM_BLOCK), TextureIntermediateSuffix(VK_FORMAT_BC7_UNORM_BLOCK), TextureIntermediateSuffix(VK_FORMAT_R16_UNORM), TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT)};
	std::string extension = rDirectoryEntry.path().extension().string();
	return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions) ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kTexture) : std::nullopt;
}

static int64_t ComputeUncompressedTextureSize(VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels)
{
	return common::ComputeImageByteSize(vkFormat, iWidth, iHeight, iMipLevels, 1, 1);
}

void ExportTexture::Export()
{
	if (gpFileManager->mbForbidExpensiveExport)
	{
		throw std::runtime_error("Texture export blocked by BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1");
	}

	// Format tokens live in either the file extension (explicit raw-intermediate formats) or the
	// filename (the [BC4]/[BC5]/[BC7] encode tags). Matching against extension()/filename() — not
	// the full path — keeps a parent directory whose name happens to contain a token (e.g. a folder
	// named "Foo.ktx") from misrouting every file beneath it.
	const std::wstring extension = mInputPath.extension().native();
	const std::wstring filename = mInputPath.filename().native();

	// Narrow copy of the extension for matching the canonical texture-intermediate suffixes
	// (TextureIntermediateSuffix returns narrow const char*) so this consumer can't drift from the
	// producers; the [BC4]/[BC5]/[BC7] encode-tag checks and the .ktx test below stay on the wide
	// strings. The suffixes are ASCII, so the narrow comparison is exact.
	const std::string narrowExtension = mInputPath.extension().string();

	// bRawTexture covers only the explicit-extension formats (the already-encoded intermediates that
	// pass straight through). A [BC4]-tagged source resolves the BC4 format below but must still take
	// the encode path, so it is deliberately excluded here.
	const bool bRawTexture = narrowExtension == TextureIntermediateSuffix(VK_FORMAT_R16_UNORM) || narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC4_UNORM_BLOCK) || narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC5_UNORM_BLOCK) || narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC7_UNORM_BLOCK) || narrowExtension == TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT);

	VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
	if (narrowExtension == TextureIntermediateSuffix(VK_FORMAT_R16_UNORM))
	{
		vkFormat = VK_FORMAT_R16_UNORM;
	}
	else if (narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC4_UNORM_BLOCK) || filename.find(L"[BC4]") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC4_UNORM_BLOCK;
	}
	else if (narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC5_UNORM_BLOCK) || filename.find(L"[BC5]") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
	}
	else if (narrowExtension == TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT))
	{
		vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	}
	else if (mChunkFlags & kCubemap || narrowExtension == TextureIntermediateSuffix(VK_FORMAT_BC7_UNORM_BLOCK) || filename.find(L"[BC7]") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
	}

	// Dispatch order is load-bearing: .ktx and raw passthrough win over the cubemap flag, so a
	// [C]-tagged raw intermediate (e.g. the IBL .R16G16B16A16_SFLOAT outputs) stays on the raw path.
	if (extension == L".ktx")
	{
		ProcessKtxCubemap();
	}
	else if (bRawTexture)
	{
		ProcessRawTexture(vkFormat);
	}
	else if (mChunkFlags & kCubemap)
	{
		ProcessLiveCubemap(vkFormat);
	}
	else
	{
		ProcessRegularTexture(vkFormat);
	}
}

void ExportTexture::ProcessKtxCubemap()
{
	gli::texture texture = LoadGliFromPath(mInputPath);
	ASSERT(!texture.empty() && texture.target() == gli::TARGET_CUBE);

	gli::texture_cube textureCube(texture);
	ASSERT(textureCube.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16);

	int64_t iUncompressedSize = static_cast<int64_t>(textureCube.size());
	std::vector<std::byte> compressed = Lz4Compress(static_cast<const std::byte*>(textureCube.data()), iUncompressedSize);
	mChunkFlags.Set(kLz4Compressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = textureCube[0].extent().x;
	pHeader->textureHeader.iTextureHeight = textureCube[0].extent().y;
	pHeader->textureHeader.iMipLevels = textureCube.levels();
	pHeader->textureHeader.vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}

void ExportTexture::ProcessRawTexture(VkFormat vkFormat)
{
	std::vector<std::byte> fileBytes = common::ReadEntireFile(mInputPath);
	TextureIntermediateHeader header = ReadTextureIntermediateHeader(fileBytes.data(), static_cast<int64_t>(fileBytes.size()));
	int64_t iWidth = header.iWidth;
	int64_t iHeight = header.iHeight;
	int64_t iMipMaps = header.iMipCount;
	std::vector<std::byte> data(fileBytes.begin() + header.iPayloadOffset, fileBytes.end());

	// Texture .pack chunks are LZ4-compressed (the runtime FileManager LZ4-decompresses them). Neither
	// raw-passthrough intermediate is LZ4 on disk, so both transcode into an LZ4 chunk here:
	//   * BCn / R16 intermediates from Texture::Save are zlib streams on disk (that intermediate format
	//     is unchanged) — zlib-inflate to raw bytes, then LZ4-compress. Uncompressed size is the 2D
	//     mip-chain byte count derived from the intermediate dims.
	//   * .R16G16B16A16_SFLOAT cubemap intermediates from Generate{Irradiance,PreFiltered}Cubemaps are
	//     raw half-float pixels with 6 cube faces packed in — LZ4-compress directly, using the on-disk
	//     payload size (already 6 faces) instead of the 2D-only ComputeUncompressedTextureSize math.
	const bool bRawHalfFloat = vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT;
	int64_t iUncompressedSize = bRawHalfFloat
		? static_cast<int64_t>(data.size())
		: ComputeUncompressedTextureSize(vkFormat, iWidth, iHeight, iMipMaps);

	std::vector<std::byte> inflated;
	if (!bRawHalfFloat)
	{
		// zlib intermediate payload is opaque input; inflate into a fixed-size buffer and assert an exact
		// fill (DataPacker has no soft-fail path — a mismatch is a producer bug, not corrupt shipped data).
		inflated.resize(static_cast<size_t>(iUncompressedSize));
		uLongf uiInflatedSize = static_cast<uLongf>(iUncompressedSize);
		int iZlibResult = uncompress(reinterpret_cast<Bytef*>(inflated.data()), &uiInflatedSize, reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()));
		ASSERT(iZlibResult == Z_OK && static_cast<int64_t>(uiInflatedSize) == iUncompressedSize);
	}
	const std::vector<std::byte>& rRawBytes = bRawHalfFloat ? data : inflated;

	std::vector<std::byte> compressed = Lz4Compress(rRawBytes.data(), static_cast<int64_t>(rRawBytes.size()));
	mChunkFlags.Set(kLz4Compressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = iWidth;
	pHeader->textureHeader.iTextureHeight = iHeight;
	pHeader->textureHeader.iMipLevels = iMipMaps;
	pHeader->textureHeader.vkFormat = vkFormat;

	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}

void ExportTexture::ProcessLiveCubemap(VkFormat vkFormat)
{
	int64_t iWidth = 0;
	int64_t iHeight = 0;
	std::vector<std::byte> data;

	static constexpr const char* kpcPngFaceNames[6] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
	static constexpr const char* kpcJpgFaceNames[6] = {"posx.jpg", "negx.jpg", "posy.jpg", "negy.jpg", "posz.jpg", "negz.jpg"};
	const char* const* pFaceNames = std::filesystem::exists(mInputPath / kpcPngFaceNames[0]) ? kpcPngFaceNames : kpcJpgFaceNames;
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		for (int64_t i = 0; i < 6; ++i)
		{
			Texture texture(mInputPath / pFaceNames[i], FileType::kImage);
			iWidth = texture.miWidth;
			iHeight = texture.miHeight;
			texture.Export(data, vkFormat, TextureOptions::kVerifyNoAlpha);
		}
	}

	int64_t iUncompressedSize = static_cast<int64_t>(data.size());
	std::vector<std::byte> compressed = Lz4Compress(data.data(), iUncompressedSize);
	mChunkFlags.Set(kLz4Compressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = iWidth;
	pHeader->textureHeader.iTextureHeight = iHeight;
	pHeader->textureHeader.iMipLevels = 1;
	pHeader->textureHeader.vkFormat = vkFormat;
	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}

// Per-mip Toksvig slope variance for a BC5 normal map: decode mip 0 to 3D normals, box-average a
// pyramid WITHOUT renormalizing (the shortened mean-normal length IS the sub-texel variance), and
// record mean((1 - |avgN|) / |avgN|) per level. Baked here because the runtime cannot recover it:
// BC5 stores only XY and the shader's DecodeNormal reconstructs a unit-length Z, so mip filtering
// silently discards the variance. Consumed by Water.frag's WATER_SPEC_AA_MIP_HANDOFF kernel via
// TextureHeader::pfMipVariance. Levels past iMipLevels pad with the last real value so the engine
// reader indexes by clamped LOD without a count.
static void ComputeBc5MipVariance(const Texture& rTexture, float pfOutVariance[common::TextureHeader::kiMipVarianceCount])
{
	int64_t iWidth = rTexture.miWidth;
	int64_t iHeight = rTexture.miHeight;
	const std::vector<float>& rMip0 = rTexture.mData.at(0);

	// Decode matches Water.frag's sign-inverted DecodeNormal (the sign flip is irrelevant to the
	// length statistic but keeps the two decodes textually comparable). Pixels are 0-255 scale.
	std::vector<float> normals(3 * iWidth * iHeight);
	for (int64_t i = 0; i < iWidth * iHeight; ++i)
	{
		float fX = 1.0f - 2.0f * (rMip0.at(4 * i + 0) / 255.0f);
		float fY = 1.0f - 2.0f * (rMip0.at(4 * i + 1) / 255.0f);
		normals.at(3 * i + 0) = fX;
		normals.at(3 * i + 1) = fY;
		normals.at(3 * i + 2) = std::sqrt(std::max(0.0f, 1.0f - fX * fX - fY * fY));
	}

	int64_t iMipLevels = static_cast<int64_t>(rTexture.mData.size());
	for (int64_t iLevel = 0; iLevel < common::TextureHeader::kiMipVarianceCount; ++iLevel)
	{
		if (iLevel >= iMipLevels)
		{
			pfOutVariance[iLevel] = pfOutVariance[iLevel - 1];
			continue;
		}

		double dVarianceSum = 0.0;
		for (int64_t i = 0; i < iWidth * iHeight; ++i)
		{
			float fLength = std::sqrt(normals.at(3 * i) * normals.at(3 * i) + normals.at(3 * i + 1) * normals.at(3 * i + 1) + normals.at(3 * i + 2) * normals.at(3 * i + 2));
			dVarianceSum += (1.0 - fLength) / std::max(fLength, 0.01f);
		}
		pfOutVariance[iLevel] = static_cast<float>(dVarianceSum / static_cast<double>(iWidth * iHeight));

		// 2x2 box-average down to the next level, clamping the source coordinate for odd dimensions
		int64_t iNextWidth = std::max<int64_t>(iWidth / 2, 1);
		int64_t iNextHeight = std::max<int64_t>(iHeight / 2, 1);
		std::vector<float> downsampled(3 * iNextWidth * iNextHeight);
		for (int64_t iY = 0; iY < iNextHeight; ++iY)
		{
			for (int64_t iX = 0; iX < iNextWidth; ++iX)
			{
				int64_t iX0 = 2 * iX;
				int64_t iY0 = 2 * iY;
				int64_t iX1 = std::min(iX0 + 1, iWidth - 1);
				int64_t iY1 = std::min(iY0 + 1, iHeight - 1);
				for (int64_t iChannel = 0; iChannel < 3; ++iChannel)
				{
					downsampled.at(3 * (iY * iNextWidth + iX) + iChannel) = 0.25f * (
						normals.at(3 * (iY0 * iWidth + iX0) + iChannel) +
						normals.at(3 * (iY0 * iWidth + iX1) + iChannel) +
						normals.at(3 * (iY1 * iWidth + iX0) + iChannel) +
						normals.at(3 * (iY1 * iWidth + iX1) + iChannel));
				}
			}
		}
		normals = std::move(downsampled);
		iWidth = iNextWidth;
		iHeight = iNextHeight;
	}
}

void ExportTexture::ProcessRegularTexture(VkFormat vkFormat)
{
	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(mInputPath, FileType::kImage);
	texture.MakeMipmaps(vkFormat);

	float pfMipVariance[common::TextureHeader::kiMipVarianceCount] {};
	if (vkFormat == VK_FORMAT_BC5_UNORM_BLOCK)
	{
		ComputeBc5MipVariance(texture, pfMipVariance);
	}

	std::vector<std::byte> data = texture.Export(vkFormat, {});

	int64_t iUncompressedSize = static_cast<int64_t>(data.size());
	std::vector<std::byte> compressed = Lz4Compress(data.data(), iUncompressedSize);
	mChunkFlags.Set(kLz4Compressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = texture.miWidth;
	pHeader->textureHeader.iTextureHeight = texture.miHeight;
	pHeader->textureHeader.iMipLevels = texture.mData.size();
	pHeader->textureHeader.vkFormat = vkFormat;
	std::memcpy(pHeader->textureHeader.pfMipVariance, pfMipVariance, sizeof(pfMipVariance));
	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}
