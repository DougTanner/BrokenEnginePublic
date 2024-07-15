#include "ExportTexture.h"

#include "Texture.h"

#pragma warning(push, 0)
#pragma warning(disable : 4146 4701 4702 4706 6001 6011 6262 6308 6330 6386 6387 26051 26408 26409 26429 26432 26433 26434 26435 26438 26440 26443 26444 26447 26448 26451 26455 26456 26459 26460 26461 26466 26472 26475 26477 26481 26482 26485 26488 26498 26490 26493 26494 26495 26496 26497 26812 26814 26818 26819 28182 28020)
#include "gli/gli/gli.hpp"
#pragma warning(pop)

using enum common::ChunkFlags;

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
	else if (mChunkFlags & kCubemap || mInputPath.native().find(L"[BC7]") != std::wstring::npos || mInputPath.native().find(L".BC7_UNORM_BLOCK") != std::wstring::npos)
	{
		vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
	}

	bool bRawTexture = mInputPath.native().find(L".R16_UNORM") != std::wstring::npos || mInputPath.native().find(L".BC4_UNORM_BLOCK") != std::wstring::npos || mInputPath.native().find(L".BC7_UNORM_BLOCK") != std::wstring::npos;

	if (mInputPath.native().find(L".ktx") != std::wstring::npos)
	{
		gli::texture texture = gli::load(mInputPath.string());
		ASSERT(!texture.empty() && texture.target() == gli::TARGET_CUBE);

		gli::texture_cube textureCube(texture);

		auto [pHeader, dataSpan] = AllocateHeaderAndData(textureCube.size());
		pHeader->textureHeader.iTextureWidth = textureCube[0].extent().x;
		pHeader->textureHeader.iTextureHeight = textureCube[0].extent().y;
		pHeader->textureHeader.iMipLevels = textureCube.levels();
		ASSERT(textureCube.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16);
		pHeader->textureHeader.vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		memcpy(dataSpan.data(), textureCube.data(), dataSpan.size());

	}
	else if (mChunkFlags & kCubemap)
	{
		int64_t iWidth = 0;
		int64_t iHeight = 0;
		std::vector<std::byte> data;

		std::vector<std::string> facesOne = {"\\px.png", "\\nx.png", "\\py.png", "\\ny.png", "\\pz.png", "\\nz.png"};
		std::vector<std::string> facesTwo = {"\\posx.jpg", "\\negx.jpg", "\\posy.jpg", "\\negy.jpg", "\\posz.jpg", "\\negz.jpg"};
		std::vector<std::string>* pFaces = std::filesystem::exists(mInputPath.string().append(facesOne[0]).c_str()) ? &facesOne : &facesTwo;
		for (int64_t i = 0; i < 6; ++i)
		{
			Texture texture(mInputPath.string().append((*pFaces)[i]).c_str(), FileType::kImage, false);
			iWidth = texture.miWidth;
			iHeight = texture.miHeight;
			texture.Export(data, vkFormat, true);
		}

		auto [pHeader, dataSpan] = AllocateHeaderAndData(data.size());
		pHeader->textureHeader.iTextureWidth = iWidth;
		pHeader->textureHeader.iTextureHeight = iHeight;
		pHeader->textureHeader.iMipLevels = 1;
		pHeader->textureHeader.vkFormat = vkFormat;
		memcpy(dataSpan.data(), data.data(), dataSpan.size());
	}
	else if (bRawTexture)
	{
		decltype(Texture::miWidth) iWidth = 0;
		decltype(Texture::miHeight) iHeight = 0;
		int64_t iMipMaps = 1;

		std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
		std::vector<byte> data(std::filesystem::file_size(mInputPath) - sizeof(Texture::miWidth) - sizeof(Texture::miHeight));
		fileStream.read(reinterpret_cast<char*>(&iWidth), sizeof(iWidth));
		fileStream.read(reinterpret_cast<char*>(&iHeight), sizeof(iHeight));
		fileStream.read(reinterpret_cast<char*>(&iMipMaps), sizeof(iMipMaps));
		fileStream.read(reinterpret_cast<char*>(data.data()), data.size());
		fileStream.close();

		auto [pHeader, dataSpan] = AllocateHeaderAndData(data.size());
		pHeader->textureHeader.iTextureWidth = iWidth;
		pHeader->textureHeader.iTextureHeight = iHeight;
		pHeader->textureHeader.iMipLevels = iMipMaps;
		pHeader->textureHeader.vkFormat = vkFormat;

		memcpy(dataSpan.data(), data.data(), data.size());
	}
	else
	{
		Texture texture(mInputPath, FileType::kImage, false);
		texture.MakeMipmaps(vkFormat);
		std::vector<std::byte> data = texture.Export(vkFormat, false);

		auto [pHeader, dataSpan] = AllocateHeaderAndData(data.size());
		pHeader->textureHeader.iTextureWidth = texture.miWidth;
		pHeader->textureHeader.iTextureHeight = texture.miHeight;
		pHeader->textureHeader.iMipLevels = texture.mData.size();
		pHeader->textureHeader.vkFormat = vkFormat;
		memcpy(dataSpan.data(), data.data(), dataSpan.size());
	}
}
