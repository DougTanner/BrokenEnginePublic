#if defined(BT_CLIENT)

#include "TextManager.h"

#include "Data/Font.h"

namespace engine
{

TextManager::TextManager()
{
	ASSERT(gpTextManager == nullptr);

	gpTextManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerTextManager);

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();

	{
		const EagerChunk& rChunk = rChunkMap.at(data::kFontsNotoSansNotoSansRegularfntCrc);
		int64_t iCharacters = rChunk.pHeader->fontHeader.iCharacters;

		// Trust boundary: iCharacters comes from on-disk pack bytes and drives a reinterpret_cast pointer offset
		// plus an indexed loop aliasing the eager chunk. Font glyph count has no structural maximum, so bound it
		// against the chunk's actual data bytes (ChunkHeader::iSize, the uncompressed eager payload size): the exact
		// reader layout is [uint32 ids, ALIGN16][Character array], so the Character array's end offset must fit iSize.
		// The leading divide cap keeps the subsequent multiply overflow-safe. A corrupt count throws; boot-required
		// font, so let it propagate to MainThread's try/catch (HandleException — crash report + exit) — boot hard-fail.
		if (iCharacters < 0
			|| iCharacters > rChunk.pHeader->iSize / static_cast<int64_t>(sizeof(uint32_t))
			|| common::FontHeader::CharactersOffset(iCharacters)
				+ iCharacters * static_cast<int64_t>(sizeof(common::Character)) > rChunk.pHeader->iSize)
		{
			LOG(kLoading, kError, "Corrupt font chunk {:#018x}: implausible character count {}", data::kFontsNotoSansNotoSansRegularfntCrc, iCharacters);
			throw common::CorruptStreamException("TextManager font");
		}

		auto pCharacterIds = reinterpret_cast<uint32_t*>(rChunk.pData);
		auto pCharacters = reinterpret_cast<common::Character*>(rChunk.pData + common::FontHeader::CharactersOffset(iCharacters));
		LOG(kLoading, kDebug, "Loading font {:#018x} with {} characters", data::kFontsNotoSansNotoSansRegularfntCrc, iCharacters);
		mfLineHeightEfigs = static_cast<float>(rChunk.pHeader->fontHeader.iLineHeight);

		for (int64_t i = 0; i < iCharacters; ++i)
		{
			if (pCharacterIds[i] < 128)
			{
				mpCharactersEfigs[pCharacterIds[i]] = &pCharacters[i];
			}
		}
	}

	UpdateTextArea(kTextDebug, "");
}

TextManager::~TextManager()
{
	if (gpTextManager == this)
	{
		gpTextManager = nullptr;
	}
}

common::Character* TextManager::GetCharacter(uint32_t uiChar)
{
	// unsigned-char semantics: signed-char callers sign-extend high UTF-8 bytes, so mask to a byte before the modulo.
	// Fall back to '?' (always font-present) when the slot is null — the ctor leaves control-char slots nullptr.
	common::Character* pCharacter = mpCharactersEfigs[static_cast<unsigned char>(uiChar) % 128];
	return pCharacter != nullptr ? pCharacter : mpCharactersEfigs['?'];
}

void TextManager::UpdateTextArea(TextAreas eTextArea, std::string_view characters)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	ASSERT(characters.size() < TextArea::kiMaxChars);

	TextArea& rTextArea = gpTextAreas[eTextArea];
	rTextArea.iCharacterCount = std::min(static_cast<int64_t>(characters.size()), TextArea::kiMaxChars);
	std::memcpy(rTextArea.text, characters.data(), rTextArea.iCharacterCount);
}

void TextManager::RenderMain(int64_t iCommandBuffer)
{
	// Build the full quad list (main + shadow) in cached workbuffer staging, then copy once into the
	// persistent-mapped write-combined storage buffer. Building directly in mapped memory forces the
	// memmove/memcpy/read-modify-write below to read back uncached bytes on every quad.
	common::ScopedWorkbufferAllocation<shaders::AxisAlignedQuadLayout*> stagingQuads
		= common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(kiMaxTextQuads * static_cast<int64_t>(sizeof(shaders::AxisAlignedQuadLayout)));
	shaders::AxisAlignedQuadLayout* pQuads = stagingQuads;
	int64_t iPos = 0;

	static constexpr float kfShadowOffsetX = 0.00075f;
	static constexpr float kfShadowOffsetY = 0.00175f;

	for (const TextArea& rTextArea : gpTextAreas)
	{
		int64_t iStartPos = iPos;

		// Compute quads once (main pass: white, no offset), limit to half remaining capacity for shadow duplication
		int64_t iHalfMax = iStartPos + (kiMaxTextQuads - iStartPos) / 2;
		WriteQuads(rTextArea.fX, rTextArea.fY, 0.25f * rTextArea.fSize, std::string_view(rTextArea.text, rTextArea.iCharacterCount), 0xFFFFFFFF, 0.0f, 0.0f, pQuads, iPos, iHalfMax);

		int64_t iCount = iPos - iStartPos;

		// Make room: move main quads forward by iCount
		std::memmove(&pQuads[iStartPos + iCount], &pQuads[iStartPos], iCount * sizeof(pQuads[0]));
		iPos += iCount;

		// Fill shadow quads at [iStartPos, iStartPos + iCount) as copies with shadow color + offset
		std::memcpy(&pQuads[iStartPos], &pQuads[iStartPos + iCount], iCount * sizeof(pQuads[0]));
		for (int64_t i = 0; i < iCount; ++i)
		{
			pQuads[iStartPos + i].f4VertexRect.x += 2.0f * kfShadowOffsetX;
			pQuads[iStartPos + i].f4VertexRect.y -= 2.0f * kfShadowOffsetY;
			pQuads[iStartPos + i].uiColor = 0xFF000000;
		}

	}

	// One linear copy of the finished quad list into the write-combined storage buffer.
	std::memcpy(gpBufferManager->mTextStorageBuffers.at(iCommandBuffer).mpMappedMemory, pQuads, iPos * sizeof(pQuads[0]));

	gpPipelineManager->mpPipelines[kPipelineProfileText].WriteIndirectBuffer(iCommandBuffer, iPos);
}

} // namespace engine

#endif // defined(BT_CLIENT)
