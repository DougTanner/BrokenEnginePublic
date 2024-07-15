#pragma once

#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Ui/Localization.h"
#include "Ui/UiManager.h"

namespace engine
{

inline constexpr int64_t kiMaxTextQuads = 2048;

enum TextAreas
{
	kTextDebug,

	kTextGraphics,
	kTextProfileFps,
	kTextProfileCpuTimers,
	kTextProfileGpuTimers,
	kTextProfileCpuCounters,

	kTextAreasCount
};
struct TextArea
{
	static constexpr int64_t kiMaxChars = 4096;

	float fX = 0.0f;
	float fY = 0.0f;
	float fSize = 0.1f;

	int64_t iCharacterCount = 0;
	char pcText[kiMaxChars] {};
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
		.fX = 0.25f,
		.fY = kfEdge,
	},
	// kTextProfileCpuCounters
	TextArea
	{
		.fX = 0.875f,
		.fY = 0.15f,
	},
};
static_assert(std::size(gpTextAreas) == kTextAreasCount);

inline constexpr float kfEfigsSize = 2048.0f;
inline constexpr float kfChineseSize = 8192.0f;

template<typename T>
constexpr bool IsEfigs(std::basic_string_view<T> pcText)
{
	return pcText.size() == 0 ? true : pcText[0] < 0x4E00;
}

class TextManager
{
public:

	TextManager();
	~TextManager();

	std::tuple<common::Character*, bool> GetCharacter(uint32_t uiChar);

	void UpdateTextArea(TextAreas eTextArea, std::string_view pcCharacters);
	void RenderMain(int64_t iCommandBuffer);

	template<typename T>
	[[nodiscard]] std::vector<float> MeasureQuads(float fSize, std::basic_string_view<T> pcText)
	{
		float fInverseLineHeight = 1.0f / (IsEfigs(pcText) ? mfLineHeightEfigs : mfLineHeightChinese);
		float fInverseAspectRatio = 1.0f / gpSwapchainManager->mfAspectRatio;

		std::vector<float> widths;
		float fCurrentX = 0.0f;
		for (size_t iInPos = 0; iInPos < pcText.size(); ++iInPos)
		{
			if (pcText[iInPos] == '\n')
			{
				widths.push_back(fCurrentX);
				fCurrentX = 0.0f;
				continue;
			}

			auto [pCharacter, bEfigs] = GetCharacter(pcText[iInPos]);
			float fAdvance = fInverseAspectRatio * fSize * fInverseLineHeight * static_cast<float>(pCharacter->iXAdvance);

			fCurrentX += fAdvance;
		}
		widths.push_back(fCurrentX);

		return widths;
	}

	template<typename T, typename U>
	void WriteQuads(const std::vector<float>& rXOffsets, float fY, float fSize, std::basic_string_view<T> pcText, uint32_t uiColor, U* pQuads, int64_t& riPos, int64_t iMaxPos)
	{
		float fInverseAspectRatio = 1.0f / gpSwapchainManager->mfAspectRatio;

		int64_t iCurrentX = 0;
		float fCurrentX = rXOffsets.at(iCurrentX++);
		float fCurrentY = fY;
		for (size_t iInPos = 0; iInPos < pcText.size(); ++iInPos)
		{
			if (riPos >= iMaxPos)
			{
				LOG("riPos >= iMaxPos");
				break;
			}

			if (pcText[iInPos] == '\n')
			{
				fCurrentX = rXOffsets.size() == 1 ? rXOffsets.at(0) : rXOffsets.at(iCurrentX++);
				fCurrentY += fSize;
				continue;
			}

			auto [pCharacter, bEfigs] = GetCharacter(pcText[iInPos]);
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

			pQuads[riPos].f4VertexRect = {-1.0f + 2.0f * (fCurrentX + fXOffset), 1.0f - 2.0f * (fCurrentY + fYOffset), 2.0f * fWidth, -2.0f * fHeight};
			fCurrentX += fAdvance;

			float fTextureHeight = bEfigs ? kfEfigsSize : kfChineseSize;
			float fTextureWidth = bEfigs ? kfEfigsSize : kfChineseSize;
			pQuads[riPos].f4TextureRect = DirectX::XMFLOAT4
			(
				static_cast<float>(pCharacter->uiX) / fTextureWidth,
				static_cast<float>(pCharacter->uiY) / fTextureHeight,
				static_cast<float>(pCharacter->uiX + pCharacter->uiWidth) / fTextureWidth,
				static_cast<float>(pCharacter->uiY + pCharacter->uiHeight) / fTextureHeight
			);

			if constexpr(std::is_same_v<U, shaders::WidgetLayout>)
			{
				pQuads[riPos].ui4Misc = {uiColor, UiCrcToIndex(bEfigs ? data::kTexturesUiBC4NotoSansRegularpngCrc : data::kTexturesUiBC4NotoSansSCLightpngCrc), 2, 0};
			}

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
