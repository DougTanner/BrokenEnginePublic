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
	void RenderLightingEffectsTab() override;
};

} // namespace game

#endif // BT_CLIENT
