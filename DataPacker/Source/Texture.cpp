#include "Texture.h"

#include <codeanalysis/warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "bc7enc_rdo/rdo_bc_encoder.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

// Aggressive lambda — well above bc7enc_rdo author's typical 0.5–1.0 examples. Full-grid
// sweeps on 2K (Ship_baseColor) + 8K (island Color) showed lambda=4.0 strictly dominates
// lower lambdas on BOTH speed and compression: the per-block RDO search converges sooner at
// higher rate-bias and produces more LZ-friendly output. PSNR drop ~2-3 dB vs lambda=1.0,
// invisible at top-down RTS camera distances on organic terrain / PBR content.
inline constexpr float kfRdoLambdaBc4 = 4.0f;
inline constexpr float kfRdoLambdaBc5 = 4.0f;
inline constexpr float kfRdoLambdaBc7 = 4.0f;
// 1024 B (64 BC7 blocks). ERT's inner loop is O(blocks × window) so this directly caps
// encode time. Far smaller than deflate's 32 KB window, but at lambda=4.0 the speed/size
// Pareto frontier collapses onto the smallest lookback — bigger windows buy proportionally
// less compression for steeply-rising encode time, especially at 8K where they cross the
// L3-cache cliff hard.
inline constexpr uint32_t kuiRdoLookbackWindowSize = 1024;
// bc7enc per-block search depth (default BC7ENC_MAX_UBER_LEVEL=6). 4 produced identical
// output and timing to 6 in the OAT sweep at moderate lambdas; pinned explicitly so future
// bc7enc upstream tuning changes don't silently shift our encoder behavior.
inline constexpr int kiBc7UberLevel = 4;
inline constexpr int kiZlibLevel = Z_BEST_COMPRESSION;

std::mutex Texture::sEncodeMutex;

// Tracks how many threads are inside EncodeWithRdo at once. The caller-held sEncodeMutex must
// keep this at 0 or 1 — anything higher means a call site forgot to take the lock.
static std::atomic<int> sActiveEncodeCount {0};

#pragma warning(push, 0)
#pragma warning(disable : 4201)
#define OPENEXR_EXPORT
#include "openexr/src/lib/OpenEXRCore/openexr.h"
#pragma warning(pop)

#include "stb/stb_image.h"
#include "stb/stb_image_resize2.h"

void Texture::StaticInit()
{
	rgbcx::init();
	bc7enc_compress_block_init();
}

Texture::Texture(const std::filesystem::path& rPath, FileType eFileType, bool bFromGamma, int64_t iWidth, int64_t iHeight)
: miWidth(iWidth)
, miHeight(iHeight)
{
	if (eFileType == FileType::kImage)
	{
		int iStbiWidth = 0;
		int iStbiHeight = 0;
		int iChannelsInFile = 0;
		stbi_uc* pPixels = stbi_load(rPath.string().c_str(), &iStbiWidth, &iStbiHeight, &iChannelsInFile, STBI_rgb_alpha);
		common::ScopedLambda freeStbiPixels([=]()
		{
			stbi_image_free(pPixels);
		});
		ASSERT(iStbiWidth != 0 && iStbiHeight != 0 && pPixels != nullptr);
		miWidth = iStbiWidth;
		miHeight = iStbiHeight;

		stbi_uc* puiSrc = pPixels;
		std::vector<float>& rPixels = mData.emplace_back(4 * iStbiWidth * iStbiHeight);
		float* pfDest = rPixels.data();
		for (int64_t j = 0; j < miHeight; ++j)
		{
			for (int64_t i = 0; i < miWidth; ++i)
			{
				pfDest[0] = puiSrc[0];
				pfDest[1] = puiSrc[1];
				pfDest[2] = puiSrc[2];
				pfDest[3] = puiSrc[3];

				puiSrc += 4;
				pfDest += 4;
			}
		}
	}
	else if (eFileType == FileType::kFloat32)
	{
		ASSERT(miWidth > 0 && miHeight > 0);

		std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
		std::vector<std::byte> data(std::filesystem::file_size(rPath));
		fileStream.read(reinterpret_cast<char*>(data.data()), data.size());
		fileStream.close();

		float* pfSrcR = reinterpret_cast<float*>(data.data());
		std::vector<float>& rPixels = mData.emplace_back(4 * miWidth * miHeight);
		float* pfDest = rPixels.data();
		for (int64_t j = 0; j < miHeight; ++j)
		{
			for (int64_t i = 0; i < miWidth; ++i)
			{
				if (bFromGamma)
				{
					pfDest[0] = 255.0f * common::FromGamma(pfSrcR[0]);
				}
				else
				{
					pfDest[0] = 255.0f * pfSrcR[0];
				}
				pfDest[1] = 0.0f;
				pfDest[2] = 0.0f;
				pfDest[3] = 0.0f;

				++pfSrcR;
				pfDest += 4;
			}
		}
	}
	else if (eFileType == FileType::kUint16Raw)
	{
		// Headerless linear unorm-16. Gaea's UshortRaw16 format. No gamma applies — source is
		// already linear. Single-channel; R is populated, GBA left at 0 (matches kFloat32).
		ASSERT(miWidth > 0 && miHeight > 0);
		ASSERT(!bFromGamma);

		std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
		std::vector<std::byte> data(std::filesystem::file_size(rPath));
		fileStream.read(reinterpret_cast<char*>(data.data()), data.size());
		fileStream.close();

		const uint16_t* puiSrc = reinterpret_cast<const uint16_t*>(data.data());
		std::vector<float>& rPixels = mData.emplace_back(4 * miWidth * miHeight);
		float* pfDest = rPixels.data();
		for (int64_t j = 0; j < miHeight; ++j)
		{
			for (int64_t i = 0; i < miWidth; ++i)
			{
				pfDest[0] = 255.0f * common::UnormToFloat<uint16_t>(puiSrc[0]);
				pfDest[1] = 0.0f;
				pfDest[2] = 0.0f;
				pfDest[3] = 0.0f;

				++puiSrc;
				pfDest += 4;
			}
		}
	}
	else
	{
		exr_context_initializer_t exrContextInitializer = EXR_DEFAULT_CONTEXT_INITIALIZER;
		exr_context_t exrContext {};
		exr_result_t exrResult = exr_start_read(&exrContext, rPath.string().c_str(), &exrContextInitializer);
		ASSERT(exrResult == EXR_ERR_SUCCESS);
		common::ScopedLambda releaseExrContext([=]()
		{
			exr_context_t exrContextCopy = exrContext;
			exr_finish(&exrContextCopy);
		});

		exr_attr_box2i_t dataWindow {};
		exr_get_data_window(exrContext, 0, &dataWindow);
		int32_t iScansPerChunk = 0;
		exr_get_scanlines_per_chunk(exrContext, 0, &iScansPerChunk);
		ASSERT(iScansPerChunk == 1);

		miWidth = dataWindow.max.x + 1;
		miHeight = dataWindow.max.y + 1;
		std::vector<float> pixelsR(miWidth * miHeight);
		std::vector<float> pixelsG(miWidth * miHeight);
		std::vector<float> pixelsB(miWidth * miHeight);

		for (int y = dataWindow.min.y; y <= dataWindow.max.y; y += iScansPerChunk)
		{
			exr_chunk_info_t exrChunkInfo {};
			exr_read_scanline_chunk_info(exrContext, 0, y, &exrChunkInfo);

			exr_decode_pipeline_t decoder {};
			exr_decoding_initialize(exrContext, 0, &exrChunkInfo, &decoder);

			decoder.channels[0].user_data_type = EXR_PIXEL_FLOAT;
			decoder.channels[0].decode_to_ptr = reinterpret_cast<uint8_t*>(pixelsB.data() + y * miWidth);
			decoder.channels[0].user_pixel_stride = 4;
			decoder.channels[0].user_line_stride = static_cast<int32_t>(4 * miWidth);
			decoder.channels[0].user_bytes_per_element = 4;

			decoder.channels[1].user_data_type = EXR_PIXEL_FLOAT;
			decoder.channels[1].decode_to_ptr = reinterpret_cast<uint8_t*>(pixelsG.data() + y * miWidth);
			decoder.channels[1].user_pixel_stride = 4;
			decoder.channels[1].user_line_stride = static_cast<int32_t>(4 * miWidth);
			decoder.channels[1].user_bytes_per_element = 4;

			decoder.channels[2].user_data_type = EXR_PIXEL_FLOAT;
			decoder.channels[2].decode_to_ptr = reinterpret_cast<uint8_t*>(pixelsR.data() + y * miWidth);
			decoder.channels[2].user_pixel_stride = 4;
			decoder.channels[2].user_line_stride = static_cast<int32_t>(4 * miWidth);
			decoder.channels[2].user_bytes_per_element = 4;

			exr_decoding_choose_default_routines(exrContext, 0, &decoder);
			exr_decoding_run(exrContext, 0, &decoder);
			exr_decoding_destroy(exrContext, &decoder);
		}

		float* pfSrcR = pixelsR.data();
		float* pfSrcG = pixelsG.data();
		float* pfSrcB = pixelsB.data();
		std::vector<float>& rPixels = mData.emplace_back(4 * miWidth * miHeight);
		float* pfDest = rPixels.data();
		for (int64_t j = 0; j < miHeight; ++j)
		{
			for (int64_t i = 0; i < miWidth; ++i)
			{
				if (bFromGamma)
				{
					pfDest[0] = 255.0f * common::FromGamma(pfSrcR[0]);
					pfDest[1] = 255.0f * common::FromGamma(pfSrcG[0]);
					pfDest[2] = 255.0f * common::FromGamma(pfSrcB[0]);
				}
				else
				{
					pfDest[0] = 255.0f * pfSrcR[0];
					pfDest[1] = 255.0f * pfSrcG[0];
					pfDest[2] = 255.0f * pfSrcB[0];
				}
				pfDest[3] = 255.0f;

				++pfSrcR;
				++pfSrcG;
				++pfSrcB;
				pfDest += 4;
			}
		}
	}
}

Texture::Texture(const std::byte* puiPixels, int64_t iWidth, int64_t iHeight, int64_t iStride)
: miWidth(iWidth)
, miHeight(iHeight)
{
	std::vector<float>& rPixels = mData.emplace_back(4 * miWidth * miHeight);
	float* pfDest = rPixels.data();
	for (int64_t j = 0; j < miHeight; ++j)
	{
		for (int64_t i = 0; i < miWidth; ++i)
		{
			pfDest[0] = static_cast<float>(std::to_integer<uint8_t>(puiPixels[0]));
			pfDest[1] = static_cast<float>(std::to_integer<uint8_t>(puiPixels[1]));
			pfDest[2] = static_cast<float>(std::to_integer<uint8_t>(puiPixels[2]));
			pfDest[3] = static_cast<float>(std::to_integer<uint8_t>(iStride == 4 ? puiPixels[3] : std::byte {255}));

			puiPixels += iStride;
			pfDest += 4;
		}
	}
}

void Texture::MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, bool bUseBoxFilter, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight)
{
	int64_t iLevel = iPreviousLevel;
	int64_t iSrcWidth = iPreviousWidth;
	int64_t iSrcHeight = iPreviousHeight;

	while (iSrcWidth > 1 && iSrcHeight > 1 && iLevel + 1 < iMaxLevel)
	{
		int64_t iDstWidth = std::max(iSrcWidth / 2, 1ll);
		int64_t iDstHeight = std::max(iSrcHeight / 2, 1ll);

		if (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK || vkFormat == VK_FORMAT_BC5_UNORM_BLOCK || vkFormat == VK_FORMAT_BC7_UNORM_BLOCK)
		{
			if (iDstWidth < 4 || iDstHeight < 4)
			{
				return;
			}

			if ((iDstWidth % 4) != 0 || (iDstHeight % 4) != 0)
			{
				LOG(kDefault, kVerbose, "BC4/BC5/BC7 early out {} x {}", iDstWidth, iDstHeight);
				return;
			}
		}

		mData.emplace_back(4 * iDstWidth * iDstHeight);
		if (bUseBoxFilter)
		{
			stbir_resize(mData.at(iLevel).data(), static_cast<int>(iSrcWidth), static_cast<int>(iSrcHeight), static_cast<int>(4 * iSrcWidth * sizeof(float)), mData.back().data(), static_cast<int>(iDstWidth), static_cast<int>(iDstHeight), static_cast<int>(4 * iDstWidth * sizeof(float)), STBIR_4CHANNEL, STBIR_TYPE_FLOAT, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX);
		}
		else
		{
			stbir_resize_float_linear(mData.at(iLevel).data(), static_cast<int>(iSrcWidth), static_cast<int>(iSrcHeight), static_cast<int>(4 * iSrcWidth * sizeof(float)), mData.back().data(), static_cast<int>(iDstWidth), static_cast<int>(iDstHeight), static_cast<int>(4 * iDstWidth * sizeof(float)), STBIR_4CHANNEL);
		}

		iSrcWidth = iDstWidth;
		iSrcHeight = iDstHeight;
		++iLevel;
	}
}

void Texture::Downsize(int64_t iLevels)
{
	ASSERT(mData.size() == 1);

	// Generate mipmaps in float format to downsample
	MakeMipmaps(VK_FORMAT_R32_SFLOAT, iLevels + 1);

	// Verify even dimensions at each level before erasing
	for (int64_t i = 0; i < iLevels; ++i)
	{
		ASSERT((miWidth >> i) % 2 == 0);
		ASSERT((miHeight >> i) % 2 == 0);
	}
	mData.erase(mData.begin(), mData.begin() + iLevels);
	miWidth >>= iLevels;
	miHeight >>= iLevels;
}

uint32_t Texture::PixelToUint32(const std::vector<float>& rIn, int64_t iWidth, int64_t iX, int64_t iY)
{
	return static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 3)) << 24 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 2)) << 16 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 1)) <<  8 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 0));
}

static utils::image_u8 ToImageU8(const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	utils::image_u8 image(static_cast<uint32_t>(iWidth), static_cast<uint32_t>(iHeight));
	utils::color_quad_u8* pDst = image.get_pixels().data();
	const float* pfSrc = rIn.data();
	int64_t iPixelCount = iWidth * iHeight;
	for (int64_t i = 0; i < iPixelCount; ++i)
	{
		pDst[i].set(static_cast<uint8_t>(std::clamp(pfSrc[0], 0.0f, 255.0f)), static_cast<uint8_t>(std::clamp(pfSrc[1], 0.0f, 255.0f)), static_cast<uint8_t>(std::clamp(pfSrc[2], 0.0f, 255.0f)), static_cast<uint8_t>(std::clamp(pfSrc[3], 0.0f, 255.0f)));
		pfSrc += 4;
	}
	return image;
}

void Texture::EncodeWithRdo(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, VkFormat vkFormat, float fLambda, uint32_t uiLookbackWindowSize, int iBc7UberLevel, bool bVerifyNoAlpha)
{
	// Callers must hold Texture::sEncodeMutex — this function trusts the caller's lock.
	int iActiveEncodes = sActiveEncodeCount.fetch_add(1, std::memory_order_relaxed) + 1;
	common::ScopedLambda decrementActive([]()
	{
		sActiveEncodeCount.fetch_sub(1, std::memory_order_relaxed);
	});
	ASSERT(iActiveEncodes == 1);
	LOG(kDefault, kVerbose, "RDO encode: {}x{} (concurrent={})", iWidth, iHeight, iActiveEncodes);

	rdo_bc::rdo_bc_params params;
	params.m_rdo_lambda = fLambda;
	params.m_rdo_multithreading = true;
	// Use physical-core-count - 2: leaves two physical cores idle (thermal headroom + OS responsiveness)
	params.m_rdo_max_threads = static_cast<int>(std::max<int64_t>(1, common::HardwareCoreCount() - 2));
	params.m_status_output = false;
	params.m_use_bc7e = false;
	params.m_lookback_window_size = uiLookbackWindowSize;
	params.m_custom_lookback_window_size = true;
	params.m_bc7_uber_level = iBc7UberLevel;

	switch (vkFormat)
	{
		case VK_FORMAT_BC4_UNORM_BLOCK:
			params.m_dxgi_format = DXGI_FORMAT_BC4_UNORM;
			break;
		case VK_FORMAT_BC5_UNORM_BLOCK:
			params.m_dxgi_format = DXGI_FORMAT_BC5_UNORM;
			break;
		case VK_FORMAT_BC7_UNORM_BLOCK:
			params.m_dxgi_format = DXGI_FORMAT_BC7_UNORM;
			break;
		default:
			ASSERT(false);
			return;
	}

	utils::image_u8 image = ToImageU8(rIn, iWidth, iHeight);
	rdo_bc::rdo_bc_encoder encoder;
	bool bInit = encoder.init(image, params);
	ASSERT(bInit);
	bool bEncoded = encoder.encode();
	ASSERT(bEncoded);

	std::memcpy(puiOut, encoder.get_blocks(), encoder.get_total_blocks_size_in_bytes());

	if (bVerifyNoAlpha && vkFormat == VK_FORMAT_BC7_UNORM_BLOCK)
	{
		ASSERT(!encoder.get_has_alpha());
	}
}

void Texture::ToBc4(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	EncodeWithRdo(puiOut, rIn, iWidth, iHeight, VK_FORMAT_BC4_UNORM_BLOCK, kfRdoLambdaBc4, kuiRdoLookbackWindowSize, kiBc7UberLevel, false);
}

void Texture::ToBc5(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	EncodeWithRdo(puiOut, rIn, iWidth, iHeight, VK_FORMAT_BC5_UNORM_BLOCK, kfRdoLambdaBc5, kuiRdoLookbackWindowSize, kiBc7UberLevel, false);
}

void Texture::ToBc7(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, bool bVerifyNoAlpha)
{
	EncodeWithRdo(puiOut, rIn, iWidth, iHeight, VK_FORMAT_BC7_UNORM_BLOCK, kfRdoLambdaBc7, kuiRdoLookbackWindowSize, kiBc7UberLevel, bVerifyNoAlpha);
}

void Texture::ToR8G8B8A8(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	for (int64_t j = 0; j < iHeight; ++j)
	{
		for (int64_t i = 0; i < iWidth; ++i)
		{
			reinterpret_cast<uint32_t*>(puiOut)[j * iWidth + i] = PixelToUint32(rIn, iWidth, i, j);
		}
	}
}

void Texture::ToR16(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	for (int64_t j = 0; j < iHeight; ++j)
	{
		for (int64_t i = 0; i < iWidth; ++i)
		{
			float fPixel = rIn.at(4 * (j * iWidth + i)) / 255.0f;

			if (fPixel > 1.0f) [[unlikely]]
			{
				if (fPixel > 1.01f) [[unlikely]]
				{
					LOG(kDefault, kWarning, "{} > 1.01f", fPixel);
				}
				fPixel = 1.0f;
			}
			if (fPixel < 0.0f) [[unlikely]]
			{
				if (fPixel < -0.01f) [[unlikely]]
				{
					LOG(kDefault, kWarning, "{} < -0.01f", fPixel);
				}
				fPixel = 0.0f;
			}

			reinterpret_cast<uint16_t*>(puiOut)[j * iWidth + i] = common::FloatToUnorm<uint16_t>(fPixel);
		}
	}
}

// R32_SFLOAT carries raw float meters (elevation), not RGB color. The kFloat32 / kUint16Raw
// constructors store source values scaled by 255 in the R channel; divide back here so the
// emitted bytes are the original meters (e.g., 50.0m source → 12750.0 internal → 50.0 emitted).
void Texture::ToR32Sfloat(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	for (int64_t j = 0; j < iHeight; ++j)
	{
		for (int64_t i = 0; i < iWidth; ++i)
		{
			reinterpret_cast<float*>(puiOut)[j * iWidth + i] = rIn.at(4 * (j * iWidth + i)) / 255.0f;
		}
	}
}

void Texture::Export(std::vector<std::byte>& rData, VkFormat vkFormat, bool bVerifyNoAlpha)
{
	int64_t iMipWidth = miWidth;
	int64_t iMipHeight = miHeight;
	int64_t iSize = 0;
	for (int64_t i = 0; i < static_cast<int64_t>(mData.size()); ++i)
	{
		iSize += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth /= 2;
		iMipHeight /= 2;
	}
	std::vector<std::byte> data(iSize);

	iMipWidth = miWidth;
	iMipHeight = miHeight;
	std::byte* puiCurrentPosition = data.data();
	for (const std::vector<float>& rMipLevel : mData)
	{
		switch (vkFormat)
		{
			case VK_FORMAT_BC4_UNORM_BLOCK:
				ToBc4(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			case VK_FORMAT_BC5_UNORM_BLOCK:
				ToBc5(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			case VK_FORMAT_BC7_UNORM_BLOCK:
				ToBc7(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight, bVerifyNoAlpha);
				break;

			case VK_FORMAT_R8G8B8A8_UNORM:
				ToR8G8B8A8(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			case VK_FORMAT_R16_UNORM:
				ToR16(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			case VK_FORMAT_R32_SFLOAT:
				ToR32Sfloat(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			default:
				ASSERT(false);
				break;
		}

		puiCurrentPosition += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth /= 2;
		iMipHeight /= 2;
	}

	rData.insert(rData.end(), data.begin(), data.end());
}

void Texture::Save(const std::filesystem::path& rPath, VkFormat vkFormat, bool bVerifyNoAlpha)
{
	std::vector<std::byte> data = Export(vkFormat, bVerifyNoAlpha);

	uLongf uiBound = compressBound(static_cast<uLong>(data.size()));
	std::vector<std::byte> compressed(uiBound);
	uLongf uiCompressedSize = uiBound;
	int iZlibResult = compress2(reinterpret_cast<Bytef*>(compressed.data()), &uiCompressedSize, reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()), kiZlibLevel);
	ASSERT(iZlibResult == Z_OK);

	std::filesystem::remove(rPath);
	std::fstream fileStreamOut(rPath, std::ios::out | std::ios::binary);
	int64_t iMagic = kiTextureIntermediateMagic;
	fileStreamOut.write(reinterpret_cast<const char*>(&iMagic), sizeof(iMagic));
	fileStreamOut.write(reinterpret_cast<const char*>(&miWidth), sizeof(miWidth));
	fileStreamOut.write(reinterpret_cast<const char*>(&miHeight), sizeof(miHeight));
	int64_t iMipMaps = static_cast<int64_t>(mData.size());
	fileStreamOut.write(reinterpret_cast<const char*>(&iMipMaps), sizeof(iMipMaps));
	fileStreamOut.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(uiCompressedSize));
	fileStreamOut.flush();
	fileStreamOut.close();
}
