#pragma once

#include "Graphics/Managers/SwapchainManager.h"

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
inline constexpr float kfChineseSize = 8192.0f;

template<typename T>
constexpr bool IsEfigs(std::basic_string_view<T> text)
{
	return text.size() == 0 ? true : text[0] < 0x4E00;
}

class TextManager
{
public:

	TextManager();
	~TextManager();

	std::tuple<common::Character*, bool> GetCharacter(uint32_t uiChar);

	void UpdateTextArea(TextAreas eTextArea, std::string_view characters);
	void RenderMain(int64_t iCommandBuffer);

	template<typename T>
	[[nodiscard]] std::vector<float> MeasureQuads(float fSize, std::basic_string_view<T> text)
	{
		float fInverseLineHeight = 1.0f / (IsEfigs(text) ? mfLineHeightEfigs : mfLineHeightChinese);
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

			auto [pCharacter, bEfigs] = GetCharacter(text[iInPos]);
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

		int64_t iCurrentX = 0;
		float fCurrentX = xOffsets[iCurrentX++];
		float fCurrentY = fY;
		for (size_t iInPos = 0; iInPos < text.size(); ++iInPos)
		{
			if (riPos >= iMaxPos)
			{
				Log("riPos >= iMaxPos");
				break;
			}

			if (text[iInPos] == '\n')
			{
				fCurrentX = xOffsets.size() == 1 ? xOffsets[0] : xOffsets[iCurrentX++];
				fCurrentY += fSize;
				continue;
			}

			auto [pCharacter, bEfigs] = GetCharacter(text[iInPos]);
			float fLineHeight = bEfigs ? mfLineHeightEfigs : mfLineHeightChinese;

			// Manual adjustments to match Efigs
			if (!bEfigs)
			{
				fLineHeight *= 1.4f;
			}

			float fInverseLineHeight = 1.0f / fLineHeight;
			float fWidth = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->uiWidth);
			float fHeight = fSize * fInverseLineHeight * static_cast<float>(pCharacter->uiHeight);
			float fXOffset = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXOffset);
			float fYOffset = fSize * fInverseLineHeight * static_cast<float>(pCharacter->iYOffset);
			float fAdvance = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXAdvance);

			// Manual adjustments to match Efigs
			if (!bEfigs)
			{
				fWidth *= 1.4f;
				fHeight *= 1.4f;
				fAdvance *= 1.6f;
			}

			pQuads[riPos].f4VertexRect = {-1.0f + 2.0f * (fCurrentX + fXOffset + fXScreenOffset), 1.0f - 2.0f * (fCurrentY + fYOffset + fYScreenOffset), 2.0f * fWidth, -2.0f * fHeight};
			fCurrentX += fAdvance;

			float fTextureHeight = bEfigs ? kfEfigsSize : kfChineseSize;
			float fTextureWidth = bEfigs ? kfEfigsSize : kfChineseSize;
			pQuads[riPos].f4TextureRect = XMFLOAT4
			(
				static_cast<float>(pCharacter->uiX) / fTextureWidth,
				static_cast<float>(pCharacter->uiY) / fTextureHeight,
				static_cast<float>(pCharacter->uiX + pCharacter->uiWidth) / fTextureWidth,
				static_cast<float>(pCharacter->uiY + pCharacter->uiHeight) / fTextureHeight
			);
			pQuads[riPos].uiColor = uiColor;

			++riPos;
		}
	}

	float mfLineHeightEfigs = 0.0f;
	float mfLineHeightChinese = 0.0f;

private:

	std::unordered_map<uint32_t, common::Character*> mCharacterMapEfigs;
	std::unordered_map<uint32_t, common::Character*> mCharacterMapChinese;
};

inline TextManager* gpTextManager = nullptr;

} // namespace engine
