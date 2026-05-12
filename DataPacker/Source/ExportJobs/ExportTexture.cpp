#include "ExportTexture.h"

#include "FileManager.h"
#include "Texture.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#define GLM_STATIC_ASSERT static_assert
#include "gli/gli/gli.hpp"
#undef malloc
#undef realloc
#undef free
#include <cmft/clcontext.h>
#include <cmft/image.h>
#include <cmft/cubemapfilter.h>
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

using enum common::ChunkFlags;

struct KtxCubemapData
{
	std::vector<float> floatData;
	uint32_t uiFaceSize = 0;
};

KtxCubemapData LoadKtxCubemapAsFloat(const std::filesystem::path& rPath)
{
	KtxCubemapData result;
	gli::texture texture = gli::load(rPath.string());
	ASSERT(!texture.empty() && texture.target() == gli::TARGET_CUBE);
	gli::texture_cube textureCube(texture);
	ASSERT(textureCube.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16);
	result.uiFaceSize = textureCube[0].extent().x;
	uint32_t uiPixelsPerFace = result.uiFaceSize * result.uiFaceSize;
	result.floatData.resize(uiPixelsPerFace * 6 * 4);
	for (int64_t iFace = 0; iFace < 6; ++iFace)
	{
		const uint16_t* pSrcHalf = static_cast<const uint16_t*>(textureCube[iFace].data());
		float* pDstFloat = result.floatData.data() + iFace * uiPixelsPerFace * 4;
		for (uint32_t uiPixel = 0; uiPixel < uiPixelsPerFace; ++uiPixel)
		{
			for (uint32_t uiChannel = 0; uiChannel < 4; ++uiChannel)
			{
				pDstFloat[uiPixel * 4 + uiChannel] = DirectX::PackedVector::XMConvertHalfToFloat(pSrcHalf[uiPixel * 4 + uiChannel]);
			}
		}
	}
	return result;
}

void GenerateIrradianceCubemaps()
{
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rDirectoryEntry.path().extension() != ".ktx")
			{
				continue;
			}

			if (rDirectoryEntry.path().filename().native().find(L"[C]") == std::wstring::npos)
			{
				continue;
			}

			// Build output path: [C]<name>_Irradiance.R16G16B16A16_SFLOAT
			std::filesystem::path outputPath = rDirectoryEntry.path().parent_path() / rDirectoryEntry.path().stem();
			outputPath += "_Irradiance.R16G16B16A16_SFLOAT";

			// Skip if intermediate output already exists and is newer than the source
			if (std::filesystem::exists(outputPath) && std::filesystem::last_write_time(outputPath) >= std::filesystem::last_write_time(rDirectoryEntry.path()))
			{
				continue;
			}

			LOG(kDefault, kDebug, "Generating irradiance cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			KtxCubemapData cubemapData = LoadKtxCubemapAsFloat(rDirectoryEntry.path());
			uint32_t uiFaceSize = cubemapData.uiFaceSize;
			uint32_t uiPixelsPerFace = uiFaceSize * uiFaceSize;
			uint32_t uiTotalPixels = uiPixelsPerFace * 6;

			// Create CMFT source image and populate with float data
			cmft::Image srcImage;
			cmft::imageCreate(srcImage, uiFaceSize, uiFaceSize, 0x000000ff, 1, 6, cmft::TextureFormat::RGBA32F);
			std::memcpy(srcImage.m_data, cubemapData.floatData.data(), uiTotalPixels * 4 * sizeof(float));

			// Generate 128x128 irradiance cubemap using spherical harmonics
			static constexpr uint32_t kuiIrradianceFaceSize = 128;
			cmft::Image dstImage;
			cmft::imageIrradianceFilterSh(dstImage, kuiIrradianceFaceSize, srcImage);

			// Convert RGBA32F result back to RGBA16F
			uint32_t uiIrradiancePixelsPerFace = kuiIrradianceFaceSize * kuiIrradianceFaceSize;
			uint32_t uiIrradianceTotalPixels = uiIrradiancePixelsPerFace * 6;
			std::vector<uint16_t> halfData(uiIrradianceTotalPixels * 4);

			const float* pSrcFloat = static_cast<const float*>(dstImage.m_data);
			for (uint32_t i = 0; i < uiIrradianceTotalPixels * 4; ++i)
			{
				halfData.at(i) = DirectX::PackedVector::XMConvertFloatToHalf(pSrcFloat[i]);
			}

			// Write intermediate file: [width][height][mipcount][pixel data for 6 faces]
			int64_t iWidth = kuiIrradianceFaceSize;
			int64_t iHeight = kuiIrradianceFaceSize;
			int64_t iMipCount = 1;

			std::fstream fileStream(outputPath, std::ios::out | std::ios::binary);
			fileStream.write(reinterpret_cast<const char*>(&iWidth), sizeof(iWidth));
			fileStream.write(reinterpret_cast<const char*>(&iHeight), sizeof(iHeight));
			fileStream.write(reinterpret_cast<const char*>(&iMipCount), sizeof(iMipCount));
			fileStream.write(reinterpret_cast<const char*>(halfData.data()), halfData.size() * sizeof(uint16_t));
			fileStream.flush();
			fileStream.close();

			// Clean up CMFT images
			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}
}

void GeneratePreFilteredCubemaps()
{
	static constexpr uint32_t kuiFaceSize = 1024;
	static constexpr uint8_t kuiMipCount = 11; // log2(1024) + 1
	static const uint8_t kuiCpuThreads = static_cast<uint8_t>(std::max(1u, std::thread::hardware_concurrency()));

	cmft::ClContext* pClContext = nullptr;
	if (cmft::clLoad() != 0)
	{
		pClContext = cmft::clInit(CMFT_CL_VENDOR_ANY_GPU, CMFT_CL_DEVICE_TYPE_GPU);
	}

	auto convertAndWrite = [](cmft::Image& dstImage, const std::filesystem::path& outputPath)
	{
		// Get per-face/per-mip byte offsets in CMFT output
		uint32_t offsets[CUBE_FACE_NUM][MAX_MIP_NUM] {};
		cmft::imageGetMipOffsets(offsets, dstImage);

		// Calculate total half-float pixel count across all faces and mips
		uint32_t uiTotalHalfFloats = 0;
		uint32_t uiMipSize = kuiFaceSize;
		for (uint8_t uiMip = 0; uiMip < kuiMipCount; ++uiMip, uiMipSize /= 2)
		{
			uiTotalHalfFloats += uiMipSize * uiMipSize * 4;
		}
		uiTotalHalfFloats *= 6; // 6 faces

		std::vector<uint16_t> halfData(uiTotalHalfFloats);
		uint32_t uiHalfOffset = 0;

		// Write in face-major/mip-minor order to match engine's TextureUploadManager iteration order
		for (int64_t iFace = 0; iFace < 6; ++iFace)
		{
			uiMipSize = kuiFaceSize;
			for (uint8_t uiMip = 0; uiMip < kuiMipCount; ++uiMip, uiMipSize /= 2)
			{
				uint32_t uiMipPixels = uiMipSize * uiMipSize;
				const float* pSrcFloat = reinterpret_cast<const float*>(static_cast<uint8_t*>(dstImage.m_data) + offsets[iFace][uiMip]);
				for (uint32_t i = 0; i < uiMipPixels * 4; ++i)
				{
					halfData.at(uiHalfOffset++) = DirectX::PackedVector::XMConvertFloatToHalf(pSrcFloat[i]);
				}
			}
		}

		// Write intermediate file: [width][height][mipcount][pixel data]
		int64_t iWidth = kuiFaceSize;
		int64_t iHeight = kuiFaceSize;
		int64_t iMipCount = kuiMipCount;

		std::fstream fileStream(outputPath, std::ios::out | std::ios::binary);
		fileStream.write(reinterpret_cast<const char*>(&iWidth), sizeof(iWidth));
		fileStream.write(reinterpret_cast<const char*>(&iHeight), sizeof(iHeight));
		fileStream.write(reinterpret_cast<const char*>(&iMipCount), sizeof(iMipCount));
		fileStream.write(reinterpret_cast<const char*>(halfData.data()), halfData.size() * sizeof(uint16_t));
		fileStream.flush();
		fileStream.close();
	};

	// Phase 1: KTX cubemaps
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rDirectoryEntry.path().extension() != ".ktx")
			{
				continue;
			}

			if (rDirectoryEntry.path().filename().native().find(L"[C]") == std::wstring::npos)
			{
				continue;
			}

			std::filesystem::path outputPath = rDirectoryEntry.path().parent_path() / rDirectoryEntry.path().stem();
			outputPath += "_Prefiltered.R16G16B16A16_SFLOAT";

			if (std::filesystem::exists(outputPath) && std::filesystem::last_write_time(outputPath) >= std::filesystem::last_write_time(rDirectoryEntry.path()))
			{
				continue;
			}

			LOG(kDefault, kDebug, "Generating pre-filtered cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			KtxCubemapData cubemapData = LoadKtxCubemapAsFloat(rDirectoryEntry.path());
			uint32_t uiFaceSize = cubemapData.uiFaceSize;
			uint32_t uiPixelsPerFace = uiFaceSize * uiFaceSize;
			uint32_t uiTotalPixels = uiPixelsPerFace * 6;

			cmft::Image srcImage;
			cmft::imageCreate(srcImage, uiFaceSize, uiFaceSize, 0x000000ff, 1, 6, cmft::TextureFormat::RGBA32F);
			std::memcpy(srcImage.m_data, cubemapData.floatData.data(), uiTotalPixels * 4 * sizeof(float));

			cmft::Image dstImage;
			cmft::imageRadianceFilter(dstImage, kuiFaceSize, cmft::LightingModel::BlinnBrdf, false, kuiMipCount, 14, 4, srcImage, cmft::EdgeFixup::None, kuiCpuThreads, pClContext);

			convertAndWrite(dstImage, outputPath);

			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}

	// Phase 2: Face-image cubemaps (directory with [C] in name)
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (!rDirectoryEntry.is_directory())
			{
				continue;
			}

			if (rDirectoryEntry.path().filename().native().find(L"[C]") == std::wstring::npos)
			{
				continue;
			}

			// Check for face files
			bool bHasJpgFaces = std::filesystem::exists(rDirectoryEntry.path() / "posx.jpg");
			bool bHasPngFaces = std::filesystem::exists(rDirectoryEntry.path() / "px.png");
			if (!bHasJpgFaces && !bHasPngFaces)
			{
				continue;
			}

			// Output path is placed alongside the directory
			std::filesystem::path outputPath = rDirectoryEntry.path().parent_path() / rDirectoryEntry.path().filename();
			outputPath += "_Prefiltered.R16G16B16A16_SFLOAT";

			// Timestamp dirty check against all 6 face files
			std::vector<std::string> faceNames = bHasJpgFaces
				? std::vector<std::string>{"posx.jpg", "negx.jpg", "posy.jpg", "negy.jpg", "posz.jpg", "negz.jpg"}
				: std::vector<std::string>{"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

			if (std::filesystem::exists(outputPath))
			{
				std::filesystem::file_time_type outputTime = std::filesystem::last_write_time(outputPath);
				bool bDirty = false;
				for (const std::string& rFaceName : faceNames)
				{
					if (std::filesystem::last_write_time(rDirectoryEntry.path() / rFaceName) > outputTime)
					{
						bDirty = true;
						break;
					}
				}
				if (!bDirty)
				{
					continue;
				}
			}

			LOG(kDefault, kDebug, "Generating pre-filtered cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			cmft::Image faceImages[6];
			for (int64_t i = 0; i < 6; ++i)
			{
				std::string facePath = (rDirectoryEntry.path() / faceNames.at(i)).string();
				cmft::imageLoadStb(faceImages[i], facePath.c_str(), cmft::TextureFormat::RGBA32F);
			}

			cmft::Image srcImage;
			cmft::imageCubemapFromFaceList(srcImage, faceImages);

			cmft::Image dstImage;
			cmft::imageRadianceFilter(dstImage, kuiFaceSize, cmft::LightingModel::BlinnBrdf, false, kuiMipCount, 14, 4, srcImage, cmft::EdgeFixup::None, kuiCpuThreads, pClContext);

			convertAndWrite(dstImage, outputPath);

			for (int64_t i = 0; i < 6; ++i)
			{
				cmft::imageUnload(faceImages[i]);
			}
			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}

	cmft::clDestroy(pClContext);
	cmft::clUnload();
}

std::optional<common::ChunkFlags_t> ExportTexture::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (rDirectoryEntry.path().filename().native().find(L"[C]") != std::wstring::npos)
	{
		return common::ChunkFlags_t({common::ChunkFlags::kTexture, common::ChunkFlags::kCubemap});
	}

	std::unordered_set<std::string> extensionSet = {".png", ".tga", ".jpg", ".ktx", ".BC4_UNORM_BLOCK", ".BC5_UNORM_BLOCK", ".BC7_UNORM_BLOCK", ".R8_UNORM", ".R8G8B8A8_UNORM", ".R16_UNORM", ".R16G16_UNORM", ".R32_SFLOAT", ".R16G16B16A16_SFLOAT"};
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
	int64_t iUncompressedSize = 0;
	int64_t iMipWidth = iWidth;
	int64_t iMipHeight = iHeight;
	for (int64_t i = 0; i < iMipLevels; ++i)
	{
		iUncompressedSize += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth /= 2;
		iMipHeight /= 2;
	}
	return iUncompressedSize;
}

void ExportTexture::Export()
{
	VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
	if (mInputPath.native().find(L".R16_UNORM") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_R16_UNORM;
	}
	else if (mInputPath.native().find(L".R32_SFLOAT") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_R32_SFLOAT;
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

	bool bRawTexture = mInputPath.native().find(L".R16_UNORM") != std::wstring::npos || mInputPath.native().find(L".R32_SFLOAT") != std::wstring::npos || mInputPath.native().find(L".BC4_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".BC5_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".BC7_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".R16G16B16A16_SFLOAT") != std::wstring::npos;

	if (mInputPath.native().find(L".ktx") != std::wstring::npos)
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
	else if (bRawTexture)
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
	else if (mChunkFlags & kCubemap)
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
				Texture texture(mInputPath.string().append((*pFaces)[i]).c_str(), FileType::kImage, false);
				iWidth = texture.miWidth;
				iHeight = texture.miHeight;
				texture.Export(data, vkFormat, true);
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
	else
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
		Texture texture(mInputPath, FileType::kImage, false);
		texture.MakeMipmaps(vkFormat, 32, bFontAtlas);
		std::vector<std::byte> data = texture.Export(vkFormat, false);

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
}
