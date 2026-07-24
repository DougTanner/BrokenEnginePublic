#include "ExportCubemapIbl.h"

#include "FileManager.h"
#include "Texture/Texture.h"

namespace
{

constexpr int64_t kiCubemapIblFingerprintVersion = 1;

struct KtxCubemapData
{
	std::vector<float> floatData;
	uint32_t uiFaceSize = 0;
};

std::filesystem::path GetFingerprintMetadataPath(const std::filesystem::path& rOutputPath, int64_t iInputRoot)
{
	std::filesystem::path metadataPath = gpFileManager->mCacheDirectory / "CubemapIbl" / std::to_string(iInputRoot);
	metadataPath /= std::filesystem::relative(rOutputPath, gpFileManager->mpInputDirectories[iInputRoot]);
	metadataPath += ".meta";
	std::filesystem::create_directories(metadataPath.parent_path());
	return metadataPath;
}

std::filesystem::path GetDirtyMarkerPath(const std::filesystem::path& rMetadataPath)
{
	std::filesystem::path dirtyMarkerPath = rMetadataPath;
	dirtyMarkerPath += ".dirty";
	return dirtyMarkerPath;
}

std::string GetCubemapFingerprint(std::string_view operation, const std::filesystem::path& rSourcePath)
{
	nlohmann::json metadata;
	metadata["version"] = kiCubemapIblFingerprintVersion;
	metadata["operation"] = operation;
	metadata["source"] = gpFileManager->GetFingerprint(rSourcePath);
	return metadata.dump();
}

std::string GetFaceCubemapFingerprint(std::string_view operation, const std::filesystem::path& rSourceDirectory, const char* const* ppFaceNames)
{
	nlohmann::json metadata;
	metadata["version"] = kiCubemapIblFingerprintVersion;
	metadata["operation"] = operation;
	for (int64_t iFace = 0; iFace < 6; ++iFace)
	{
		metadata["faces"][ppFaceNames[iFace]] = gpFileManager->GetFingerprint(rSourceDirectory / ppFaceNames[iFace]);
	}
	return metadata.dump();
}

void WriteFingerprintMetadata(const std::filesystem::path& rMetadataPath, std::string_view fingerprint)
{
	std::filesystem::path temporaryPath = rMetadataPath;
	temporaryPath += ".tmp";
	std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
	stream.write(fingerprint.data(), static_cast<std::streamsize>(fingerprint.size()));
	stream.close();
	VERIFY_SUCCESS(stream.good());
	VERIFY_SUCCESS(MoveFileExW(temporaryPath.native().c_str(), rMetadataPath.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}

bool IsOutputCurrent(const std::filesystem::path& rOutputPath, const std::filesystem::path& rMetadataPath, std::string_view fingerprint, const std::filesystem::path* pLegacyInputs, size_t uiLegacyInputCount)
{
	if (std::filesystem::exists(GetDirtyMarkerPath(rMetadataPath)))
	{
		return false;
	}

	if (!std::filesystem::exists(rOutputPath))
	{
		return false;
	}

	if (std::filesystem::exists(rMetadataPath))
	{
		std::ifstream stream(rMetadataPath, std::ios::binary);
		std::string cachedFingerprint {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
		return !stream.bad() && cachedFingerprint == fingerprint;
	}

	std::filesystem::file_time_type outputTime = std::filesystem::last_write_time(rOutputPath);
	for (size_t i = 0; i < uiLegacyInputCount; ++i)
	{
		if (std::filesystem::last_write_time(pLegacyInputs[i]) > outputTime)
		{
			return false;
		}
	}
	WriteFingerprintMetadata(rMetadataPath, fingerprint);
	return true;
}

void BeginOutputUpdate(const std::filesystem::path& rMetadataPath)
{
	std::ofstream stream(GetDirtyMarkerPath(rMetadataPath), std::ios::binary | std::ios::trunc);
	stream.close();
	VERIFY_SUCCESS(stream.good());
	std::filesystem::remove(rMetadataPath);
}

void CompleteOutputUpdate(const std::filesystem::path& rMetadataPath, std::string_view fingerprint)
{
	WriteFingerprintMetadata(rMetadataPath, fingerprint);
	VERIFY_SUCCESS(std::filesystem::remove(GetDirtyMarkerPath(rMetadataPath)));
}

} // namespace

static KtxCubemapData LoadKtxCubemapAsFloat(const std::filesystem::path& rPath)
{
	KtxCubemapData result;
	gli::texture texture = LoadGliFromPath(rPath);
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
		DirectX::PackedVector::XMConvertHalfToFloatStream(pDstFloat, sizeof(float), pSrcHalf, sizeof(uint16_t), uiPixelsPerFace * 4);
	}
	return result;
}

void GenerateIrradianceCubemaps()
{
	for (int64_t iInputRoot = 0; iInputRoot < static_cast<int64_t>(std::size(gpFileManager->mpInputDirectories)); ++iInputRoot)
	{
		const std::filesystem::path& rBaseDirectory = gpFileManager->mpInputDirectories[iInputRoot];
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rDirectoryEntry.path().extension() != ".ktx")
			{
				continue;
			}

			if (!HasCubemapTag(rDirectoryEntry.path()))
			{
				continue;
			}

			// Build output path: [C]<name>_Irradiance<R16G16B16A16_SFLOAT suffix>
			std::filesystem::path outputPath = rDirectoryEntry.path().parent_path() / rDirectoryEntry.path().stem();
			outputPath += "_Irradiance";
			outputPath += TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT);

			std::string fingerprint = GetCubemapFingerprint("irradiance", rDirectoryEntry.path());
			std::filesystem::path metadataPath = GetFingerprintMetadataPath(outputPath, iInputRoot);
			std::filesystem::path legacyInput = rDirectoryEntry.path();
			if (IsOutputCurrent(outputPath, metadataPath, fingerprint, &legacyInput, 1))
			{
				continue;
			}
			BeginOutputUpdate(metadataPath);

			LOG(kDefault, kDebug, "Generating irradiance cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			KtxCubemapData cubemapData = LoadKtxCubemapAsFloat(rDirectoryEntry.path());
			uint32_t uiFaceSize = cubemapData.uiFaceSize;
			uint32_t uiPixelsPerFace = uiFaceSize * uiFaceSize;
			uint32_t uiTotalPixels = uiPixelsPerFace * 6;

			// Create CMFT source image and populate with float data
			cmft::Image srcImage;
			cmft::imageCreate(srcImage, uiFaceSize, uiFaceSize, 0x000000ff, 1, 6, cmft::TextureFormat::RGBA32F);
			std::memcpy(srcImage.m_data, cubemapData.floatData.data(), uiTotalPixels * 4 * sizeof(float));

			// Generate 128x128 irradiance cubemap using spherical harmonics.
			// Cross-machine note: unlike the radiance filter, this SH path is serial CPU (no OpenCL, no
			// thread-count input), so it carries only generic FP-environment exposure (FMA contraction in
			// the double-precision SH reduction; DataPacker is not /fp:strict) - low cross-host variance risk.
			static constexpr uint32_t kuiIrradianceFaceSize = 128;
			cmft::Image dstImage;
			cmft::imageIrradianceFilterSh(dstImage, kuiIrradianceFaceSize, srcImage);

			// Convert RGBA32F result back to RGBA16F
			uint32_t uiIrradiancePixelsPerFace = kuiIrradianceFaceSize * kuiIrradianceFaceSize;
			uint32_t uiIrradianceTotalPixels = uiIrradiancePixelsPerFace * 6;
			std::vector<uint16_t> halfData(uiIrradianceTotalPixels * 4);

			const float* pSrcFloat = static_cast<const float*>(dstImage.m_data);
			DirectX::PackedVector::XMConvertFloatToHalfStream(halfData.data(), sizeof(uint16_t), pSrcFloat, sizeof(float), uiIrradianceTotalPixels * 4);

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
			VERIFY_SUCCESS(fileStream.good());
			CompleteOutputUpdate(metadataPath, fingerprint);

			// Clean up CMFT images
			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}
}

static constexpr uint32_t kuiPreFilteredFaceSize = 1024;
static constexpr uint8_t kuiPreFilteredMipCount = 11; // log2(1024) + 1

// Packs a CMFT radiance-filtered cubemap into face-major / mip-minor half-floats (matching the engine's
// TextureUploadManager iteration order) and writes the [width][height][mipcount][pixels] intermediate.
static void WriteFilteredCubemap(cmft::Image& rDstImage, const std::filesystem::path& rOutputPath)
{
	// Get per-face/per-mip byte offsets in CMFT output
	uint32_t offsets[CUBE_FACE_NUM][MAX_MIP_NUM] {};
	cmft::imageGetMipOffsets(offsets, rDstImage);

	// Calculate total half-float pixel count across all faces and mips
	uint32_t uiTotalHalfFloats = 0;
	uint32_t uiMipSize = kuiPreFilteredFaceSize;
	for (uint8_t uiMip = 0; uiMip < kuiPreFilteredMipCount; ++uiMip, uiMipSize /= 2)
	{
		uiTotalHalfFloats += uiMipSize * uiMipSize * 4;
	}
	uiTotalHalfFloats *= 6; // 6 faces

	std::vector<uint16_t> halfData(uiTotalHalfFloats);
	uint32_t uiHalfOffset = 0;

	// Write in face-major/mip-minor order to match engine's TextureUploadManager iteration order
	for (int64_t iFace = 0; iFace < 6; ++iFace)
	{
		uiMipSize = kuiPreFilteredFaceSize;
		for (uint8_t uiMip = 0; uiMip < kuiPreFilteredMipCount; ++uiMip, uiMipSize /= 2)
		{
			uint32_t uiMipPixels = uiMipSize * uiMipSize;
			const float* pSrcFloat = reinterpret_cast<const float*>(static_cast<uint8_t*>(rDstImage.m_data) + offsets[iFace][uiMip]);
			DirectX::PackedVector::XMConvertFloatToHalfStream(halfData.data() + uiHalfOffset, sizeof(uint16_t), pSrcFloat, sizeof(float), uiMipPixels * 4);
			uiHalfOffset += uiMipPixels * 4;
		}
	}

	// Write intermediate file: [width][height][mipcount][pixel data]
	int64_t iWidth = kuiPreFilteredFaceSize;
	int64_t iHeight = kuiPreFilteredFaceSize;
	int64_t iMipCount = kuiPreFilteredMipCount;

	std::fstream fileStream(rOutputPath, std::ios::out | std::ios::binary);
	fileStream.write(reinterpret_cast<const char*>(&iWidth), sizeof(iWidth));
	fileStream.write(reinterpret_cast<const char*>(&iHeight), sizeof(iHeight));
	fileStream.write(reinterpret_cast<const char*>(&iMipCount), sizeof(iMipCount));
	fileStream.write(reinterpret_cast<const char*>(halfData.data()), halfData.size() * sizeof(uint16_t));
	fileStream.flush();
	fileStream.close();
	VERIFY_SUCCESS(fileStream.good());
}

// Radiance-filters every [C]-tagged .ktx cubemap that is out of date and writes the pre-filtered
// intermediate beside the source.
static void ProcessKtxCubemaps(uint8_t uiCpuThreads, cmft::ClContext* pClContext)
{
	for (int64_t iInputRoot = 0; iInputRoot < static_cast<int64_t>(std::size(gpFileManager->mpInputDirectories)); ++iInputRoot)
	{
		const std::filesystem::path& rBaseDirectory = gpFileManager->mpInputDirectories[iInputRoot];
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rDirectoryEntry.path().extension() != ".ktx")
			{
				continue;
			}

			if (!HasCubemapTag(rDirectoryEntry.path()))
			{
				continue;
			}

			std::filesystem::path outputPath = rDirectoryEntry.path().parent_path() / rDirectoryEntry.path().stem();
			outputPath += "_Prefiltered";
			outputPath += TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT);

			std::string fingerprint = GetCubemapFingerprint("prefiltered", rDirectoryEntry.path());
			std::filesystem::path metadataPath = GetFingerprintMetadataPath(outputPath, iInputRoot);
			std::filesystem::path legacyInput = rDirectoryEntry.path();
			if (IsOutputCurrent(outputPath, metadataPath, fingerprint, &legacyInput, 1))
			{
				continue;
			}
			BeginOutputUpdate(metadataPath);

			LOG(kDefault, kDebug, "Generating pre-filtered cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			KtxCubemapData cubemapData = LoadKtxCubemapAsFloat(rDirectoryEntry.path());
			uint32_t uiFaceSize = cubemapData.uiFaceSize;
			uint32_t uiPixelsPerFace = uiFaceSize * uiFaceSize;
			uint32_t uiTotalPixels = uiPixelsPerFace * 6;

			cmft::Image srcImage;
			cmft::imageCreate(srcImage, uiFaceSize, uiFaceSize, 0x000000ff, 1, 6, cmft::TextureFormat::RGBA32F);
			std::memcpy(srcImage.m_data, cubemapData.floatData.data(), uiTotalPixels * 4 * sizeof(float));

			cmft::Image dstImage;
			cmft::imageRadianceFilter(dstImage, kuiPreFilteredFaceSize, cmft::LightingModel::BlinnBrdf, false, kuiPreFilteredMipCount, 14, 4, srcImage, cmft::EdgeFixup::None, uiCpuThreads, pClContext);

			WriteFilteredCubemap(dstImage, outputPath);
			CompleteOutputUpdate(metadataPath, fingerprint);

			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}
}

// Radiance-filters every [C]-tagged directory of six cube-face images (posx/negx/... .jpg or px/nx/... .png)
// that is out of date and writes the pre-filtered intermediate beside the directory.
static void ProcessFaceImageCubemaps(uint8_t uiCpuThreads, cmft::ClContext* pClContext)
{
	for (int64_t iInputRoot = 0; iInputRoot < static_cast<int64_t>(std::size(gpFileManager->mpInputDirectories)); ++iInputRoot)
	{
		const std::filesystem::path& rBaseDirectory = gpFileManager->mpInputDirectories[iInputRoot];
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (!rDirectoryEntry.is_directory())
			{
				continue;
			}

			if (!HasCubemapTag(rDirectoryEntry.path()))
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
			outputPath += "_Prefiltered";
			outputPath += TextureIntermediateSuffix(VK_FORMAT_R16G16B16A16_SFLOAT);

			static constexpr const char* kpcJpgFaceNames[6] =
			{
				"posx.jpg",
				"negx.jpg",
				"posy.jpg",
				"negy.jpg",
				"posz.jpg",
				"negz.jpg",
			};
			static constexpr const char* kpcPngFaceNames[6] =
			{
				"px.png",
				"nx.png",
				"py.png",
				"ny.png",
				"pz.png",
				"nz.png",
			};
			const char* const* pFaceNames = bHasJpgFaces ? kpcJpgFaceNames : kpcPngFaceNames;
			std::filesystem::path legacyInputs[6];
			for (int64_t iFace = 0; iFace < 6; ++iFace)
			{
				legacyInputs[iFace] = rDirectoryEntry.path() / pFaceNames[iFace];
			}
			std::string fingerprint = GetFaceCubemapFingerprint("prefiltered", rDirectoryEntry.path(), pFaceNames);
			std::filesystem::path metadataPath = GetFingerprintMetadataPath(outputPath, iInputRoot);
			if (IsOutputCurrent(outputPath, metadataPath, fingerprint, legacyInputs, std::size(legacyInputs)))
			{
				continue;
			}
			BeginOutputUpdate(metadataPath);

			LOG(kDefault, kDebug, "Generating pre-filtered cubemap for \"{}\"", rDirectoryEntry.path().filename().string());

			cmft::Image faceImages[6];
			for (int64_t i = 0; i < 6; ++i)
			{
				// stb link-resolves to the shared first-party STBI_WINDOWS_UTF8 build (cmft's vendored stb is not compiled), so imageLoadStb decodes this UTF-8 path correctly.
				std::u8string facePath = (rDirectoryEntry.path() / pFaceNames[i]).u8string();
				cmft::imageLoadStb(faceImages[i], reinterpret_cast<const char*>(facePath.c_str()), cmft::TextureFormat::RGBA32F);
			}

			cmft::Image srcImage;
			cmft::imageCubemapFromFaceList(srcImage, faceImages);

			cmft::Image dstImage;
			cmft::imageRadianceFilter(dstImage, kuiPreFilteredFaceSize, cmft::LightingModel::BlinnBrdf, false, kuiPreFilteredMipCount, 14, 4, srcImage, cmft::EdgeFixup::None, uiCpuThreads, pClContext);

			WriteFilteredCubemap(dstImage, outputPath);
			CompleteOutputUpdate(metadataPath, fingerprint);

			for (int64_t i = 0; i < 6; ++i)
			{
				cmft::imageUnload(faceImages[i]);
			}
			cmft::imageUnload(srcImage);
			cmft::imageUnload(dstImage);
		}
	}
}

void GeneratePreFilteredCubemaps()
{
	static const uint8_t kuiCpuThreads = static_cast<uint8_t>(std::max(1u, std::thread::hardware_concurrency()));

	// Cross-machine note: radiance convolution runs on whatever OpenCL GPU is present (below), so the
	// pre-filtered half-float output is GPU/driver-dependent; the CPU fallback (kuiCpuThreads above) is
	// thread-count-dependent. Either way the .R16G16B16A16_SFLOAT radiance intermediates are reproducible
	// only per bake host. Acceptable under the single-canonical-bake-machine assumption;
	// force a pinned-thread CPU path if CI / multi-machine
	// bakes are introduced.
	cmft::ClContext* pClContext = nullptr;
	if (cmft::clLoad() != 0)
	{
		pClContext = cmft::clInit(CMFT_CL_VENDOR_ANY_GPU, CMFT_CL_DEVICE_TYPE_GPU);
	}

	ProcessKtxCubemaps(kuiCpuThreads, pClContext);
	ProcessFaceImageCubemaps(kuiCpuThreads, pClContext);

	cmft::clDestroy(pClContext);
	cmft::clUnload();
}
