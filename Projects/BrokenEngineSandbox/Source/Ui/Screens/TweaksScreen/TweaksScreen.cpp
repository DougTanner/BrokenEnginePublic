#include "TweaksScreen.h"

#include "Game.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::Render()
{
	if constexpr (kbDebugInput)
	{
		if (!game::gpGame->mbShowImGui)
		{
			return;
		}
	}

	TweaksScreenBase::Render();
}

} // namespace game

#endif // BT_CLIENT
