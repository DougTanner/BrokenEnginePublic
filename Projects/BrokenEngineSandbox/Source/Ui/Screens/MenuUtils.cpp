#include "MenuUtils.h"

#include "Ui/GraphicsSettingsWrappersBase.h"

#include "Ui/Localization.h"

namespace game
{

common::ScopedWorkbufferAllocation<char*> AppendUtf8(common::Workbuffer& rWorkbuffer, std::u32string_view u32String)
{
	// Reserve the worst case (4 UTF-8 bytes per code point + the null terminator), encode in a single pass, then
	// shrink to the actual length. The returned move-only handle owns the frame, so the bytes stay valid through
	// the caller's full-expression (the implicit const char* hands straight to the consuming ImGui call).
	common::ScopedWorkbufferAllocation<char*> scopedAllocation = rWorkbuffer.PushBuffer<char*>(static_cast<int64_t>(u32String.size()) * 4 + 1);
	char* const pcBase = scopedAllocation;
	char* pcWrite = pcBase;
	for (char32_t cCodePoint : u32String)
	{
		if (cCodePoint < 0x80)
		{
			*pcWrite++ = static_cast<char>(cCodePoint);
		}
		else if (cCodePoint < 0x800)
		{
			*pcWrite++ = static_cast<char>(0xC0 | (cCodePoint >> 6));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
		else if (cCodePoint < 0x10000)
		{
			*pcWrite++ = static_cast<char>(0xE0 | (cCodePoint >> 12));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 6) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
		else
		{
			*pcWrite++ = static_cast<char>(0xF0 | (cCodePoint >> 18));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 12) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 6) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
	}
	*pcWrite++ = '\0';
	rWorkbuffer.ShrinkLastPushBuffer(static_cast<int64_t>(pcWrite - pcBase));
	return scopedAllocation;
}

bool WrapperToggle(std::string_view label, engine::Wrapper* pWrapper)
{
	bool bValue = pWrapper->Get<bool>();
	if (ImGui::Checkbox(label.data(), &bValue))
	{
		pWrapper->Set(bValue);
		return true;
	}
	return false;
}

bool WrapperSlider(std::string_view label, engine::Wrapper* pWrapper)
{
	float fValue = pWrapper->Get();
	if (ImGui::SliderFloat(label.data(), &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.2f"))
	{
		pWrapper->Set(fValue);
		return true;
	}
	return false;
}

bool WrapperPlusMinus(std::string_view label, engine::Wrapper* pWrapper, float fStep)
{
	bool bChanged = false;
	ImGui::Text("%s", label.data());
	ImGui::SameLine();
	ImGui::PushID(label.data());
	if (ImGui::Button("-"))
	{
		pWrapper->Set(pWrapper->Get() - fStep);
		bChanged = true;
	}
	ImGui::SameLine();
	ImGui::Text("%.0f%%", pWrapper->Get() * 100.0f);
	ImGui::SameLine();
	if (ImGui::Button("+"))
	{
		pWrapper->Set(pWrapper->Get() + fStep);
		bChanged = true;
	}
	ImGui::PopID();
	return bChanged;
}

#if defined(BT_CLIENT)

namespace
{

constexpr float kfPanelRounding = 8.0f;
constexpr float kfButtonRounding = 6.0f;
constexpr float kfPanelBorderThickness = 2.0f;
constexpr float kfAccentStripThickness = 3.0f;
constexpr float kfButtonAccentBarWidth = 6.0f;
constexpr float kfHoverAnimRate = 12.0f;

// Custom menu chrome per engine::UiTheme; hues track the engine ThemePalettes (ImGuiManager.cpp) — designer-pass placeholders
struct MenuChrome
{
	ImVec4 f4PanelFillTop;
	ImVec4 f4PanelFillBottom;
	ImVec4 f4PanelBorder;
	ImVec4 f4Accent;
	ImVec4 f4ButtonFill;
	ImVec4 f4ButtonHover;
	ImVec4 f4ButtonActive;
	ImVec4 f4BackdropDim;
};

constexpr MenuChrome kMenuChromes[]
{
	// kNavalSteel
	{
		.f4PanelFillTop = ImVec4(0.10f, 0.14f, 0.18f, 0.92f),
		.f4PanelFillBottom = ImVec4(0.05f, 0.07f, 0.09f, 0.92f),
		.f4PanelBorder = ImVec4(0.30f, 0.42f, 0.52f, 0.8f),
		.f4Accent = ImVec4(0.15f, 0.75f, 0.85f, 1.0f),
		.f4ButtonFill = ImVec4(0.10f, 0.14f, 0.18f, 0.75f),
		.f4ButtonHover = ImVec4(0.16f, 0.36f, 0.44f, 0.9f),
		.f4ButtonActive = ImVec4(0.10f, 0.55f, 0.65f, 0.95f),
		.f4BackdropDim = ImVec4(0.0f, 0.0f, 0.0f, 0.55f),
	},
	// kDarkAmber
	{
		.f4PanelFillTop = ImVec4(0.14f, 0.13f, 0.11f, 0.92f),
		.f4PanelFillBottom = ImVec4(0.07f, 0.06f, 0.05f, 0.92f),
		.f4PanelBorder = ImVec4(0.45f, 0.38f, 0.26f, 0.8f),
		.f4Accent = ImVec4(0.95f, 0.65f, 0.15f, 1.0f),
		.f4ButtonFill = ImVec4(0.14f, 0.13f, 0.11f, 0.75f),
		.f4ButtonHover = ImVec4(0.42f, 0.30f, 0.12f, 0.9f),
		.f4ButtonActive = ImVec4(0.62f, 0.42f, 0.10f, 0.95f),
		.f4BackdropDim = ImVec4(0.0f, 0.0f, 0.0f, 0.55f),
	},
	// kMonochrome
	{
		.f4PanelFillTop = ImVec4(0.13f, 0.13f, 0.13f, 0.92f),
		.f4PanelFillBottom = ImVec4(0.06f, 0.06f, 0.06f, 0.92f),
		.f4PanelBorder = ImVec4(0.42f, 0.42f, 0.42f, 0.8f),
		.f4Accent = ImVec4(0.90f, 0.90f, 0.90f, 1.0f),
		.f4ButtonFill = ImVec4(0.13f, 0.13f, 0.13f, 0.75f),
		.f4ButtonHover = ImVec4(0.32f, 0.32f, 0.32f, 0.9f),
		.f4ButtonActive = ImVec4(0.50f, 0.50f, 0.50f, 0.95f),
		.f4BackdropDim = ImVec4(0.0f, 0.0f, 0.0f, 0.55f),
	},
};
static_assert(std::size(kMenuChromes) == static_cast<size_t>(engine::UiTheme::kCount));

const MenuChrome& GetMenuChrome()
{
	return kMenuChromes[static_cast<size_t>(engine::GetUiTheme())];
}

ImVec4 LerpColor(const ImVec4& rf4A, const ImVec4& rf4B, float fT)
{
	return ImVec4(std::lerp(rf4A.x, rf4B.x, fT), std::lerp(rf4A.y, rf4B.y, fT), std::lerp(rf4A.z, rf4B.z, fT), std::lerp(rf4A.w, rf4B.w, fT));
}

// GetColorU32 additionally multiplies by style.Alpha, so BeginDisabled dims custom chrome like stock widgets
ImU32 ChromeColor(const ImVec4& rf4Color, float fAlphaScale = 1.0f)
{
	return ImGui::GetColorU32(ImVec4(rf4Color.x, rf4Color.y, rf4Color.z, rf4Color.w * fAlphaScale));
}

} // namespace

ScopedMenuFont::ScopedMenuFont(float fScale)
{
	ImFont* pFont = geLanguage == kChinese ? engine::gpImGuiManager->mpChineseFont : nullptr;
	ImGui::PushFont(pFont, ImGui::GetStyle().FontSizeBase * fScale);
}

ScopedMenuFont::~ScopedMenuFont()
{
	ImGui::PopFont();
}

void DrawFullScreenDim()
{
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, ChromeColor(GetMenuChrome().f4BackdropDim));
}

void DrawPanelBackground(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax)
{
	const MenuChrome& rChrome = GetMenuChrome();

	// User opacity comes from the wrappers, not Colors[ImGuiCol_WindowBg].w — callers push a fully transparent
	// WindowBg before Begin() and PushStyleColor writes the live style, so the style read would always be 0.0
	float fWindowAlpha = engine::gOpaqueUi.Get<bool>() ? 1.0f : engine::gUiOpacity.Get();

	pDrawList->AddRectFilled(vMin, vMax, ChromeColor(rChrome.f4PanelFillBottom, fWindowAlpha), kfPanelRounding);

	// AddRectFilledMultiColor cannot round corners — inset the gradient past the radius
	ImVec2 vInsetMin(vMin.x + kfPanelRounding, vMin.y + kfPanelRounding);
	ImVec2 vInsetMax(vMax.x - kfPanelRounding, vMax.y - kfPanelRounding);
	if (vInsetMin.x < vInsetMax.x && vInsetMin.y < vInsetMax.y)
	{
		ImU32 uiTop = ChromeColor(rChrome.f4PanelFillTop, fWindowAlpha);
		ImU32 uiBottom = ChromeColor(rChrome.f4PanelFillBottom, fWindowAlpha);
		pDrawList->AddRectFilledMultiColor(vInsetMin, vInsetMax, uiTop, uiTop, uiBottom, uiBottom);
	}

	pDrawList->AddRect(vMin, vMax, ChromeColor(rChrome.f4PanelBorder, fWindowAlpha), kfPanelRounding, 0, kfPanelBorderThickness);
	pDrawList->AddRectFilled(ImVec2(vMin.x + kfPanelRounding, vMin.y), ImVec2(vMax.x - kfPanelRounding, vMin.y + kfAccentStripThickness), ChromeColor(rChrome.f4Accent, fWindowAlpha));
}

void DrawPanelAccents(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax)
{
	const MenuChrome& rChrome = GetMenuChrome();
	pDrawList->AddRect(vMin, vMax, ChromeColor(rChrome.f4PanelBorder), kfPanelRounding, 0, kfPanelBorderThickness);
	pDrawList->AddRectFilled(ImVec2(vMin.x + kfPanelRounding, vMin.y), ImVec2(vMax.x - kfPanelRounding, vMin.y + kfAccentStripThickness), ChromeColor(rChrome.f4Accent));
}

bool MenuButton(const char* pcLabel, const ImVec2& vSize, float& rfHoverAnim, bool bSelected)
{
	ImVec2 vTextSize = ImGui::CalcTextSize(pcLabel);
	const ImGuiStyle& rStyle = ImGui::GetStyle();
	ImVec2 vButtonSize(vSize.x > 0.0f ? vSize.x : vTextSize.x + rStyle.FramePadding.x * 2.0f, vSize.y > 0.0f ? vSize.y : vTextSize.y + rStyle.FramePadding.y * 2.0f);

	// EnableNav: InvisibleButton defaults to ImGuiItemFlags_NoNav, which would skip keyboard/gamepad navigation
	bool bPressed = ImGui::InvisibleButton(pcLabel, vButtonSize, ImGuiButtonFlags_EnableNav);
	bool bHovered = ImGui::IsItemHovered() || ImGui::IsItemFocused(); // Focus term keeps keyboard/gamepad nav visible
	rfHoverAnim += ((bHovered ? 1.0f : 0.0f) - rfHoverAnim) * common::ExponentialInterpolant(kfHoverAnimRate, ImGui::GetIO().DeltaTime);

	const MenuChrome& rChrome = GetMenuChrome();
	ImVec2 vMin = ImGui::GetItemRectMin();
	ImVec2 vMax = ImGui::GetItemRectMax();
	ImDrawList* pDrawList = ImGui::GetWindowDrawList();

	ImVec4 f4Fill = LerpColor(rChrome.f4ButtonFill, rChrome.f4ButtonHover, bSelected ? 1.0f : rfHoverAnim);
	if (ImGui::IsItemActive())
	{
		f4Fill = rChrome.f4ButtonActive;
	}
	pDrawList->AddRectFilled(vMin, vMax, ChromeColor(f4Fill), kfButtonRounding);
	pDrawList->AddRect(vMin, vMax, ChromeColor(LerpColor(rChrome.f4PanelBorder, rChrome.f4Accent, bSelected ? 1.0f : rfHoverAnim)), kfButtonRounding);

	// Left accent bar grows from the vertical center with hover/selection
	float fBarIntensity = bSelected ? 1.0f : rfHoverAnim;
	if (fBarIntensity > 0.01f)
	{
		float fCenterY = (vMin.y + vMax.y) * 0.5f;
		float fHalfHeight = ((vMax.y - vMin.y) * 0.5f - kfButtonRounding * 0.5f) * fBarIntensity;
		pDrawList->AddRectFilled(ImVec2(vMin.x, fCenterY - fHalfHeight), ImVec2(vMin.x + kfButtonAccentBarWidth, fCenterY + fHalfHeight), ChromeColor(rChrome.f4Accent, fBarIntensity));
	}

	ImVec2 vTextPos((vMin.x + vMax.x - vTextSize.x) * 0.5f, (vMin.y + vMax.y - vTextSize.y) * 0.5f);
	pDrawList->AddText(vTextPos, ImGui::GetColorU32(ImGuiCol_Text), pcLabel);

	return bPressed;
}

#endif // BT_CLIENT

} // namespace game
