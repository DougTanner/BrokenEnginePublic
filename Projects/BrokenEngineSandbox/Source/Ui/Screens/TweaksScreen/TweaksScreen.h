#pragma once

namespace game
{

class TweaksScreen : public engine::TweaksScreenBase
{
public:

	TweaksScreen();

	void Render();

	void RenderHexShieldSection() override;
	void RenderWindDepositsSection() override;
};

} // namespace game
