#pragma once

#include "Ui/Localization.h"
#include "Ui/Wrapper.h"
#include "Ui/Ui.h"

namespace game
{

struct MenuInput;

}

namespace engine
{

enum class WidgetFlags : uint64_t
{
	kHStack = 0x0000001,
	kVStack = 0x0000002,
	kSlider = 0x0000004,

	kCenterHorizontal  = 0x0000008,
	kCenterVertical    = 0x0000010,
	kMatchChildWidth   = 0x0000020,
	kMatchChildHeight  = 0x0000040,
	kMatchTextWidth    = 0x0000080,
	kExcludeFromLayout = 0x0000100,

	kFocus              = 0x0000200,
	kFocusOutline       = 0x0000400,
	kFocusBackground    = 0x0000800,
	kFocusSize          = 0x0001000,
	kCaptureMouse       = 0x0002000,
	kCaptureHides       = 0x0004000,
	kIgnoreCaptureHides = 0x0008000,
	kOverHides          = 0x0010000,

	kBackground        = 0x00020000,
	kBackgroundTexture = 0x00040000,

	kRotary            = 0x00080000,

	kTextAlignLeft     = 0x00100000,
	
	kRoundedEdges      = 0x00200000,

	kWidthEqualsHeight = 0x00400000,
	// kHeightEqualsWidth = 0x00400000,
};
using WidgetFlags_t = common::Flags<WidgetFlags>;

struct RotaryInfo
{
	int64_t iTotal = 0;
	int64_t iCurrent = 0;

	float fDistance = 0.0f;
	float fSize = 0.0f;

	uint32_t uiActiveColor = 0xFFFFFFFF;
	uint32_t uiInactiveColor = 0x00000000;
};

struct WidgetInfo
{
	WidgetFlags_t flags;

	DirectX::XMFLOAT2 f2Size {0.0f, 0.0f};
	DirectX::XMFLOAT4 f4Padding {0.0f, 0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT4 f4Border {0.0f, 0.0f, 0.0f, 0.0f};

	uint32_t uiBackground = 0x000000FF;
	float fTextSize = 1.0f;
	uint32_t uiTextColor = 0xFFFFFFFF;

	float fShadowOffset = 0.0f;
	uint32_t uiShadowColor = 0x00000000;

	game::Strings eString = game::kStringsCount;
	std::u32string_view pcText;

	Wrapper* pWrapper = nullptr;
	int64_t iLinkId = 0;

	std::function<bool()> Enabled = [](){ return true; };
	std::function<DirectX::XMFLOAT2()> Size;
	std::function<void(DirectX::XMFLOAT2 f2Position)> OnClick;
	std::function<void(DirectX::XMFLOAT2 f2Position)> MouseIsDown;
	std::function<uint32_t()> BackgroundColor;
	std::function<common::crc_t()> BackgroundTexture;
	std::function<std::u32string_view()> Text;
	std::function<uint32_t()> TextColor;
	std::function<uint32_t()> ShadowColor;
	std::function<RotaryInfo()> RotaryInfo;
};

template<typename T>
T& Value(void* pValue)
{
	return *reinterpret_cast<T*>(pValue);
}

class Widget
{
public:

	Widget() = default;

	Widget(WidgetInfo& rInfo, std::vector<Widget>&& rChildren = std::vector<Widget>())
	: mInfo(rInfo)
	, mChildren(std::move(rChildren))
	{
		if (mInfo.flags & WidgetFlags::kFocusBackground || mInfo.flags & WidgetFlags::kFocusOutline || mInfo.flags & WidgetFlags::kFocusSize)
		{
			mInfo.flags |= WidgetFlags::kFocus;
		}
	}

	~Widget() = default;

	DirectX::XMFLOAT2 Size() const;
	std::u32string_view Text() const;

	void SetEnabled(bool bEnabled = true);
	void Input(const game::MenuInput& rMenuInput);
	void Layout(DirectX::XMFLOAT4& rParentRect);

	bool FindFirstFocus(const game::MenuInput& rMenuInput);
	void FocusSearch(const game::MenuInput& rMenuInput, Widget* pFocusedWidget, Widget*& rpOtherWidget, bool bIgnoreBox);

	void WriteUniformBuffer(shaders::WidgetLayout* pQuads, int64_t& riQuads) const;

	DirectX::XMFLOAT2 Center() const
	{
		return {mf4Rect.x + 0.5f * mf4Rect.z, mf4Rect.y + 0.5f * mf4Rect.w};
	}

	WidgetInfo mInfo;
	std::vector<Widget> mChildren;

	DirectX::XMFLOAT4 mf4Rect {}; // Left, Top, Width, Height
	bool mbEnabled = false;
	bool mbFocused = false;
};

inline auto widgetEnabled = [](const Widget& rWidget) { return rWidget.mbEnabled; };

inline Widget HStack(WidgetInfo&& rData = {}, std::vector<Widget>&& rChildren = {}) { rData.flags |= WidgetFlags::kHStack; return Widget(rData, std::move(rChildren)); }
inline Widget VStack(WidgetInfo&& rData = {}, std::vector<Widget>&& rChildren = {}) { rData.flags |= WidgetFlags::kVStack; return Widget(rData, std::move(rChildren)); }
inline Widget Spacer(WidgetInfo&& rData = {}) { return Widget(rData); }

inline Widget Text(WidgetInfo&& rData = {}) { return Widget(rData); }

inline Widget Text(game::Strings eString, WidgetInfo&& rData = {})
{
	rData.eString = eString;
	return Widget(rData);
}

inline Widget Text(std::u32string_view pcText, WidgetInfo&& rData = {})
{
	rData.pcText = pcText;
	return Widget(rData);
}

inline Widget Button(game::Strings eString, WidgetInfo&& rData = {})
{
	rData.flags |= WidgetFlags::kFocus;
	rData.eString = eString;
	return Widget(rData);
}

inline Widget Button(std::u32string_view pcText, WidgetInfo&& rData = {})
{
	rData.flags |= WidgetFlags::kFocus;
	rData.pcText = pcText;
	return Widget(rData);
}

inline Widget Rotary(WidgetInfo&& rData = {})
{
	rData.flags |= WidgetFlags::kRotary;
	return Widget(rData);
}

Widget Toggle(std::u32string_view pcText, WidgetInfo&& rData = {});
Widget Slider(WidgetInfo&& rData = {});
Widget Slider(std::u32string_view pcText, WidgetInfo&& rData = {});

inline constexpr float kfToggleWidth = 0.5f;
inline constexpr float kfSliderToggleHeight = 0.045f;

template<typename T>
Widget RadioButtons(std::vector<std::u32string_view>&& rTexts, WidgetInfo&& rData, Widget* pCustom = nullptr)
{
	static constexpr uint32_t kuiRadioButtonSelected = 0xDDDDDDFF;
	static constexpr uint32_t kuiRadioButtonUnselected = 0x444444FF;

	std::vector<Widget> radioButtons;
	radioButtons.push_back(Spacer());
	radioButtons.push_back(Spacer({.f2Size = {0.25f * rData.f2Size.x, 0.0f}}));
	for (int64_t i = 0; i < static_cast<int64_t>(rTexts.size()); ++i)
	{
		WidgetFlags_t flags {WidgetFlags::kFocus, WidgetFlags::kFocusOutline, WidgetFlags::kBackground};
		if (rData.flags & WidgetFlags::kMatchTextWidth)
		{
			flags |= WidgetFlags::kMatchTextWidth;
		}

		radioButtons.push_back(Text(rTexts[i], {.flags = flags, .f2Size = rData.f2Size, .fTextSize = 0.5f, .uiTextColor = game::kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
		.OnClick = [=](DirectX::XMFLOAT2 f2Position)
		{
			rData.pWrapper->SetIndex(i);

			if (rData.OnClick)
			{
				rData.OnClick(f2Position);
			}
		},
		.BackgroundColor = [=]()
		{
			return rData.pWrapper->GetIndex() == i ? kuiRadioButtonSelected : kuiRadioButtonUnselected;
		}}));

		radioButtons.push_back(Spacer({.f2Size = {0.25f * rData.f2Size.x, 0.0f}}));
	}

	if (pCustom != nullptr)
	{
		radioButtons.push_back(*pCustom);
		radioButtons.push_back(Spacer({.f2Size = {0.25f * rData.f2Size.x, 0.0f}}));
	}

	radioButtons.push_back(Spacer());

	return HStack({.flags = rData.flags & WidgetFlags::kCenterVertical ? WidgetFlags_t {WidgetFlags::kCenterVertical} : WidgetFlags_t {}, .f2Size = {0.0f, kfSliderToggleHeight}, .uiBackground = 0x00000000}, std::move(radioButtons));
}

} // namespace engine
