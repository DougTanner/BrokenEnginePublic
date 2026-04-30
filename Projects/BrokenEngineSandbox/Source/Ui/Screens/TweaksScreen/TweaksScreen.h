#pragma once

#if defined(BT_CLIENT)

namespace game
{

class TweaksScreen : public engine::TweaksScreenBase
{
public:

	TweaksScreen();

	void Render();

	void RenderHexShieldSection() override;
	void RenderWindDepositsSection() override;
	void RenderParticlesSection() override;
	void RenderLightingEffectsVisibleTab() override;
	void RenderLightingEffectsLightingTab() override;
};

} // namespace game

#endif // BT_CLIENT
