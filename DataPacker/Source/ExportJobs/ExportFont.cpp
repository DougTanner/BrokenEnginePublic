#include "ExportFont.h"

using enum common::ChunkFlags;

#pragma pack(push)
#pragma pack(1)

// https://www.angelcode.com/products/bmfont/doc/file_format.html
struct CommonBlock
{
    unsigned short lineHeight;
    unsigned short base;
    unsigned short scaleW;
    unsigned short scaleH;
    unsigned short pages;
    unsigned char packed:1;
    unsigned char reserved:7;
	unsigned char alphaChnl;
	unsigned char redChnl;
	unsigned char greenChnl;
	unsigned char blueChnl;
};

struct CharInfo
{
    DWORD id;
    unsigned short x;
    unsigned short y;
    unsigned short width;
    unsigned short height;
    short xoffset;
    short yoffset;
    short xadvance;
    unsigned char page;
    unsigned char chnl;
};

struct KerningPair
{
    DWORD first;
    DWORD second;
    short amount;
};

#pragma pack(pop)

void ExportFont::Export()
{
	int64_t iFntBytes = std::filesystem::file_size(mInputPath);
	std::vector<byte> fntData(iFntBytes);
	std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(fntData.data()), fntData.size());
	ASSERT(fntData[0] == 'B' && fntData[1] == 'M' && fntData[2] == 'F' && fntData[3] == 3);

	common::FontHeader fontHeader {};
	std::vector<uint32_t> ids;
	std::vector<common::Character> characters;

	int64_t iPos = 4;
	while (iPos < iFntBytes)
	{
		int64_t iBlockType = fntData[iPos++];
		int64_t iBlockSize = *reinterpret_cast<int*>(&fntData.at(iPos));
		iPos += 4;
		LOG("Block {} {}", iBlockType, iBlockSize);

		switch (iBlockType)
		{
			case 1:
			{
				// We don't need anything from info block
				break;
			}
			case 2:
			{
				CommonBlock& rCommonBlock = *reinterpret_cast<CommonBlock*>(&fntData[iPos]);
				LOG("  CommonBlock {} {} {} {} {} {}", rCommonBlock.lineHeight, rCommonBlock.base, rCommonBlock.scaleW, rCommonBlock.scaleH, rCommonBlock.pages, rCommonBlock.packed);
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
				std::span<CharInfo> pCharInfos(reinterpret_cast<CharInfo*>(&fntData[iPos]), iBlockSize / sizeof(CharInfo));
				for (const CharInfo& rCharInfo : pCharInfos)
				{
					// LOG("  CharInfo {} {} {} {} {} {} {} {}", rCharInfo.id, rCharInfo.x, rCharInfo.y, rCharInfo.width, rCharInfo.height, rCharInfo.xoffset, rCharInfo.yoffset, rCharInfo.xadvance);
					ids.emplace_back(rCharInfo.id);
					characters.emplace_back(common::Character {rCharInfo.x, rCharInfo.y, rCharInfo.width, rCharInfo.height, rCharInfo.xoffset, rCharInfo.yoffset, rCharInfo.xadvance});
				}
				break;
			}
			case 5:
			{
				std::span<KerningPair> pKerningPairs(reinterpret_cast<KerningPair*>(&fntData[iPos]), iBlockSize / sizeof(KerningPair));
				for ([[maybe_unused]] const KerningPair& rKerningPair : pKerningPairs)
				{
					// LOG("  KerningPair {} {} {}", rKerningPair.first, rKerningPair.second, rKerningPair.amount);
				}
				break;
			}
			default:
				ASSERT(false);
		}

		iPos += iBlockSize;
	}

	int64_t iIdsBytes = ids.size() * sizeof(ids[0]);
	int64_t iDataSize = common::RoundUp(iIdsBytes, common::kiAlignmentBytes);
	int64_t iCharactersBytes = characters.size() * sizeof(characters[0]);
	iDataSize += iCharactersBytes;
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iDataSize);

	fontHeader.iCharacters = characters.size();
	fontHeader.iKerningPairs = 0;
	pHeader->fontHeader = fontHeader;

	memcpy(dataSpan.data(), ids.data(), iIdsBytes);
	memcpy(&dataSpan[common::RoundUp(iIdsBytes, common::kiAlignmentBytes)], characters.data(), iCharactersBytes);
}
