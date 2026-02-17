#include "TextManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Ui/Localization.h"
#include "BufferManager.h"
#include "PipelineManager.h"
#include "Profile/ProfileManager.h"
#include "ThreadLocal.h"

#include "Data/Font.h"
#include "Data/Raw.h"

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

	ScopedBootTimer scopedBootTimer(kBootTimerTextManager);

	std::setlocale(LC_ALL, "en_US.utf8");
	for (int64_t i = 0; i < game::kStringsCount; ++i)
	{
		for (int64_t j = 0; j < game::kLanguageCount; ++j)
		{
			int64_t k = 0;
			while (game::gppTranslatedStrings[i][j][k] != 0)
			{
				game::gppTranslatedStrings[i][j][k] = towupper(static_cast<wint_t>(game::gppTranslatedStrings[i][j][k]));
				++k;
			}			
		}
	}

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();

	{
		const EagerChunk& rChunk = rChunkMap.at(data::kFontsNotoSansNotoSansRegularfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;
		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::RoundUp<int64_t, common::kiAlignmentBytes>(iCharacters * static_cast<int64_t>(sizeof(pCharacterIds[0]))));
		Log("Loading font {:#018x} with {} characters", data::kFontsNotoSansNotoSansRegularfntCrc, iCharacters);
		mfLineHeightEfigs = static_cast<float>(rChunk.pHeader->fontHeader.iLineHeight);

		for (int64_t i = 0; i < iCharacters; ++i)
		{
			auto [it, bInserted] = mCharacterMapEfigs.try_emplace(pCharacterIds[i], &pCharacters[i]);
			ASSERT(bInserted);
		}
	}

	{
		const EagerChunk& rChunk = rChunkMap.at(data::kFontsNotoSansSCNotoSansSCLightfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;
		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::RoundUp<int64_t, common::kiAlignmentBytes>(iCharacters * static_cast<int64_t>(sizeof(pCharacterIds[0]))));
		Log("Loading font {:#018x} with {} characters", data::kFontsNotoSansSCNotoSansSCLightfntCrc, iCharacters);
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

void TextManager::UpdateTextArea(TextAreas eTextArea, std::string_view characters)
{
	ASSERT(characters.size() < TextArea::kiMaxChars);

	TextArea& rTextArea = gpTextAreas[eTextArea];
	rTextArea.iCharacterCount = std::min(static_cast<int64_t>(characters.size()), TextArea::kiMaxChars);
	memcpy(rTextArea.text, characters.data(), rTextArea.iCharacterCount);
}

void TextManager::RenderMain(int64_t iCommandBuffer)
{
	auto pQuads = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mTextStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iPos = 0;

	static constexpr float kfShadowOffsetX = 0.00075f;
	static constexpr float kfShadowOffsetY = 0.00175f;

	for (const TextArea& rTextArea : gpTextAreas)
	{
		common::gpThreadLocal->mWorkbuffer.Push();
		common::gpThreadLocal->mWorkbuffer.PushBack(rTextArea.fX);

		// Shadow pass (black, offset down-right)
		WriteQuads(common::gpThreadLocal->mWorkbuffer.Span<float>(), rTextArea.fY, 0.25f * rTextArea.fSize, std::string_view(rTextArea.text, rTextArea.iCharacterCount), 0xFF000000, kfShadowOffsetX, kfShadowOffsetY, pQuads, iPos, kiMaxTextQuads);

		// Main pass (white, no offset)
		WriteQuads(common::gpThreadLocal->mWorkbuffer.Span<float>(), rTextArea.fY, 0.25f * rTextArea.fSize, std::string_view(rTextArea.text, rTextArea.iCharacterCount), 0xFFFFFFFF, 0.0f, 0.0f, pQuads, iPos, kiMaxTextQuads);
		common::gpThreadLocal->mWorkbuffer.Pop();
	}

	gpPipelineManager->mpPipelines[kPipelineProfileText].WriteIndirectBuffer(iCommandBuffer, iPos);
}

} // namespace engine
