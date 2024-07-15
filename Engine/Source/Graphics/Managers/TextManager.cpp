#include "TextManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

static VkMappedMemoryRange sVkMappedMemoryRange
{
	.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
	.pNext = nullptr,
	// .memory
	.offset = 0,
	// .size
};

TextManager::TextManager()
{
	gpTextManager = this;

	SCOPED_BOOT_TIMER(kBootTimerTextManager);

	std::setlocale(LC_ALL, "en_US.utf8");
	for (int64_t i = 0; i < game::kStringsCount; ++i)
	{
		for (int64_t j = 0; j < game::kLanguageCount; ++j)
		{
			int64_t k = 0;
			while (game::gppTranslatedStrings[i][j][k] != 0)
			{
				game::gppTranslatedStrings[i][j][k] = towupper((wint_t)game::gppTranslatedStrings[i][j][k]);
				++k;
			}			
		}
	}

	auto& rChunkMap = gpFileManager->GetDataChunkMap();

	{
		Chunk& rChunk = rChunkMap.at(data::kFontsNotoSansNotoSansRegularfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;
		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::RoundUp(iCharacters * static_cast<int64_t>(sizeof(pCharacterIds[0])), common::kiAlignmentBytes));
		LOG("Loading font {:#018x} with {} characters", data::kFontsNotoSansNotoSansRegularfntCrc, iCharacters);
		mfLineHeightEfigs = static_cast<float>(rChunk.pHeader->fontHeader.iLineHeight);

		for (int64_t i = 0; i < iCharacters; ++i)
		{
			auto [it, bInserted] = mCharacterMapEfigs.try_emplace(pCharacterIds[i], &pCharacters[i]);
			ASSERT(bInserted);
		}
	}

	{
		Chunk& rChunk = rChunkMap.at(data::kFontsNotoSansSCNotoSansSCLightfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;
		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::RoundUp(iCharacters * static_cast<int64_t>(sizeof(pCharacterIds[0])), common::kiAlignmentBytes));
		LOG("Loading font {:#018x} with {} characters", data::kFontsNotoSansSCNotoSansSCLightfntCrc, iCharacters);
		mfLineHeightChinese = static_cast<float>(rChunk.pHeader->fontHeader.iLineHeight);

		for (int64_t i = 0; i < iCharacters; ++i)
		{
			auto [it, bInserted] = mCharacterMapChinese.try_emplace(pCharacterIds[i], &pCharacters[i]);
			ASSERT(bInserted);
		}
	}

	gpTextManager->UpdateTextArea(kTextDebug, "");
}

TextManager::~TextManager()
{
	gpTextManager = nullptr;
}

std::tuple<common::Character*, bool> TextManager::GetCharacter(uint32_t uiChar)
{
	if (mCharacterMapEfigs.find(uiChar) != mCharacterMapEfigs.end())
	{
		return std::make_tuple(mCharacterMapEfigs.at(uiChar), true);
	}
	else if (mCharacterMapChinese.find(uiChar) != mCharacterMapChinese.end())
	{
		return std::make_tuple(mCharacterMapChinese.at(uiChar), false);
	}
	else
	{
		DEBUG_BREAK();
		return std::make_tuple(mCharacterMapEfigs.begin()->second, true);
	}
}

void TextManager::UpdateTextArea(TextAreas eTextArea, std::string_view pcCharacters)
{
	ASSERT(pcCharacters.size() < TextArea::kiMaxChars);

	TextArea& rTextArea = gpTextAreas[eTextArea];
	rTextArea.iCharacterCount = std::min(static_cast<int64_t>(pcCharacters.size()), TextArea::kiMaxChars);
	memcpy(rTextArea.pcText, pcCharacters.data(), rTextArea.iCharacterCount);
}

void TextManager::RenderMain(int64_t iCommandBuffer)
{
	auto pQuads = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mTextStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iPos = 0;

	for (const TextArea& rTextArea : gpTextAreas)
	{
		std::vector<float> xOffsets;
		xOffsets.push_back(rTextArea.fX);
		WriteQuads(xOffsets, rTextArea.fY, 0.25f * rTextArea.fSize, std::string_view(rTextArea.pcText, rTextArea.iCharacterCount), 0xFFFFFFFF, pQuads, iPos, kiMaxTextQuads);
	}

	gpPipelineManager->mpPipelines[kPipelineProfileText].WriteIndirectBuffer(iCommandBuffer, iPos);
}

} // namespace engine
