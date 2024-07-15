#pragma once

#include "Graphics/Objects/Texture.h"
#include "Ui/Localization.h"

#include "Widget.h"

namespace engine
{

class UiManager
{
public:

	UiManager();
	~UiManager();

	void Update(const game::MenuInput& rMenuInput);
	void RenderMain(int64_t iCommandBuffer);
	void Focus(Widget* pWidget);

	Widget* mpCapturedWidget = nullptr;
	Widget* mpFocusedWidget = nullptr;

private:

	Widget mRootWidget {};
	common::Timer mNextFocusTimer;
};

inline UiManager* gpUiManager = nullptr;

} // namespace engine
