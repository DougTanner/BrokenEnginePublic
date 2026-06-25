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

	static constexpr std::string_view kExtensions[] = {".png", ".tga", ".jpg", ".ktx", ".BC4_UNORM_BLOCK", ".BC5_UNORM_BLOCK", ".BC7_UNORM_BLOCK", ".R16_UNORM", ".R16G16B16A16_SFLOAT"};
	std::string extension = rDirectoryEntry.path().extension().string();
	return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions) ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kTexture) : std::nullopt;
}

static int64_t ComputeUncompressedTextureSize(VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels)
{
	return common::ComputeImageByteSize(vkFormat, iWidth, iHeight, iMipLevels, 1, 1);
}

void ExportTexture::Export()
{
	// Format tokens live in either the file extension (explicit raw-intermediate formats) or the
	// filename (the [BC4]/[BC5]/[BC7] encode tags). Matching against extension()/filename() — not
	// the full path — keeps a parent directory whose name happens to contain a token (e.g. a folder
	// named "Foo.ktx") from misrouting every file beneath it.
	const std::wstring extension = mInputPath.extension().native();
	const std::wstring filename = mInputPath.filename().native();

	// bRawTexture covers only the explicit-extension formats (the already-encoded intermediates that
	// pass straight through). A [BC4]-tagged source resolves the BC4 format below but must still take
	// the encode path, so it is deliberately excluded here.
	const bool bRawTexture = extension == L".R16_UNORM" || extension == L".BC4_UNORM_BLOCK" || extension == L".BC5_UNORM_BLOCK" || extension == L".BC7_UNORM_BLOCK" || extension == L".R16G16B16A16_SFLOAT";

	VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
	if (extension == L".R16_UNORM")
	{
		vkFormat = VK_FORMAT_R16_UNORM;
	}
	else if (extension == L".BC4_UNORM_BLOCK" || filename.find(L"[BC4]") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC4_UNORM_BLOCK;
	}
	else if (extension == L".BC5_UNORM_BLOCK" || filename.find(L"[BC5]") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
	}
	else if (extension == L".R16G16B16A16_SFLOAT")
	{
		vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	}
	else if (mChunkFlags & kCubemap || extension == L".BC7_UNORM_BLOCK" || filename.find(L"[BC7]") != std::wstring::npos)
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
	// The font-atlas stem set is fixed for the whole run (input dirs resolve at startup), so collect
	// every Fonts/**/*.fnt stem once instead of re-walking the Fonts tree on every texture job. The
	// function-local static initializes thread-safely on first use even though jobs run concurrently
	// on std::async threads (see RunExportJobs in Main.cpp).
	static const std::unordered_set<std::wstring> fontStems = []
	{
		std::unordered_set<std::wstring> stems;
		for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
		{
			std::filesystem::path fontsDir = rBaseDirectory / "Fonts";
			if (!std::filesystem::exists(fontsDir))
			{
				continue;
			}
			for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(fontsDir))
			{
				if (rEntry.path().extension() == ".fnt")
				{
					stems.insert(rEntry.path().stem().native());
				}
			}
		}
		return stems;
	}();

	std::wstring stem = mInputPath.stem().native();
	if (size_t uiBracket = stem.rfind(L']'); uiBracket != std::wstring::npos)
	{
		stem = stem.substr(uiBracket + 1);
	}
	const bool bFontAtlas = fontStems.contains(stem);

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
