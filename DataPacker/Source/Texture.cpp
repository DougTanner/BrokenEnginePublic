#include "Texture.h"

#pragma warning(push, 0)
#pragma warning(disable : 6297 26495)
#include "bc7enc_rdo/bc7enc.h"
#include "bc7enc_rdo/rgbcx.h"
#pragma warning(pop)

#pragma warning(push, 0)
#pragma warning(disable : 4201)
#define OPENEXR_EXPORT
#include "openexr/src/lib/OpenEXRCore/openexr.h"
#pragma warning(pop)

#include "stb/stb_image.h"

using namespace DirectX;

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
		miChannels = 4;

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
	else if(eFileType == FileType::kFloat32)
	{
		ASSERT(miWidth > 0 && miHeight > 0);

		std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
		std::vector<byte> data(std::filesystem::file_size(rPath));
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
	else
	{
		exr_context_initializer_t ctxtinit = EXR_DEFAULT_CONTEXT_INITIALIZER;
		exr_context_t f {};
		exr_result_t rv = exr_start_read(&f, rPath.string().c_str(), &ctxtinit);
		ASSERT(rv == EXR_ERR_SUCCESS);
		common::ScopedLambda releaseExrContext([=]()
		{
			exr_context_t exrContextCopy = f;
			exr_finish(&exrContextCopy);
		});

		exr_attr_box2i_t dw {};
		exr_get_data_window(f, 0, &dw);
		int32_t scansperchunk = 0;
		exr_get_scanlines_per_chunk(f, 0, &scansperchunk);
		ASSERT(scansperchunk == 1);

		miWidth = dw.max.x + 1;
		miHeight = dw.max.y + 1;
		std::vector<float> pixelsR(miWidth * miHeight);
		std::vector<float> pixelsG(miWidth * miHeight);
		std::vector<float> pixelsB(miWidth * miHeight);

		for (int y = dw.min.y; y <= dw.max.y; y += scansperchunk)
		{
			exr_chunk_info_t cinfo;
			exr_read_scanline_chunk_info(f, 0, y, &cinfo);

			exr_decode_pipeline_t decoder {};
			exr_decoding_initialize(f, 0, &cinfo, &decoder);

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

			exr_decoding_choose_default_routines(f, 0, &decoder);
			exr_decoding_run(f, 0, &decoder);
			exr_decoding_destroy(f, &decoder);
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
					pfDest[1] = 255.0f * common::FromGamma(pfSrcG[1]);
					pfDest[2] = 255.0f * common::FromGamma(pfSrcB[2]);
				}
				else
				{
					pfDest[0] = 255.0f * pfSrcR[0];
					pfDest[1] = 255.0f * pfSrcG[1];
					pfDest[2] = 255.0f * pfSrcB[2];
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
			pfDest[0] = static_cast<float>(puiPixels[0]);
			pfDest[1] = static_cast<float>(puiPixels[1]);
			pfDest[2] = static_cast<float>(puiPixels[2]);
			pfDest[3] = static_cast<float>(iStride == 4 ? puiPixels[3] : std::byte(255));

			puiPixels += iStride;
			pfDest += 4;
		}
	}
}

void Texture::MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight)
{
	if (iPreviousWidth == 1 || iPreviousHeight == 1 || iPreviousLevel + 1 == iMaxLevel)
	{
		return;
	}

	int64_t iWidth = std::max(iPreviousWidth / 2, 1ll);
	int64_t iHeight = std::max(iPreviousHeight / 2, 1ll);

	if (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK || vkFormat == VK_FORMAT_BC7_UNORM_BLOCK)
	{
		if (iWidth < 4 || iHeight < 4)
		{
			return;
		}

		if ((iWidth % 4) != 0 || (iHeight % 4) != 0)
		{
			LOG("BC4/BC7 early out {} x {}", iWidth, iHeight);
			return;
		}
	}

	std::vector<float>& rPixels = mData.emplace_back(4 * iWidth * iHeight);

	float* pfPreviousPixels = mData.at(iPreviousLevel).data();
	float* pfPixels = rPixels.data();
	for (int64_t j = 0; j < iHeight; ++j)
	{
		for (int64_t i = 0; i < iWidth; ++i)
		{
			int64_t iTopLeft     = (2 * j + 0) * 4 * iPreviousWidth + (2 * i + 0) * 4;
			int64_t iTopRight    = (2 * j + 0) * 4 * iPreviousWidth + (2 * i + 1) * 4;
			int64_t iBottomLeft  = (2 * j + 1) * 4 * iPreviousWidth + (2 * i + 0) * 4;
			int64_t iBottomRight = (2 * j + 1) * 4 * iPreviousWidth + (2 * i + 1) * 4;

			pfPixels[j * 4 * iWidth + 4 * i + 0] = 0.25f * (pfPreviousPixels[iTopLeft + 0] + pfPreviousPixels[iTopRight + 0] + pfPreviousPixels[iBottomLeft + 0] + pfPreviousPixels[iBottomRight + 0]);
			pfPixels[j * 4 * iWidth + 4 * i + 1] = 0.25f * (pfPreviousPixels[iTopLeft + 1] + pfPreviousPixels[iTopRight + 1] + pfPreviousPixels[iBottomLeft + 1] + pfPreviousPixels[iBottomRight + 1]);
			pfPixels[j * 4 * iWidth + 4 * i + 2] = 0.25f * (pfPreviousPixels[iTopLeft + 2] + pfPreviousPixels[iTopRight + 2] + pfPreviousPixels[iBottomLeft + 2] + pfPreviousPixels[iBottomRight + 2]);
			pfPixels[j * 4 * iWidth + 4 * i + 3] = 0.25f * (pfPreviousPixels[iTopLeft + 3] + pfPreviousPixels[iTopRight + 3] + pfPreviousPixels[iBottomLeft + 3] + pfPreviousPixels[iBottomRight + 3]);
		}
	}

	return MakeMipmaps(vkFormat, iMaxLevel, iPreviousLevel + 1, iWidth, iHeight);
}

uint32_t Texture::PixelToUint32(const std::vector<float>& rIn, int64_t iWidth, [[maybe_unused]] int64_t iHeight, int64_t iX, int64_t iY)
{
	return static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 3)) << 24 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 2)) << 16 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 1)) <<  8 |
	       static_cast<uint32_t>(rIn.at(4 * (iY * iWidth + iX) + 0));
}

std::mutex gBc4Mutex;

void Texture::ToBc4(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, int64_t iIndex)
{
	int64_t iCurrentPosition = 0;
	int64_t iBlocksX = iWidth / 4;
	int64_t iBlocksY = iHeight / 4;
	for (int64_t j = 0; j < iBlocksY; ++j)
	{
		for (int64_t i = 0; i < iBlocksX; ++i)
		{
			uint8_t puiBlock[16] {};
			for (int64_t k = 0; k < 4; ++k)
			{
				puiBlock[4 * k + 0] = static_cast<uint8_t>(rIn.at(4 * (j * 4 * iWidth + i * 4 + k * iWidth + 0) + iIndex));
				puiBlock[4 * k + 1] = static_cast<uint8_t>(rIn.at(4 * (j * 4 * iWidth + i * 4 + k * iWidth + 1) + iIndex));
				puiBlock[4 * k + 2] = static_cast<uint8_t>(rIn.at(4 * (j * 4 * iWidth + i * 4 + k * iWidth + 2) + iIndex));
				puiBlock[4 * k + 3] = static_cast<uint8_t>(rIn.at(4 * (j * 4 * iWidth + i * 4 + k * iWidth + 3) + iIndex));
			}

			std::lock_guard lockGuard(gBc4Mutex);
			rgbcx::encode_bc4_hq(&puiOut[iCurrentPosition], puiBlock, 1);
			iCurrentPosition += 8;
		}
	}
}

std::mutex gBc7Mutex;

void Texture::ToBc7(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, bool bVerifyNoAlpha)
{
	bc7enc_compress_block_params bc7encCompressBlockParams {};
	bc7enc_compress_block_params_init(&bc7encCompressBlockParams);

	int64_t iCurrentPosition = 0;
	int64_t iBlocksX = iWidth / 4;
	int64_t iBlocksY = iHeight / 4;
	for (int64_t j = 0; j < iBlocksY; ++j)
	{
		for (int64_t i = 0; i < iBlocksX; ++i)
		{
			uint32_t puiBlock[16] {};
			for (int64_t k = 0; k < 4; ++k)
			{
				puiBlock[4 * k + 0] = PixelToUint32(rIn, iWidth, iHeight, 4 * i + 0, 4 * j + k);
				puiBlock[4 * k + 1] = PixelToUint32(rIn, iWidth, iHeight, 4 * i + 1, 4 * j + k);
				puiBlock[4 * k + 2] = PixelToUint32(rIn, iWidth, iHeight, 4 * i + 2, 4 * j + k);
				puiBlock[4 * k + 3] = PixelToUint32(rIn, iWidth, iHeight, 4 * i + 3, 4 * j + k);
			}

			std::lock_guard lockGuard(gBc7Mutex);
			bool bAlpha = bc7enc_compress_block(&puiOut[iCurrentPosition], puiBlock, &bc7encCompressBlockParams);
			iCurrentPosition += 16;

			if (bVerifyNoAlpha)
			{
				ASSERT(!bAlpha);
			}
		}
	}
}

void Texture::ToR8G8B8A8(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight)
{
	for (int64_t j = 0; j < iHeight; ++j)
	{
		for (int64_t i = 0; i < iWidth; ++i)
		{
			reinterpret_cast<uint32_t*>(puiOut)[j * iWidth + i] = PixelToUint32(rIn, iWidth, iHeight, i, j);
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
					LOG("{} > 1.01f", fPixel);
				}
				fPixel = 1.0f;
			}
			if (fPixel < 0.0f) [[unlikely]]
			{
				if (fPixel < -0.01f) [[unlikely]]
				{
					LOG("{} < -0.01f", fPixel);
				}
				fPixel = 0.0f;
			}

			reinterpret_cast<uint16_t*>(puiOut)[j * iWidth + i] = common::FloatToUnorm<uint16_t>(fPixel);
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

			case VK_FORMAT_BC7_UNORM_BLOCK:
				ToBc7(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight, bVerifyNoAlpha);
				break;

			case VK_FORMAT_R8G8B8A8_UNORM:
				ToR8G8B8A8(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;

			case VK_FORMAT_R16_UNORM:
				ToR16(puiCurrentPosition, rMipLevel, iMipWidth, iMipHeight);
				break;
		}

		puiCurrentPosition += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);;
		iMipWidth /= 2;
		iMipHeight /= 2;
	}

	rData.insert(rData.end(), data.begin(), data.end());
}

void Texture::Save(const std::filesystem::path& rPath, VkFormat vkFormat, bool bVerifyNoAlpha)
{
	std::vector<std::byte> data = Export(vkFormat, bVerifyNoAlpha);

	std::filesystem::remove(rPath);
	std::fstream fileStreamOut(rPath, std::ios::out | std::ios::binary);
	fileStreamOut.write(reinterpret_cast<const char*>(&miWidth), sizeof(miWidth));
	fileStreamOut.write(reinterpret_cast<const char*>(&miHeight), sizeof(miHeight));
	int64_t iMipMaps = static_cast<int64_t>(mData.size());
	fileStreamOut.write(reinterpret_cast<const char*>(&iMipMaps), sizeof(iMipMaps));
	fileStreamOut.write(reinterpret_cast<const char*>(data.data()), data.size());
	fileStreamOut.flush();
	fileStreamOut.close();
}
