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

void RegisterGameTweakSections()
{
	// ImGuiManager owns the one and only TweaksScreenBase as a game::TweaksScreen, so the downcast always holds.
	engine::TweaksScreenBase::RegisterSection(giTweakSectionHexShield, {.displayName = "Hex Shield", .stableKey = "HexShield",
		.pfnRender = [](engine::TweaksScreenBase& rScreen) { static_cast<TweaksScreen&>(rScreen).RenderHexShieldSection(); }});
	engine::TweaksScreenBase::RegisterSection(giTweakSectionParticles, {.displayName = "Particles", .stableKey = "Particles",
		.pfnRender = [](engine::TweaksScreenBase& rScreen) { static_cast<TweaksScreen&>(rScreen).RenderParticlesSection(); }});
}

} // namespace game

#endif // BT_CLIENT
