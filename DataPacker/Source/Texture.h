#pragma once

enum class FileType
{
	kExr,
	kFloat32,
	kImage,
};

class Texture
{
public:

	static void StaticInit();

	Texture() = delete;
	Texture(const std::filesystem::path& rPath, FileType eFileType, bool bFromGamma, int64_t iWidth = 0, int64_t iHeight = 0);
	Texture(const std::byte* puiPixels, int64_t iWidth, int64_t iHeight, int64_t iStride);

	~Texture() = default;

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel, int64_t iPreviousLevel, int64_t iPreviousWidth, int64_t iPreviousHeight);

	void MakeMipmaps(VkFormat vkFormat, int64_t iMaxLevel = 32)
	{
		ASSERT(mData.size() == 1);
		MakeMipmaps(vkFormat, iMaxLevel, 0, miWidth, miHeight);
	}

	uint32_t PixelToUint32(const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, int64_t iX, int64_t iY);
	void ToBc4(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, int64_t iIndex = 0);
	void ToBc7(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight, bool bVerifyNoAlpha);
	void ToR8G8B8A8(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);
	void ToR16(std::byte* puiOut, const std::vector<float>& rIn, int64_t iWidth, int64_t iHeight);

	void Export(std::vector<std::byte>& rData, VkFormat vkFormat, bool bVerifyNoAlpha);

	std::vector<std::byte> Export(VkFormat vkFormat, bool bVerifyNoAlpha)
	{
		std::vector<std::byte> data;
		Export(data, vkFormat, bVerifyNoAlpha);
		return data;
	}

	void Save(const std::filesystem::path& rPath, VkFormat vkFormat, bool bVerifyNoAlpha);

	int64_t miWidth = 0;
	int64_t miHeight = 0;
	int64_t miChannels = 0;

	std::vector<std::vector<float>> mData;
};
