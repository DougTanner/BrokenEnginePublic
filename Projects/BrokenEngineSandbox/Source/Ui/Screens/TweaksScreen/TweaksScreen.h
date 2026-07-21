#pragma once

#if defined(BT_CLIENT)

namespace game
{

class TweaksScreen : public engine::TweaksScreenBase
{
public:

	void Render();

	// Whole game sections, reached through the thunks registered by RegisterGameTweakSections().
	void RenderHexShieldSection();
	void RenderParticlesSection();

	// Sub-section tabs owned by engine sections.
	void RenderSmokeDepositsTab() override;
	void RenderWindDepositsTab() override;
	void RenderLightingEffectsVisibleTab() override;
	void RenderLightingEffectsLightingTab() override;
	void RenderSoundEffects() override;
};

// Startup-assigned dense section indices, written by RegisterGameTweakSections(). engine::kiInvalidTweakSection until then.
inline int64_t giTweakSectionHexShield = engine::kiInvalidTweakSection;
inline int64_t giTweakSectionParticles = engine::kiInvalidTweakSection;

// Registers the game's tweaks sections. Called from Main.cpp immediately after engine::RegisterEngineTweakSections(),
// before the Graphics ctor builds ImGuiManager -> TweaksScreen.
void RegisterGameTweakSections();

} // namespace game

#endif // BT_CLIENT
