#pragma once

#include "Ui/Localization.h"

namespace game
{

class GameSettingsScreen
{
public:

	void Render();

private:

	float mfLanguageHoverAnims[kLanguageCount] {};
	float mfDefaultsHoverAnim = 0.0f;
	float mfBackHoverAnim = 0.0f;
};

} // namespace game
