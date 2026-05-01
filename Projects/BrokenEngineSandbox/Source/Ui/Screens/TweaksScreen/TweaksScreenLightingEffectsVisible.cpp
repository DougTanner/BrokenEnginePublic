#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderLightingEffectsVisibleTab()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kLighting);

	if (ImGui::BeginTable("LightingEffectsVisibleColumns", 2))
	{
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary");
		WrapperSlider("Visible Area One", kiSection, 1.0f, "Explosion Primary Visible Area One");
		WrapperSlider("Visible Area Two", kiSection, 1.0f, "Explosion Primary Visible Area Two");
		WrapperSlider("Visible Area Three", kiSection, 1.0f, "Explosion Primary Visible Area Three");
		WrapperSlider("Visible Int One", kiSection, 1.0f, "Explosion Primary Visible Intensity One");
		WrapperSlider("Visible Int Two", kiSection, 1.0f, "Explosion Primary Visible Intensity Two");
		WrapperSlider("Visible Int Three", kiSection, 1.0f, "Explosion Primary Visible Intensity Three");

		WrapperSeparatorText("Explosions - Secondary");
		WrapperSlider("Visible Area One", kiSection, 1.0f, "Explosion Secondary Visible Area One");
		WrapperSlider("Visible Area Two", kiSection, 1.0f, "Explosion Secondary Visible Area Two");
		WrapperSlider("Visible Area Three", kiSection, 1.0f, "Explosion Secondary Visible Area Three");
		WrapperSlider("Visible Int One", kiSection, 1.0f, "Explosion Secondary Visible Intensity One");
		WrapperSlider("Visible Int Two", kiSection, 1.0f, "Explosion Secondary Visible Intensity Two");
		WrapperSlider("Visible Int Three", kiSection, 1.0f, "Explosion Secondary Visible Intensity Three");

		WrapperSeparatorText("Explosions - Primary Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Explosion Primary Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Explosion Primary Puff Area Two");

		WrapperSeparatorText("Explosions - Secondary Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Explosion Secondary Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Explosion Secondary Puff Area Two");

		WrapperSeparatorText("Blasters - Terrain Crater");
		WrapperSlider("Visible Area One", kiSection, 1.0f, "Crater Visible Area One");
		WrapperSlider("Visible Area Two", kiSection, 1.0f, "Crater Visible Area Two");
		WrapperSlider("Visible Area Three", kiSection, 1.0f, "Crater Visible Area Three");
		WrapperSlider("Visible Area Four", kiSection, 1.0f, "Crater Visible Area Four");
		WrapperSlider("Visible Int One", kiSection, 1.0f, "Crater Visible Intensity One");
		WrapperSlider("Visible Int Two", kiSection, 1.0f, "Crater Visible Intensity Two");
		WrapperSlider("Visible Int Three", kiSection, 1.0f, "Crater Visible Intensity Three");
		WrapperSlider("Visible Int Four", kiSection, 1.0f, "Crater Visible Intensity Four");

		WrapperSeparatorText("Blasters - Terrain Puff");
		WrapperSlider("Area Start", kiSection, 1.0f, "Blaster Puff Area Start");
		WrapperSlider("Area End", kiSection, 1.0f, "Blaster Puff Area End");

		ImGui::TableNextColumn();

		WrapperSeparatorText("Players - Area Light");
		WrapperSlider("Visible Intensity", kiSection, 1.0f, "Player Area Light Visible Intensity");

		WrapperSeparatorText("Players - Impact Light");
		WrapperSlider("Visible Area One", kiSection, 1.0f, "Player Impact Visible Area One");
		WrapperSlider("Visible Area Two", kiSection, 1.0f, "Player Impact Visible Area Two");
		WrapperSlider("Visible Int One", kiSection, 1.0f, "Player Impact Visible Intensity One");
		WrapperSlider("Visible Int Two", kiSection, 1.0f, "Player Impact Visible Intensity Two");

		WrapperSeparatorText("Players - Impact Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Player Impact Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Player Impact Puff Area Two");

		WrapperSeparatorText("Players - Hex Shield");
		WrapperSlider("Intensity Decay", kiSection, 1.0f, "Hex Shield Intensity Decay");

		WrapperSeparatorText("Missiles - Exhaust");
		WrapperSlider("Visible Intensity", kiSection, 1.0f, "Missile Exhaust Visible Intensity");

		WrapperSeparatorText("Missiles - Trail");
		WrapperSlider("Intensity", kiSection, 1.0f, "Missile Trail Intensity");

		WrapperSeparatorText("Spaceships - Enemy Blaster");
		WrapperSlider("Visible Intensity", kiSection, 1.0f, "Enemy Blaster Visible Intensity");

		WrapperSeparatorText("Spaceships - Hit Flash");
		WrapperSlider("Visible Area One", kiSection, 1.0f, "Hit Flash Visible Area One");
		WrapperSlider("Visible Area Two", kiSection, 1.0f, "Hit Flash Visible Area Two");
		WrapperSlider("Visible Int One", kiSection, 1.0f, "Hit Flash Visible Intensity One");
		WrapperSlider("Visible Int Two", kiSection, 1.0f, "Hit Flash Visible Intensity Two");

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
