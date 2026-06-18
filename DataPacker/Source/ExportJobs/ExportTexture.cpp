#include "ExportTexture.h"

#include "FileManager.h"
#include "Texture/Texture.h"

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportTexture::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (rDirectoryEntry.path().filename().native().find(L"[C]") != std::wstring::npos)
	{
		return common::ChunkFlags_t({common::ChunkFlags::kTexture, common::ChunkFlags::kCubemap});
	}

	std::unordered_set<std::string> extensionSet = {".png", ".tga", ".jpg", ".ktx", ".BC4_UNORM_BLOCK", ".BC5_UNORM_BLOCK", ".BC7_UNORM_BLOCK", ".R16_UNORM", ".R16G16B16A16_SFLOAT"};
	return extensionSet.contains(rDirectoryEntry.path().extension().string()) ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kTexture) : std::nullopt;
}

static std::vector<std::byte> ZlibCompress(const std::byte* puiSource, int64_t iSourceSize)
{
	uLongf uiBound = compressBound(static_cast<uLong>(iSourceSize));
	std::vector<std::byte> compressed(uiBound);
	uLongf uiCompressedSize = uiBound;
	int iZlibResult = compress2(reinterpret_cast<Bytef*>(compressed.data()), &uiCompressedSize, reinterpret_cast<const Bytef*>(puiSource), static_cast<uLong>(iSourceSize), Z_BEST_COMPRESSION);
	ASSERT(iZlibResult == Z_OK);
	compressed.resize(uiCompressedSize);
	return compressed;
}

static int64_t ComputeUncompressedTextureSize(VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels)
{
	return common::ComputeImageByteSize(vkFormat, iWidth, iHeight, iMipLevels, 1, 1);
}

void ExportTexture::Export()
{
	VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
	if (mInputPath.native().find(L".R16_UNORM") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_R16_UNORM;
	}
	else if (mInputPath.native().find(L"[BC4]") != std::wstring::npos || mInputPath.native().find(L".BC4_UNORM_BLOCK") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC4_UNORM_BLOCK;
	}
	else if (mInputPath.native().find(L"[BC5]") != std::wstring::npos || mInputPath.native().find(L".BC5_UNORM_BLOCK") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
	}
	else if (mInputPath.native().find(L".R16G16B16A16_SFLOAT") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	}
	else if (mChunkFlags & kCubemap || mInputPath.native().find(L"[BC7]") != std::wstring::npos || mInputPath.native().find(L".BC7_UNORM_BLOCK") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
	}

	bool bRawTexture = mInputPath.native().find(L".R16_UNORM") != std::wstring::npos || mInputPath.native().find(L".BC4_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".BC5_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".BC7_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".R16G16B16A16_SFLOAT") != std::wstring::npos;

	if (mInputPath.native().find(L".ktx") != std::wstring::npos)
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
	gli::texture texture = gli::load(mInputPath.string());
	ASSERT(!texture.empty() && texture.target() == gli::TARGET_CUBE);

	gli::texture_cube textureCube(texture);
	ASSERT(textureCube.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16);

	int64_t iUncompressedSize = static_cast<int64_t>(textureCube.size());
	std::vector<std::byte> compressed = ZlibCompress(static_cast<const std::byte*>(textureCube.data()), iUncompressedSize);
	mChunkFlags.Set(kZlibCompressed);

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
	decltype(Texture::miWidth) iWidth = 0;
	decltype(Texture::miHeight) iHeight = 0;
	int64_t iMipMaps = 1;
	int64_t iFirstQword = 0;

	std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(&iFirstQword), sizeof(iFirstQword));
	int64_t iHeaderSize = 0;
	if (iFirstQword == kiTextureIntermediateMagic)
	{
		fileStream.read(reinterpret_cast<char*>(&iWidth), sizeof(iWidth));
		iHeaderSize = 4 * static_cast<int64_t>(sizeof(int64_t));
	}
	else
	{
		iWidth = static_cast<decltype(iWidth)>(iFirstQword);
		iHeaderSize = 3 * static_cast<int64_t>(sizeof(int64_t));
	}
	fileStream.read(reinterpret_cast<char*>(&iHeight), sizeof(iHeight));
	fileStream.read(reinterpret_cast<char*>(&iMipMaps), sizeof(iMipMaps));
	std::vector<std::byte> data(std::filesystem::file_size(mInputPath) - iHeaderSize);
	fileStream.read(reinterpret_cast<char*>(data.data()), data.size());
	fileStream.close();

	// BCn / R16 intermediates from Texture::Save are already zlib-streams — pass through.
	// .R16G16B16A16_SFLOAT cubemap intermediates from Generate{Irradiance,PreFiltered}Cubemaps
	// are written as raw half-float pixels with 6 cube faces packed in, so compress here and
	// use the actual on-disk payload size (which already includes 6 faces) instead of the
	// 2D-only mip-chain math from ComputeUncompressedTextureSize.
	mChunkFlags.Set(kZlibCompressed);

	const bool bRawIntermediateNeedsCompress = vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT;
	int64_t iUncompressedSize = bRawIntermediateNeedsCompress
		? static_cast<int64_t>(data.size())
		: ComputeUncompressedTextureSize(vkFormat, iWidth, iHeight, iMipMaps);

	std::vector<std::byte> compressed;
	if (bRawIntermediateNeedsCompress)
	{
		compressed = ZlibCompress(data.data(), static_cast<int64_t>(data.size()));
	}
	const std::vector<std::byte>& rChunkPayload = bRawIntermediateNeedsCompress ? compressed : data;

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(rChunkPayload.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = iWidth;
	pHeader->textureHeader.iTextureHeight = iHeight;
	pHeader->textureHeader.iMipLevels = iMipMaps;
	pHeader->textureHeader.vkFormat = vkFormat;

	std::memcpy(dataSpan.data(), rChunkPayload.data(), rChunkPayload.size());
}

void ExportTexture::ProcessLiveCubemap(VkFormat vkFormat)
{
	int64_t iWidth = 0;
	int64_t iHeight = 0;
	std::vector<std::byte> data;

	std::vector<std::string> facesOne = {"\\px.png", "\\nx.png", "\\py.png", "\\ny.png", "\\pz.png", "\\nz.png"};
	std::vector<std::string> facesTwo = {"\\posx.jpg", "\\negx.jpg", "\\posy.jpg", "\\negy.jpg", "\\posz.jpg", "\\negz.jpg"};
	std::vector<std::string>* pFaces = std::filesystem::exists(mInputPath.string().append(facesOne[0]).c_str()) ? &facesOne : &facesTwo;
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		for (int64_t i = 0; i < 6; ++i)
		{
			Texture texture(mInputPath.string().append((*pFaces)[i]).c_str(), FileType::kImage);
			iWidth = texture.miWidth;
			iHeight = texture.miHeight;
			texture.Export(data, vkFormat, TextureOptions::kVerifyNoAlpha);
		}
	}

	int64_t iUncompressedSize = static_cast<int64_t>(data.size());
	std::vector<std::byte> compressed = ZlibCompress(data.data(), iUncompressedSize);
	mChunkFlags.Set(kZlibCompressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = iWidth;
	pHeader->textureHeader.iTextureHeight = iHeight;
	pHeader->textureHeader.iMipLevels = 1;
	pHeader->textureHeader.vkFormat = vkFormat;
	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}

void ExportTexture::ProcessRegularTexture(VkFormat vkFormat)
{
	bool bFontAtlas = false;
	std::wstring stem = mInputPath.stem().native();
	if (auto pos = stem.rfind(L']'); pos != std::wstring::npos)
	{
		stem = stem.substr(pos + 1);
	}
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path fontsDir = rBaseDirectory / "Fonts";
		if (!std::filesystem::exists(fontsDir))
		{
			continue;
		}
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(fontsDir))
		{
			if (rEntry.path().extension() == ".fnt" && rEntry.path().stem().native() == stem)
			{
				bFontAtlas = true;
				break;
			}
		}
		if (bFontAtlas)
		{
			break;
		}
	}

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(mInputPath, FileType::kImage);
	TextureOptions_t mipOptions;
	mipOptions.Set(TextureOptions::kUseBoxFilter, bFontAtlas);
	texture.MakeMipmaps(vkFormat, 32, mipOptions);
	std::vector<std::byte> data = texture.Export(vkFormat, {});

	int64_t iUncompressedSize = static_cast<int64_t>(data.size());
	std::vector<std::byte> compressed = ZlibCompress(data.data(), iUncompressedSize);
	mChunkFlags.Set(kZlibCompressed);

	auto [pHeader, dataSpan] = AllocateHeaderAndData(static_cast<int64_t>(compressed.size()));
	pHeader->iUncompressedSize = iUncompressedSize;
	pHeader->textureHeader.iTextureWidth = texture.miWidth;
	pHeader->textureHeader.iTextureHeight = texture.miHeight;
	pHeader->textureHeader.iMipLevels = texture.mData.size();
	pHeader->textureHeader.vkFormat = vkFormat;
	std::memcpy(dataSpan.data(), compressed.data(), compressed.size());
}
