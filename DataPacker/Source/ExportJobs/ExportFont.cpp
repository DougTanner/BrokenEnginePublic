#include "ExportFont.h"

using enum common::ChunkFlags;

#pragma pack(push)
#pragma pack(1)

// https://www.angelcode.com/products/bmfont/doc/file_format.html
struct CommonBlock
{
	uint16_t lineHeight;
	uint16_t base;
	uint16_t scaleW;
	uint16_t scaleH;
	uint16_t pages;
	uint8_t packed:1;
	uint8_t reserved:7;
	uint8_t alphaChnl;
	uint8_t redChnl;
	uint8_t greenChnl;
	uint8_t blueChnl;
};

struct CharInfo
{
	DWORD id;
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
	int16_t xoffset;
	int16_t yoffset;
	int16_t xadvance;
	uint8_t page;
	uint8_t chnl;
};

struct KerningPair
{
	DWORD first;
	DWORD second;
	short amount;
};

#pragma pack(pop)

std::optional<common::ChunkFlags_t> ExportFont::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".fnt" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kFont) : std::nullopt;
}

void ExportFont::Export()
{
	int64_t iFntBytes = std::filesystem::file_size(mInputPath);
	std::vector<std::byte> fntData(iFntBytes);
	std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(fntData.data()), fntData.size());
	ASSERT(fntData.at(0) == std::byte{'B'} && fntData.at(1) == std::byte{'M'} && fntData.at(2) == std::byte{'F'} && fntData.at(3) == std::byte{3});

	common::FontHeader fontHeader {};
	std::vector<uint32_t> ids;
	std::vector<common::Character> characters;

	int64_t iPos = 4;
	while (iPos < iFntBytes)
	{
		int64_t iBlockType = std::to_integer<int64_t>(fntData.at(iPos++));
		int64_t iBlockSize = *reinterpret_cast<int*>(&fntData.at(iPos));
		iPos += 4;
		Log(kVerbose, "Block {} {}", iBlockType, iBlockSize);

		switch (iBlockType)
		{
			case 1:
			{
				// We don't need anything from info block
				break;
			}
			case 2:
			{
				CommonBlock& rCommonBlock = *reinterpret_cast<CommonBlock*>(&fntData.at(iPos));
				Log(kVerbose, "  CommonBlock {} {} {} {} {} {}", rCommonBlock.lineHeight, rCommonBlock.base, rCommonBlock.scaleW, rCommonBlock.scaleH, rCommonBlock.pages, rCommonBlock.packed);
				fontHeader.iLineHeight = rCommonBlock.lineHeight;
				fontHeader.iBase = rCommonBlock.base;
				fontHeader.iScaleW = rCommonBlock.scaleW;
				fontHeader.iScaleH = rCommonBlock.scaleH;
				break;
			}
			case 3:
			{
				// We don't need anything from pages block
				break;
			}
			case 4:
			{
				std::span<CharInfo> pCharInfos(reinterpret_cast<CharInfo*>(&fntData.at(iPos)), iBlockSize / sizeof(CharInfo));
				for (const CharInfo& rCharInfo : pCharInfos)
				{
					// Log("  CharInfo {} {} {} {} {} {} {} {}", rCharInfo.id, rCharInfo.x, rCharInfo.y, rCharInfo.width, rCharInfo.height, rCharInfo.xoffset, rCharInfo.yoffset, rCharInfo.xadvance);
					ids.emplace_back(rCharInfo.id);
					characters.emplace_back(common::Character {rCharInfo.x, rCharInfo.y, rCharInfo.width, rCharInfo.height, rCharInfo.xoffset, rCharInfo.yoffset, rCharInfo.xadvance});
				}
				break;
			}
			case 5:
			{
				std::span<KerningPair> pKerningPairs(reinterpret_cast<KerningPair*>(&fntData.at(iPos)), iBlockSize / sizeof(KerningPair));
				for ([[maybe_unused]] const KerningPair& rKerningPair : pKerningPairs)
				{
					// Log("  KerningPair {} {} {}", rKerningPair.first, rKerningPair.second, rKerningPair.amount);
				}
				break;
			}
			default:
				ASSERT(false);
		}

		iPos += iBlockSize;
	}

	int64_t iIdsBytes = ids.size() * sizeof(ids.at(0));
	int64_t iDataSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(iIdsBytes);
	int64_t iCharactersBytes = characters.size() * sizeof(characters.at(0));
	iDataSize += iCharactersBytes;
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iDataSize);

	fontHeader.iCharacters = characters.size();
	fontHeader.iKerningPairs = 0;
	pHeader->fontHeader = fontHeader;

	std::memcpy(dataSpan.data(), ids.data(), iIdsBytes);
	std::memcpy(&dataSpan[common::RoundUp<int64_t, common::kiAlignmentBytes>(iIdsBytes)], characters.data(), iCharactersBytes);
}
