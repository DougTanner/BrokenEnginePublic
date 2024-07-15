#include "UiManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/TextManager.h"
#include "Graphics/Managers/TextureManager.h"

#include "Game.h"
#include "Input/Input.h"
#include "Ui/Ui.h"

using namespace DirectX;

namespace engine
{

UiManager::UiManager()
{
	gpUiManager = this;

	mRootWidget = game::BuildUi();
	mRootWidget.mf4Rect = {0.0f, 0.0f, 1.0f, 1.0f};
}

UiManager::~UiManager()
{
	gpUiManager = nullptr;
}

void UiManager::Update(const game::MenuInput& rMenuInput)
{
	mRootWidget.SetEnabled();

	// Unfocus if disabled
	if (mpFocusedWidget != nullptr && !mpFocusedWidget->mbEnabled)
	{
		Focus(nullptr);
	}

	// Uncapture is disabled
	if (mpCapturedWidget != nullptr && !mpCapturedWidget->mbEnabled)
	{
		mpCapturedWidget = nullptr;
	}

	if (rMenuInput.bGamepad)
	{
		// Update focus for gamepad
		if (mpFocusedWidget == nullptr)
		{
			mRootWidget.FindFirstFocus(rMenuInput);
			mNextFocusTimer.Reset();
		}
		else if (mpCapturedWidget == nullptr && mNextFocusTimer.GetDeltaNs(false) > 200'000'000ns && (std::abs(rMenuInput.f2Gamepad.x) > 0.5f || std::abs(rMenuInput.f2Gamepad.y) > 0.5f))
		{
			mNextFocusTimer.Reset();

			Widget* pOtherWidget = nullptr;
			mRootWidget.FocusSearch(rMenuInput, mpFocusedWidget, pOtherWidget, false);
			if (pOtherWidget != nullptr)
			{
				Focus(pOtherWidget);
			}
			else
			{
				mRootWidget.FocusSearch(rMenuInput, mpFocusedWidget, pOtherWidget, true);
				if (pOtherWidget != nullptr)
				{
					Focus(pOtherWidget);
				}
			}
		}
	}
	else if (!(rMenuInput.flags & game::MenuInputFlags::kMouseIsDown))
	{
		// In mouse mode, release captured widget when mouse is no longer pressed
		mpCapturedWidget = nullptr;
	}
		
	mRootWidget.Input(rMenuInput);
	mRootWidget.Layout(mRootWidget.mf4Rect);
}

void UiManager::RenderMain(int64_t iCommandBuffer)
{
	auto pQuads = reinterpret_cast<shaders::WidgetLayout*>(gpBufferManager->mWidgetsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iQuads = 0;
	mRootWidget.WriteUniformBuffer(pQuads, iQuads);
	gpPipelineManager->mpPipelines[kPipelineWidgets].WriteIndirectBuffer(iCommandBuffer, iQuads);
}

void UiManager::Focus(Widget* pWidget)
{
	if (mpFocusedWidget != nullptr)
	{
		mpFocusedWidget->mbFocused = false;
	}

	mpFocusedWidget = pWidget;

	if (mpFocusedWidget != nullptr)
	{
		mpFocusedWidget->mbFocused = true;
	}
}

} // namespace engine
