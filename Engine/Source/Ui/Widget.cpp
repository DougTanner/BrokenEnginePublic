#include "Widget.h"

#include "Graphics/Graphics.h"
#include "Ui/Wrapper.h"

#include "Game.h"

using namespace DirectX;

namespace engine
{

using enum WidgetFlags;

static int64_t siLinkId = 0;

Widget Toggle(std::u32string_view pcText, WidgetInfo&& rData)
{
	return HStack({.f2Size = {kfToggleWidth, kfSliderToggleHeight}, .Enabled = rData.Enabled},
	{
		Spacer(),
		HStack({.flags = {kFocus, kFocusOutline}, .f2Size = {0.4f * kfToggleWidth, kfSliderToggleHeight},
		.OnClick = [=](XMFLOAT2)
		{
			rData.pWrapper->Toggle();
		}},
		{
			Spacer({.f2Size = {0.25f * kfSliderToggleHeight / gpSwapchainManager->mfAspectRatio, kfSliderToggleHeight}}),
			Spacer({.flags = {kBackgroundTexture}, .f2Size = {kfSliderToggleHeight / gpSwapchainManager->mfAspectRatio, kfSliderToggleHeight}, .BackgroundTexture = [=]() { return rData.pWrapper->Get<bool>() ? data::kTexturesUiBC7CheckboxCheckedtgaCrc : data::kTexturesUiBC7CheckboxUncheckedtgaCrc; }}),
			Text(pcText, {.flags = {kMatchTextWidth, kCenterHorizontal}, .fTextSize = 0.5f, .uiTextColor = game::kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor}),
		}),
		Spacer(),
	});
}

Widget Slider(WidgetInfo&& rData)
{
	++siLinkId;

	rData.flags |= kFocus;
	rData.flags |= kSlider;
	rData.flags |= kCaptureMouse;
	rData.uiBackground = 0xFF0000FF;
	rData.f2Size = {0.4f, kfSliderToggleHeight};
	rData.iLinkId = siLinkId;
	rData.MouseIsDown = [=](XMFLOAT2 f2Position)
	{
		rData.pWrapper->SetPercent(f2Position.x);
	};

	return HStack({.Enabled = rData.Enabled},
	{
		Spacer(),
		Text(U"",
		{
			.flags = {}, .f2Size = {0.005f, kfSliderToggleHeight}, .fTextSize = 0.6f, .uiTextColor = game::kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor, .iLinkId = siLinkId,
			.Text = [=]()
			{
				std::string string = common::FromFloat(rData.pWrapper->Get(), 2);
				std::u32string wstring(string.begin(), string.end());
				static std::u32string sString; sString = wstring; return std::u32string_view(sString);
			}
		}),
		Spacer(),
		Widget(rData),
		Spacer(),
	});
}

Widget Slider(std::u32string_view pcText, WidgetInfo&& rData)
{
	++siLinkId;

	return VStack({.flags = kMatchChildHeight, .Enabled = rData.Enabled},
	{
		HStack({.f2Size = {0.0f, kfSliderToggleHeight}},
		{
			Spacer(),
			Text(pcText, {.flags = kMatchTextWidth, .f2Size = {0.01f, 0.0f}, .fTextSize = 0.5f, .uiTextColor = game::kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor, .iLinkId = siLinkId}),
			Spacer(),
		}),
		Slider({.flags = {rData.flags & kCaptureHides ? WidgetFlags_t {kCaptureMouse, kCaptureHides} : kCaptureMouse}, .f2Size = {0.0f, kfSliderToggleHeight}, .pWrapper = rData.pWrapper, .iLinkId = siLinkId}),
	});
}

XMFLOAT2 Widget::Size() const
{
	XMFLOAT2 f2Size = mInfo.Size ? mInfo.Size() : mInfo.f2Size;
	auto enabledChildren = mChildren | std::views::filter(widgetEnabled);

	if (mInfo.flags & kMatchTextWidth)
	{
		ASSERT(mInfo.eString != game::kStringsCount || mInfo.pcText.length() > 0 || mInfo.Text);
		std::u32string_view pcText = mInfo.eString != game::kStringsCount ? game::TranslatedString(mInfo.eString) : (mInfo.Text ? mInfo.Text() : mInfo.pcText);
		float fSize = mf4Rect.w * mInfo.fTextSize;
		std::vector<float> widths = gpTextManager->MeasureQuads(fSize, pcText);
		float fMaxWidth = *std::max_element(widths.begin(), widths.end());
		f2Size.x = mInfo.f2Size.x + fMaxWidth;
	}
	else if (mInfo.flags & kMatchChildWidth)
	{
		f2Size.x = 0.0f;
		for (const Widget& rChild : enabledChildren)
		{
			XMFLOAT2 f2ChildSize = rChild.Size();
			f2Size.x = std::max(f2ChildSize.x, f2Size.x);
		}
		ASSERT(f2Size.x > 0.0f);
		f2Size.x += mInfo.f4Border.x + mInfo.f4Border.z;
	}
	else if (mInfo.flags & kMatchChildHeight)
	{
		f2Size.y = 0.0f;
		for (const Widget& rChild : enabledChildren)
		{
			XMFLOAT2 f2ChildSize = rChild.Size();
			f2Size.y = std::max(f2ChildSize.y, f2Size.y);
		}
		ASSERT(f2Size.y > 0.0f);
		f2Size.y += mInfo.f4Border.y + mInfo.f4Border.w;
	}

	f2Size.x = std::max(0.0f, f2Size.x);
	f2Size.y = std::max(0.0f, f2Size.y);

	return f2Size;
}

std::u32string_view Widget::Text() const
{
	if (mInfo.eString != game::kStringsCount || mInfo.pcText.length() > 0 || mInfo.Text)
	{
		return mInfo.eString != game::kStringsCount ? game::TranslatedString(mInfo.eString) : (mInfo.Text ? mInfo.Text() : mInfo.pcText);
	}
	else
	{
		return std::u32string_view(nullptr, 0);
	}
}

void Widget::SetEnabled(bool bEnabled)
{
	mbEnabled = bEnabled ? mInfo.Enabled() : false;
	for (auto& rChild : mChildren)
	{
		rChild.SetEnabled(bEnabled && mbEnabled);
	}
}

void Widget::Input(const game::MenuInput& rMenuInput)
{
	if (rMenuInput.bGamepad)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kGamepadButton && mbFocused)
		{
			if (mInfo.flags & kCaptureMouse)
			{
				gpUiManager->mpCapturedWidget = gpUiManager->mpCapturedWidget == this ? nullptr : this;
			}
			else if (mInfo.OnClick)
			{
				mInfo.OnClick({mf4Rect.x + 0.5f * mf4Rect.z, mf4Rect.y + 0.5f * mf4Rect.w});
			}
		}

		if (gpUiManager->mpCapturedWidget == this && (rMenuInput.f2Gamepad.x > 0.0f || rMenuInput.f2Gamepad.x < 0.0f))
		{
			mInfo.pWrapper->SetPercent(mInfo.pWrapper->Percent() + 0.005f * rMenuInput.f2Gamepad.x);
		}
	}
	else if (!rMenuInput.bGamepad)
	{
		bool bMouseOver = rMenuInput.f2Mouse.x >= mf4Rect.x && rMenuInput.f2Mouse.x <= mf4Rect.x + mf4Rect.z && rMenuInput.f2Mouse.y >= mf4Rect.y && rMenuInput.f2Mouse.y <= mf4Rect.y + mf4Rect.w;
		if (rMenuInput.flags & game::MenuInputFlags::kMouseClick && bMouseOver)
		{
			if (mInfo.OnClick)
			{
				mInfo.OnClick({(rMenuInput.f2Mouse.x - mf4Rect.x) / (mf4Rect.z), (rMenuInput.f2Mouse.y - mf4Rect.y) / (mf4Rect.w)});
			}

			if (mInfo.flags & kCaptureMouse)
			{
				gpUiManager->mpCapturedWidget = this;
			}
		}

		bool bFocus = false;
		if (mInfo.flags & kFocus)
		{
			if (gpUiManager->mpCapturedWidget == nullptr)
			{
				bFocus = bMouseOver;
			}
			else if (gpUiManager->mpCapturedWidget == this)
			{
				bFocus = true;
			}
		}
		if (bFocus)
		{
			gpUiManager->Focus(this);
		}

		if (gpUiManager->mpCapturedWidget == nullptr || gpUiManager->mpCapturedWidget == this)
		{
			if (gpUiManager->mpCapturedWidget == this)
			{
				mInfo.MouseIsDown({std::clamp((rMenuInput.f2Mouse.x - mf4Rect.x) / mf4Rect.z, 0.0f, 1.0f), std::clamp((rMenuInput.f2Mouse.y - mf4Rect.y) / mf4Rect.w, 0.0f, 1.0f)});
			}
			else if (rMenuInput.flags & game::MenuInputFlags::kMouseIsDown && bMouseOver && mInfo.MouseIsDown)
			{
				mInfo.MouseIsDown({(rMenuInput.f2Mouse.x - mf4Rect.x) / (mf4Rect.z), (rMenuInput.f2Mouse.y - mf4Rect.y) / (mf4Rect.w)});
			}
		}
	}

	auto enabledChildren = mChildren | std::views::filter(widgetEnabled);
	for (Widget& rChild : enabledChildren)
	{
		rChild.Input(rMenuInput);
	}
}

void Widget::Layout(DirectX::XMFLOAT4& rParentRect)
{
	auto enabledChildren = mChildren | std::views::filter(widgetEnabled);

	if (mInfo.flags & kWidthEqualsHeight)
	{
		float fPreviousWidth = mf4Rect.z;
		mf4Rect.z = mf4Rect.w / gpSwapchainManager->mfAspectRatio;
		mf4Rect.x += 0.5f * (fPreviousWidth - mf4Rect.z);
	}

	mf4Rect.x += mInfo.f4Padding.x / gpSwapchainManager->mfAspectRatio;
	mf4Rect.y += mInfo.f4Padding.y;
	mf4Rect.z -= (mInfo.f4Padding.x + mInfo.f4Padding.z) / gpSwapchainManager->mfAspectRatio;
	mf4Rect.w -= mInfo.f4Padding.y + mInfo.f4Padding.w;

	if (mInfo.flags & kCenterHorizontal)
	{
		float fRemaining = rParentRect.z - mf4Rect.z;
		mf4Rect.x = rParentRect.x + 0.5f * fRemaining;
	}

	if (mInfo.flags & kCenterVertical)
	{
		float fRemaining = rParentRect.w - mf4Rect.w;
		mf4Rect.y = rParentRect.y + 0.5f * fRemaining;
	}

	if (mInfo.flags & kHStack)
	{
		float fSpacerCount = 0.0f;
		float fSpacersWidth = mf4Rect.z;

		for (const auto& rChild : enabledChildren)
		{
			if (rChild.mInfo.flags & kExcludeFromLayout)
			{
				continue;
			}

			XMFLOAT2 f2ChildSize = rChild.Size();
			if (f2ChildSize.x > 0.0f)
			{
				fSpacersWidth -= f2ChildSize.x;
			}
			else
			{
				fSpacerCount += 1.0f;
			}
		}
		if (fSpacerCount > 0.0f)
		{
			fSpacersWidth /= fSpacerCount;
		}

		float fCurrentX = mf4Rect.x;
		for (auto& rChild : enabledChildren)
		{
			if (rChild.mInfo.flags & kExcludeFromLayout)
			{
				rChild.mf4Rect = mf4Rect;
				continue;
			}

			XMFLOAT2 f2ChildSize = rChild.Size();
			float fChildWidth = f2ChildSize.x > 0.0f ? f2ChildSize.x : fSpacersWidth;
			float fChildHeight = f2ChildSize.y > 0.0f ? f2ChildSize.y : mf4Rect.w;
			rChild.mf4Rect = {fCurrentX, mf4Rect.y, fChildWidth, fChildHeight};
			fCurrentX += fChildWidth;
		}
	}
	else if (mInfo.flags & kVStack)
	{
		float fSpacerCount = 0.0f;
		float fSpacersHeight = mf4Rect.w;
		for (const Widget& rChild : enabledChildren)
		{
			if (rChild.mInfo.flags & kExcludeFromLayout)
			{
				continue;
			}

			XMFLOAT2 f2ChildSize = rChild.Size();
			if (f2ChildSize.y > 0.0f)
			{
				fSpacersHeight -= f2ChildSize.y;
			}
			else
			{
				fSpacerCount += 1.0f;
			}
		}
		if (fSpacerCount > 0.0f)
		{
			fSpacersHeight /= fSpacerCount;
		}

		float fCurrentY = mf4Rect.y;
		for (Widget& rChild : enabledChildren)
		{
			if (rChild.mInfo.flags & kExcludeFromLayout)
			{
				rChild.mf4Rect = mf4Rect;
				continue;
			}

			XMFLOAT2 f2ChildSize = rChild.Size();
			float fChildWidth = f2ChildSize.x > 0.0f ? f2ChildSize.x : mf4Rect.z;
			float fChildHeight = f2ChildSize.y > 0.0f ? f2ChildSize.y : fSpacersHeight;
			rChild.mf4Rect = {mf4Rect.x, fCurrentY, fChildWidth, fChildHeight};
			fCurrentY += fChildHeight;
		}
	}

	for (Widget& rChild : enabledChildren)
	{
		rChild.Layout(mf4Rect);
	}
}

bool Widget::FindFirstFocus(const game::MenuInput& rMenuInput)
{
	ASSERT(gpUiManager->mpFocusedWidget == nullptr);

	if (mInfo.flags & kFocus)
	{
		gpUiManager->Focus(this);
		return true;
	}

	auto enabledChildren = mChildren | std::views::filter(widgetEnabled);
	for (Widget& rChild : enabledChildren)
	{
		if (rChild.FindFirstFocus(rMenuInput))
		{
			return true;
		}
	}

	return false;
}

float FocusDistance(const game::MenuInput& rMenuInput, Widget* pFocusedWidget, Widget* pOtherWidget, bool bIgnoreBox)
{
	if (pOtherWidget == nullptr || pFocusedWidget == pOtherWidget)
	{
		return std::numeric_limits<float>::max();
	}

	bool bLeft = rMenuInput.f2Gamepad.x < -0.5f;
	bool bRight = rMenuInput.f2Gamepad.x > 0.5f;
	bool bUp = rMenuInput.f2Gamepad.y > 0.5f;
	bool bDown = rMenuInput.f2Gamepad.y < -0.5f;

	XMFLOAT2 f2FocusedCenter = pFocusedWidget->Center();
	XMFLOAT2 f2OtherCenter = pOtherWidget->Center();

	if (!bIgnoreBox)
	{
		if (bLeft || bRight)
		{
			if (f2OtherCenter.y < pFocusedWidget->mf4Rect.y || f2OtherCenter.y > pFocusedWidget->mf4Rect.y + pFocusedWidget->mf4Rect.w)
			{
				if (f2FocusedCenter.y < pOtherWidget->mf4Rect.y || f2FocusedCenter.y > pOtherWidget->mf4Rect.y + pOtherWidget->mf4Rect.w)
				{
					return std::numeric_limits<float>::max();
				}
			}
		}
		else
		{
			if (f2OtherCenter.x < pFocusedWidget->mf4Rect.x || f2OtherCenter.x > pFocusedWidget->mf4Rect.x + pFocusedWidget->mf4Rect.z)
			{
				if (f2FocusedCenter.x < pOtherWidget->mf4Rect.x || f2FocusedCenter.x > pOtherWidget->mf4Rect.x + pOtherWidget->mf4Rect.z)
				{
					return std::numeric_limits<float>::max();
				}
			}
		}
	}

	if (bLeft)
	{
		if (f2OtherCenter.x > pFocusedWidget->mf4Rect.x)
		{
			return std::numeric_limits<float>::max();
		}

		return pFocusedWidget->mf4Rect.x - f2OtherCenter.x;
	}
	else if (bRight)
	{
		if (f2OtherCenter.x < pFocusedWidget->mf4Rect.x + pFocusedWidget->mf4Rect.z)
		{
			return std::numeric_limits<float>::max();
		}

		return  f2OtherCenter.x - pFocusedWidget->mf4Rect.x;
	}
	else if (bUp)
	{
		if (f2OtherCenter.y > pFocusedWidget->mf4Rect.y)
		{
			return std::numeric_limits<float>::max();
		}

		return pFocusedWidget->mf4Rect.y - f2OtherCenter.y;
	}
	else if (bDown)
	{
		if (f2OtherCenter.y < pFocusedWidget->mf4Rect.y + pFocusedWidget->mf4Rect.w)
		{
			return std::numeric_limits<float>::max();
		}

		return f2OtherCenter.y - pFocusedWidget->mf4Rect.y;
	}

	return std::numeric_limits<float>::max();
}

void Widget::FocusSearch(const game::MenuInput& rMenuInput, Widget* pFocusedWidget, Widget*& rpOtherWidget, bool bIgnoreBox)
{
	ASSERT(pFocusedWidget != nullptr);

	if (mInfo.flags & kFocus)
	{
		float fCurrentDistance = FocusDistance(rMenuInput, pFocusedWidget, rpOtherWidget, bIgnoreBox);
		float fNewDistance = FocusDistance(rMenuInput, pFocusedWidget, this, bIgnoreBox);
		if (rpOtherWidget == nullptr && fNewDistance != std::numeric_limits<float>::max())
		{
			rpOtherWidget = this;
		}
		else if (fNewDistance < fCurrentDistance)
		{
			rpOtherWidget = this;
		}
	}

	auto enabledChildren = mChildren | std::views::filter(widgetEnabled);
	for (Widget& rChild : enabledChildren)
	{
		rChild.FocusSearch(rMenuInput, pFocusedWidget, rpOtherWidget, bIgnoreBox);
	}
}

void Widget::WriteUniformBuffer(shaders::WidgetLayout* pQuads, int64_t& riQuads) const
{
	if (!mInfo.Enabled())
	{
		return;
	}

	if (gpUiManager->mpCapturedWidget != nullptr && gpUiManager->mpCapturedWidget->mInfo.flags & kCaptureHides && gpUiManager->mpCapturedWidget != this && (gpUiManager->mpCapturedWidget->mInfo.iLinkId != mInfo.iLinkId))
	{
		for (const Widget& rWidget : mChildren)
		{
			rWidget.WriteUniformBuffer(pQuads, riQuads);
		}

		return;
	}

	float fAspectRatio = gpSwapchainManager->mfAspectRatio;

	if (mInfo.flags & kBackgroundTexture)
	{
		float fFocusSize = mbFocused && mInfo.flags & kFocusSize ? 0.02f * mf4Rect.w : 0.0f;

		uint32_t uiIndex = UiCrcToIndex(mInfo.BackgroundTexture());
		shaders::WidgetLayout* pFinalQuads = pQuads;
		int64_t& riFinalQuads = riQuads;

		ASSERT(riFinalQuads < shaders::kiMaxWidgets);
		pFinalQuads[riFinalQuads].f4VertexRect = {-1.0f + 2.0f * (mf4Rect.x - fFocusSize / fAspectRatio), 1.0f - 2.0f * (mf4Rect.y - fFocusSize), 2.0f * (mf4Rect.z + 2.0f * fFocusSize / fAspectRatio), -2.0f * (mf4Rect.w + 2.0f * fFocusSize)};
		pFinalQuads[riFinalQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
		pFinalQuads[riFinalQuads].ui4Misc = {0xFFFFFFFF, uiIndex, mInfo.flags & kRoundedEdges ? 3u : 0u, 0};
		++riFinalQuads;
	}

	if (mInfo.flags & kRotary)
	{
		RotaryInfo rotaryInfo = mInfo.RotaryInfo();

		float fAngleDelta = -XM_2PI / static_cast<float>(rotaryInfo.iTotal);
		for (int64_t i = 0; i < rotaryInfo.iTotal; ++i)
		{
			bool bActive = !(i < rotaryInfo.iTotal - rotaryInfo.iCurrent);
			if (!bActive && rotaryInfo.uiInactiveColor == 0x00000000)
			{
				continue;
			}

			auto vecOffset = XMVectorSet(rotaryInfo.fDistance, 0.0f, 0.0f, 1.0f);
			vecOffset = XMVector3Rotate(vecOffset, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fAngleDelta + static_cast<float>(i) * fAngleDelta));

			float fCenterX = mf4Rect.x + 0.5f * mf4Rect.z + XMVectorGetX(vecOffset) / fAspectRatio;
			float fCenterY = mf4Rect.y + 0.5f * mf4Rect.w + XMVectorGetY(vecOffset);

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {-1.0f + 2.0f * (fCenterX - 0.5f * rotaryInfo.fSize / fAspectRatio), 1.0f - 2.0f * (fCenterY - 0.5f * rotaryInfo.fSize), 2.0f * (rotaryInfo.fSize / fAspectRatio), -2.0f * (rotaryInfo.fSize)};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {bActive ? rotaryInfo.uiActiveColor : rotaryInfo.uiInactiveColor, UiCrcToIndex(data::kTexturesUiBC7CirclepngCrc), 0, 0};
			++riQuads;
		}
	}

	if (mInfo.flags & kSlider)
	{
		uint32_t uiBackgroundColor = mInfo.BackgroundColor ? mInfo.BackgroundColor() : mInfo.uiBackground;

		if (mbFocused)
		{
			float fFocusSize = 0.001f + 0.01f * mf4Rect.w;

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {-1.0f + 2.0f * (mf4Rect.x - fFocusSize), 1.0f - 2.0f * ((mf4Rect.y + 0.25f * mf4Rect.w) - fAspectRatio * fFocusSize), 2.0f * (mf4Rect.z + 2.0f * fFocusSize), -2.0f * ((0.5f * mf4Rect.w) + 2.0f * fAspectRatio * fFocusSize)};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {0xFFFFFFFF, 0, 1, 0};
			++riQuads;
		}

 		ASSERT(riQuads < shaders::kiMaxWidgets);
		pQuads[riQuads].f4VertexRect = {-1.0f + 2.0f * mf4Rect.x, 1.0f - 2.0f * (mf4Rect.y + 0.25f * mf4Rect.w), 2.0f * mf4Rect.z, -2.0f * (0.5f * mf4Rect.w)};
		pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
		pQuads[riQuads].ui4Misc = {uiBackgroundColor, 0, 1, 0};
		++riQuads;

		float fPercent = mInfo.pWrapper->Percent();
		float fSliderWidth = 0.02f * mf4Rect.z;
		float fX = std::clamp(mf4Rect.x + fPercent * mf4Rect.z - 0.5f * fSliderWidth, mf4Rect.x, mf4Rect.x + mf4Rect.z - fSliderWidth);
		ASSERT(riQuads < shaders::kiMaxWidgets);
		pQuads[riQuads].f4VertexRect = {-1.0f + 2.0f * fX, 1.0f - 2.0f * mf4Rect.y, 2.0f * fSliderWidth, -2.0f * mf4Rect.w};
		pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
		pQuads[riQuads].ui4Misc = {(~uiBackgroundColor) | 0x000000FF, 0, 1, 0};
		++riQuads;
	}
	else
	{
		if (mbFocused && mInfo.flags & kFocusOutline)
		{
			float fFocusSize = 0.001f + 0.005f * mf4Rect.w;

			float fLeft = -1.0f + 2.0f * mf4Rect.x;
			float fRight = fLeft + 2.0f * mf4Rect.z;
			float fTop = 1.0f - 2.0f * mf4Rect.y;
			float fBottom = fTop - 2.0f * mf4Rect.w;

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {fLeft - fFocusSize, fTop + fAspectRatio * fFocusSize, fFocusSize, -2.0f * (mf4Rect.w + fAspectRatio * fFocusSize)};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {0xFFFFFFFF, 0, 1, 0};
			++riQuads;

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {fLeft - fFocusSize, fTop + fAspectRatio * fFocusSize, 2.0f * (mf4Rect.z + fFocusSize), -fAspectRatio * fFocusSize};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {0xFFFFFFFF, 0, 1, 0};
			++riQuads;

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {fRight, fTop + fAspectRatio * fFocusSize, fFocusSize, -2.0f * (mf4Rect.w + fAspectRatio * fFocusSize)};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {0xFFFFFFFF, 0, 1, 0};
			++riQuads;

			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {fLeft - fFocusSize, fBottom, 2.0f * (mf4Rect.z + fFocusSize), -fAspectRatio * fFocusSize};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {0xFFFFFFFF, 0, 1, 0};
			++riQuads;
		}

		if ((mbFocused && mInfo.flags & kFocusBackground) || mInfo.flags & kBackground)
		{
			uint32_t uiBackgroundColor = mInfo.BackgroundColor ? mInfo.BackgroundColor() : mInfo.uiBackground;
			ASSERT(riQuads < shaders::kiMaxWidgets);
			pQuads[riQuads].f4VertexRect = {-1.0f + 2.0f * mf4Rect.x, 1.0f - 2.0f * mf4Rect.y, 2.0f * mf4Rect.z, -2.0f * mf4Rect.w};
			pQuads[riQuads].f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
			pQuads[riQuads].ui4Misc = {uiBackgroundColor, 0, 1, 0};
			++riQuads;
		}
	}

	std::u32string_view pcText = Text();
	if (pcText.size() > 0)
	{
		float fSize = mf4Rect.w * mInfo.fTextSize;

		if (mInfo.fShadowOffset != 0.0f)
		{
			float fShadowOffset = mInfo.fShadowOffset * fSize;
			std::vector<float> xOffsets = gpTextManager->MeasureQuads(fSize, pcText);
			for (float& rXOffset : xOffsets)
			{
				rXOffset = fShadowOffset + mf4Rect.x + (mInfo.flags & kTextAlignLeft ? 0.0f : 0.5f * (mf4Rect.z - rXOffset));
			}
			uint32_t uiShadowColor = mInfo.ShadowColor ? mInfo.ShadowColor() : mInfo.uiShadowColor;
			gpTextManager->WriteQuads(xOffsets, gpSwapchainManager->mfAspectRatio * fShadowOffset + mf4Rect.y + 0.5f * (mf4Rect.w - fSize), fSize, pcText, uiShadowColor, pQuads, riQuads, shaders::kiMaxWidgets);
		}

		std::vector<float> xOffsets = gpTextManager->MeasureQuads(fSize, pcText);
		for (float& rXOffset : xOffsets)
		{
			rXOffset = mf4Rect.x + (mInfo.flags & kTextAlignLeft ? 0.0f : 0.5f * (mf4Rect.z - rXOffset));
		}
		uint32_t uiTextColor = mInfo.TextColor ? mInfo.TextColor() : mInfo.uiTextColor;
		gpTextManager->WriteQuads(xOffsets, mf4Rect.y + 0.5f * (mf4Rect.w - fSize), fSize, pcText, uiTextColor, pQuads, riQuads, shaders::kiMaxWidgets);
	}

	for (const Widget& rWidget : mChildren)
	{
		rWidget.WriteUniformBuffer(pQuads, riQuads);
	}
}

} // namespace engine
