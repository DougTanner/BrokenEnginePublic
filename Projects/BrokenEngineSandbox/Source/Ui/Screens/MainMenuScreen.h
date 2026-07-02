#pragma once

#include "Ui/Localization.h"

namespace game
{

class MainMenuScreen
{
public:

	void Render();

private:

	// Local Server / Remote Server / Graphics / Sound / Quit
	static constexpr int64_t kiMenuButtonCount = 5;

	float mfButtonHoverAnims[kiMenuButtonCount] {};
	float mfLanguageHoverAnims[kLanguageCount] {};
};

} // namespace game
