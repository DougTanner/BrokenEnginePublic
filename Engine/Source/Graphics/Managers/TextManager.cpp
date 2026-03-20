#include "TextManager.h"

#include "Profile/ProfileManager.h"

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

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();

	{
		const EagerChunk& rChunk = rChunkMap.at(data::kFontsNotoSansNotoSansRegularfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;
		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::RoundUp<int64_t, common::kiAlignmentBytes>(iCharacters * static_cast<int64_t>(sizeof(pCharacterIds[0]))));
		Log(kLogLoading, "Loading font {:#018x} with {} characters", data::kFontsNotoSansNotoSansRegularfntCrc, iCharacters);
		mfLineHeightEfigs = static_cast<float>(rChunk.pHeader->fontHeader.iLineHeight);

		for (int64_t i = 0; i < iCharacters; ++i)
		{
			if (pCharacterIds[i] < 128)
			{
				mpCharactersEfigs[pCharacterIds[i]] = &pCharacters[i];
			}
		}
	}

	gpTextManager->UpdateTextArea(kTextDebug, "");
}

TextManager::~TextManager()
{
	gpTextManager = nullptr;
}

common::Character* TextManager::GetCharacter(uint32_t uiChar)
{
	return mpCharactersEfigs[uiChar % 128];
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

		int64_t iStartPos = iPos;

		// Compute quads once (main pass: white, no offset), limit to half remaining capacity for shadow duplication
		int64_t iHalfMax = iStartPos + (kiMaxTextQuads - iStartPos) / 2;
		WriteQuads(common::gpThreadLocal->mWorkbuffer.Span<float>(), rTextArea.fY, 0.25f * rTextArea.fSize, std::string_view(rTextArea.text, rTextArea.iCharacterCount), 0xFFFFFFFF, 0.0f, 0.0f, pQuads, iPos, iHalfMax);

		int64_t iCount = iPos - iStartPos;

		// Make room: move main quads forward by iCount
		memmove(&pQuads[iStartPos + iCount], &pQuads[iStartPos], iCount * sizeof(pQuads[0]));
		iPos += iCount;

		// Fill shadow quads at [iStartPos, iStartPos + iCount) as copies with shadow color + offset
		memcpy(&pQuads[iStartPos], &pQuads[iStartPos + iCount], iCount * sizeof(pQuads[0]));
		for (int64_t i = 0; i < iCount; ++i)
		{
			pQuads[iStartPos + i].f4VertexRect.x += 2.0f * kfShadowOffsetX;
			pQuads[iStartPos + i].f4VertexRect.y -= 2.0f * kfShadowOffsetY;
			pQuads[iStartPos + i].uiColor = 0xFF000000;
		}

		common::gpThreadLocal->mWorkbuffer.Pop();
	}

	gpPipelineManager->mpPipelines[kPipelineProfileText].WriteIndirectBuffer(iCommandBuffer, iPos);
}

} // namespace engine
