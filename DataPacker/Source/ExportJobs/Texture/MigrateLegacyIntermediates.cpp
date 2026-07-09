#include "MigrateLegacyIntermediates.h"

#include "FileManager.h"
#include "Texture.h"

// Inverse of TextureIntermediateSuffix, deliberately scoped to the migration-eligible formats. The
// IBL .R16G16B16A16_SFLOAT intermediates are excluded on purpose -- they are legacy-shaped by design
// and must never be picked up by the migration pass. The suffix strings come from the canonical
// TextureIntermediateSuffix helper so the inverse can't drift from the forward map.
static VkFormat IntermediateFormatFromExtension(const std::filesystem::path& rPath)
{
	const std::string sExtension = rPath.extension().string();
	if (sExtension == TextureIntermediateSuffix(VK_FORMAT_BC4_UNORM_BLOCK))
	{
		return VK_FORMAT_BC4_UNORM_BLOCK;
	}
	if (sExtension == TextureIntermediateSuffix(VK_FORMAT_BC5_UNORM_BLOCK))
	{
		return VK_FORMAT_BC5_UNORM_BLOCK;
	}
	if (sExtension == TextureIntermediateSuffix(VK_FORMAT_BC7_UNORM_BLOCK))
	{
		return VK_FORMAT_BC7_UNORM_BLOCK;
	}
	if (sExtension == TextureIntermediateSuffix(VK_FORMAT_R16_UNORM))
	{
		return VK_FORMAT_R16_UNORM;
	}
	return VK_FORMAT_UNDEFINED;
}

// Decode a single mip's BCn block stream (raw, no header) to an RGBA8 byte buffer.
// BC4 fills R only (G=B=0, A=255). BC5 fills R+G (B=0, A=255). BC7 fills RGBA verbatim.
static std::vector<uint8_t> DecodeBcnMip0ToRgba8(const std::byte* puiEncoded, int64_t iWidth, int64_t iHeight, VkFormat vkFormat)
{
	int64_t iBlocksX = (iWidth + 3) / 4;
	int64_t iBlocksY = (iHeight + 3) / 4;
	int64_t iBlockSizeBytes = (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK) ? 8 : 16;
	std::vector<uint8_t> rgba8(static_cast<size_t>(iWidth * iHeight * 4));

	for (int64_t iBlockY = 0; iBlockY < iBlocksY; ++iBlockY)
	{
		for (int64_t iBlockX = 0; iBlockX < iBlocksX; ++iBlockX)
		{
			const std::byte* pBlock = puiEncoded + (iBlockY * iBlocksX + iBlockX) * iBlockSizeBytes;
			uint8_t blockRgba[16 * 4] = {};

			switch (vkFormat)
			{
				case VK_FORMAT_BC4_UNORM_BLOCK:
				{
					uint8_t bc4Pixels[16] = {};
					rgbcx::unpack_bc4(pBlock, bc4Pixels, 1);
					for (int i = 0; i < 16; ++i)
					{
						blockRgba[i * 4 + 0] = bc4Pixels[i];
						blockRgba[i * 4 + 3] = 255;
					}
					break;
				}
				case VK_FORMAT_BC5_UNORM_BLOCK:
				{
					uint8_t bc5Pixels[16 * 2] = {};
					rgbcx::unpack_bc5(pBlock, bc5Pixels, 0, 1, 2);
					for (int i = 0; i < 16; ++i)
					{
						blockRgba[i * 4 + 0] = bc5Pixels[i * 2 + 0];
						blockRgba[i * 4 + 1] = bc5Pixels[i * 2 + 1];
						blockRgba[i * 4 + 3] = 255;
					}
					break;
				}
				case VK_FORMAT_BC7_UNORM_BLOCK:
				{
					bc7decomp::color_rgba bc7Pixels[16] = {};
					bc7decomp::unpack_bc7(pBlock, bc7Pixels);
					for (int i = 0; i < 16; ++i)
					{
						blockRgba[i * 4 + 0] = bc7Pixels[i].r;
						blockRgba[i * 4 + 1] = bc7Pixels[i].g;
						blockRgba[i * 4 + 2] = bc7Pixels[i].b;
						blockRgba[i * 4 + 3] = bc7Pixels[i].a;
					}
					break;
				}
				default:
					ASSERT(false);
					break;
			}

			for (int64_t by = 0; by < 4; ++by)
			{
				for (int64_t bx = 0; bx < 4; ++bx)
				{
					int64_t iX = iBlockX * 4 + bx;
					int64_t iY = iBlockY * 4 + by;
					if (iX >= iWidth || iY >= iHeight)
					{
						continue;
					}
					int64_t iDestIndex = (iY * iWidth + iX) * 4;
					int64_t iBlockIndex = (by * 4 + bx) * 4;
					rgba8.at(iDestIndex + 0) = blockRgba[iBlockIndex + 0];
					rgba8.at(iDestIndex + 1) = blockRgba[iBlockIndex + 1];
					rgba8.at(iDestIndex + 2) = blockRgba[iBlockIndex + 2];
					rgba8.at(iDestIndex + 3) = blockRgba[iBlockIndex + 3];
				}
			}
		}
	}

	return rgba8;
}

// Idempotent migration: skips files already prefixed with kiTextureIntermediateMagic.
// For legacy raw BCn files: decodes mip 0, re-encodes with current RDO knobs (Texture::Save
// regenerates the mip chain and writes the new format with magic). For legacy raw R16 files:
// just zlib-wraps with magic — R16 has no encoding step.
static void MigrateLegacyIntermediate(const std::filesystem::path& rPath)
{
	VkFormat vkFormat = IntermediateFormatFromExtension(rPath);
	if (vkFormat == VK_FORMAT_UNDEFINED)
	{
		return;
	}

	int64_t iFileSize = std::filesystem::file_size(rPath);
	int64_t iLegacyHeaderSize = 3 * static_cast<int64_t>(sizeof(int64_t));
	if (iFileSize < iLegacyHeaderSize + static_cast<int64_t>(sizeof(int64_t)))
	{
		return;
	}

	// Peek the header region (>= the 4-qword magic shape, guaranteed in-bounds by the size check above).
	std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
	std::byte headerBytes[4 * sizeof(int64_t)] = {};
	fileStream.read(reinterpret_cast<char*>(headerBytes), sizeof(headerBytes));
	TextureIntermediateHeader header = ReadTextureIntermediateHeader(headerBytes, static_cast<int64_t>(sizeof(headerBytes)));
	if (header.bHadMagic)
	{
		return;
	}

	auto tMigrateStart = std::chrono::steady_clock::now();
	int64_t iSizeBefore = iFileSize;

	int64_t iWidth = header.iWidth;
	int64_t iHeight = header.iHeight;
	int64_t iMipMaps = header.iMipCount;

	if (iWidth <= 0 || iHeight <= 0 || iMipMaps <= 0 || iWidth > 32768 || iHeight > 32768 || iMipMaps > 32)
	{
		return;
	}

	int64_t iExpectedRawSize = common::ComputeImageByteSize(vkFormat, iWidth, iHeight, iMipMaps, 1, 1);

	int64_t iPayloadSize = iFileSize - iLegacyHeaderSize;
	std::vector<std::byte> payload(iPayloadSize);
	fileStream.seekg(header.iPayloadOffset);
	fileStream.read(reinterpret_cast<char*>(payload.data()), iPayloadSize);
	fileStream.close();

	// Two valid legacy shapes for the payload:
	//   1. Pre-zlib raw bytes (size == iExpectedRawSize)
	//   2. Zlib-wrapped without magic (size != raw, but decompresses to iExpectedRawSize)
	// The second case happens for files migrated by an earlier session before this magic
	// scheme was added. Both flow through the same decode + re-encode below.
	std::vector<std::byte> rawBytes;
	if (iPayloadSize == iExpectedRawSize)
	{
		rawBytes = std::move(payload);
	}
	else
	{
		rawBytes.resize(iExpectedRawSize);
		uLongf uiUncompressedSize = static_cast<uLongf>(iExpectedRawSize);
		int iZlibResult = uncompress(reinterpret_cast<Bytef*>(rawBytes.data()), &uiUncompressedSize, reinterpret_cast<const Bytef*>(payload.data()), static_cast<uLong>(iPayloadSize));
		if (iZlibResult != Z_OK || static_cast<int64_t>(uiUncompressedSize) != iExpectedRawSize)
		{
			return;
		}
	}

	if (vkFormat == VK_FORMAT_R16_UNORM)
	{
		// R16 has no encoding step. Compress the raw bytes and rewrite with magic.
		std::vector<std::byte> compressed = ZlibCompress(rawBytes.data(), iExpectedRawSize);

		std::filesystem::remove(rPath);
		std::fstream fileStreamOut(rPath, std::ios::out | std::ios::binary);
		int64_t iMagic = kiTextureIntermediateMagic;
		fileStreamOut.write(reinterpret_cast<const char*>(&iMagic), sizeof(iMagic));
		fileStreamOut.write(reinterpret_cast<const char*>(&iWidth), sizeof(iWidth));
		fileStreamOut.write(reinterpret_cast<const char*>(&iHeight), sizeof(iHeight));
		fileStreamOut.write(reinterpret_cast<const char*>(&iMipMaps), sizeof(iMipMaps));
		fileStreamOut.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
		fileStreamOut.flush();
		fileStreamOut.close();
		VERIFY_SUCCESS(fileStreamOut.good());

		auto tMigrateEnd = std::chrono::steady_clock::now();
		double fMigrateSeconds = std::chrono::duration<double>(tMigrateEnd - tMigrateStart).count();
		int64_t iSizeAfter = std::filesystem::file_size(rPath);
		LOG(kDefault, kInfo, "Migrated R16 (zlib + magic): \"{}\" ({} -> {} bytes raw; {} -> {} bytes on-disk; {:.2f}s)", rPath.string(), iExpectedRawSize, static_cast<int64_t>(compressed.size()), iSizeBefore, iSizeAfter, fMigrateSeconds);
		return;
	}

	std::vector<uint8_t> rgba8 = DecodeBcnMip0ToRgba8(rawBytes.data(), iWidth, iHeight, vkFormat);

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(reinterpret_cast<const std::byte*>(rgba8.data()), iWidth, iHeight, 4);
		texture.MakeMipmaps(vkFormat);
		texture.Save(rPath, vkFormat, {});
	}

	auto tMigrateEnd = std::chrono::steady_clock::now();
	double fMigrateSeconds = std::chrono::duration<double>(tMigrateEnd - tMigrateStart).count();
	int64_t iSizeAfter = std::filesystem::file_size(rPath);
	LOG(kDefault, kInfo, "Migrated BCn (decode + re-encode with current knobs + magic): \"{}\" ({}x{}; {} -> {} bytes on-disk; {:.2f}s)", rPath.string(), iWidth, iHeight, iSizeBefore, iSizeAfter, fMigrateSeconds);
}

void MigrateLegacyIntermediates()
{
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rEntry.is_regular_file())
			{
				MigrateLegacyIntermediate(rEntry.path());
			}
		}
	}
}
