#pragma once

namespace engine
{

inline constexpr int64_t kiMaxTextQuads = 4096;

enum TextAreas
{
	kTextDebug,

	kTextGraphics,
	kTextProfileFps,
	kTextProfileCpuTimers,
	kTextProfileGpuTimers,
	kTextProfileCpuCounters,
	kTextProfileMemory,
	kTextProfileFrameStats,

	kTextAreasCount
};
struct TextArea
{
	static constexpr int64_t kiMaxChars = 4096;

	float fX = 0.0f;
	float fY = 0.0f;
	float fSize = 0.1f;

	int64_t iCharacterCount = 0;
	char text[kiMaxChars] {};
};
inline constexpr float kfEdge = 0.025f;
inline TextArea gpTextAreas[]
{
	// kTextDebug
	TextArea
	{
		.fX = 0.45f,
		.fY = 1.0f - 2.0f * kfEdge,
	},
	// kTextGraphics
	TextArea
	{
		.fX = 0.875f,
		.fY = kfEdge,
	},
	// kTextProfileFps
	TextArea
	{
		.fX = 0.5f * kfEdge,
		.fY = kfEdge,
	},
	// kTextProfileCpuTimers
	TextArea
	{
		.fX = 0.5f * kfEdge,
		.fY = kfEdge,
	},
	// kTextProfileGpuTimers
	TextArea
	{
		.fX = 0.5f * kfEdge,
		.fY = kfEdge,
	},
	// kTextProfileCpuCounters
	TextArea
	{
		.fX = 0.875f,
		.fY = 0.15f,
	},
	// kTextProfileMemory
	TextArea
	{
		.fX = 0.725f,
		.fY = 0.15f,
	},
	// kTextProfileFrameStats
	TextArea
	{
		.fX = 0.5f * kfEdge,
		.fY = kfEdge,
	},
};
static_assert(std::size(gpTextAreas) == kTextAreasCount);

inline constexpr float kfEfigsSize = 2048.0f;

class TextManager
{
public:

	TextManager();
	~TextManager();

	common::Character* GetCharacter(uint32_t uiChar);

	void UpdateTextArea(TextAreas eTextArea, std::string_view characters);
	void RenderMain(int64_t iCommandBuffer);

	template<typename T>
	[[nodiscard]] std::vector<float> MeasureQuads(float fSize, std::basic_string_view<T> text)
	{
		float fInverseLineHeight = 1.0f / mfLineHeightEfigs;
		float fInverseAspectRatio = 1.0f / gpSwapchainManager->mfAspectRatio;

		std::vector<float> widths;
		float fCurrentX = 0.0f;
		for (size_t iInPos = 0; iInPos < text.size(); ++iInPos)
		{
			if (text[iInPos] == '\n')
			{
				widths.push_back(fCurrentX);
				fCurrentX = 0.0f;
				continue;
			}

			common::Character* pCharacter = GetCharacter(text[iInPos]);
			float fAdvance = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXAdvance);

			fCurrentX += fAdvance;
		}
		widths.push_back(fCurrentX);

		return widths;
	}

	template<typename T, typename U>
	void WriteQuads(std::span<const float> xOffsets, float fY, float fSize, std::basic_string_view<T> text, uint32_t uiColor, float fXScreenOffset, float fYScreenOffset, U* pQuads, int64_t& riPos, int64_t iMaxPos)
	{
		float fInverseAspectRatio = 1.0f / gpSwapchainManager->mfAspectRatio;
		float fInverseLineHeight = 1.0f / mfLineHeightEfigs;

		int64_t iCurrentX = 0;
		float fCurrentX = xOffsets[iCurrentX++];
		float fCurrentY = fY;
		for (size_t iInPos = 0; iInPos < text.size(); ++iInPos)
		{
			if (riPos >= iMaxPos)
			{
				Log(kLogGraphics, kWarning, "riPos >= iMaxPos");
				break;
			}

			if (text[iInPos] == '\n')
			{
				fCurrentX = xOffsets.size() == 1 ? xOffsets[0] : xOffsets[std::min(iCurrentX++, static_cast<int64_t>(xOffsets.size() - 1))];
				fCurrentY += fSize;
				continue;
			}

			common::Character* pCharacter = GetCharacter(text[iInPos]);

			float fWidth = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->uiWidth);
			float fHeight = fSize * fInverseLineHeight * static_cast<float>(pCharacter->uiHeight);
			float fXOffset = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXOffset);
			float fYOffset = fSize * fInverseLineHeight * static_cast<float>(pCharacter->iYOffset);
			float fAdvance = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXAdvance);

			pQuads[riPos].f4VertexRect = {-1.0f + 2.0f * (fCurrentX + fXOffset + fXScreenOffset), 1.0f - 2.0f * (fCurrentY + fYOffset + fYScreenOffset), 2.0f * fWidth, -2.0f * fHeight};
			fCurrentX += fAdvance;

			pQuads[riPos].f4TextureRect = XMFLOAT4(static_cast<float>(pCharacter->uiX) / kfEfigsSize, static_cast<float>(pCharacter->uiY) / kfEfigsSize, static_cast<float>(pCharacter->uiX + pCharacter->uiWidth) / kfEfigsSize, static_cast<float>(pCharacter->uiY + pCharacter->uiHeight) / kfEfigsSize);
			pQuads[riPos].uiColor = uiColor;

			++riPos;
		}
	}

	float mfLineHeightEfigs = 0.0f;

private:

	common::Character* mpCharactersEfigs[128] {};
};

inline TextManager* gpTextManager = nullptr;

} // namespace engine
