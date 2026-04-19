#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderLightingEffectsLightingTab()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kLighting);

	if (ImGui::BeginTable("LightingEffectsLightingColumns", 2))
	{
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary");
		WrapperSlider("Lighting Area One", kiSection, 1.0f, "Explosion Primary Lighting Area One");
		WrapperSlider("Lighting Area Two", kiSection, 1.0f, "Explosion Primary Lighting Area Two");
		WrapperSlider("Lighting Area Three", kiSection, 1.0f, "Explosion Primary Lighting Area Three");
		WrapperSlider("Lighting Int One", kiSection, 1.0f, "Explosion Primary Lighting Intensity One");
		WrapperSlider("Lighting Int Two", kiSection, 1.0f, "Explosion Primary Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", kiSection, 1.0f, "Explosion Primary Lighting Intensity Three");

		WrapperSeparatorText("Explosions - Secondary");
		WrapperSlider("Lighting Area One", kiSection, 1.0f, "Explosion Secondary Lighting Area One");
		WrapperSlider("Lighting Area Two", kiSection, 1.0f, "Explosion Secondary Lighting Area Two");
		WrapperSlider("Lighting Area Three", kiSection, 1.0f, "Explosion Secondary Lighting Area Three");
		WrapperSlider("Lighting Int One", kiSection, 1.0f, "Explosion Secondary Lighting Intensity One");
		WrapperSlider("Lighting Int Two", kiSection, 1.0f, "Explosion Secondary Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", kiSection, 1.0f, "Explosion Secondary Lighting Intensity Three");

		WrapperSeparatorText("Explosions - Particle");
		WrapperSlider("Lighting Area", kiSection, 1.0f, "Explosion Particle Lighting Area");
		WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Explosion Particle Lighting Intensity");

		WrapperSeparatorText("Blasters - Terrain Crater");
		WrapperSlider("Lighting Area One", kiSection, 1.0f, "Crater Lighting Area One");
		WrapperSlider("Lighting Area Two", kiSection, 1.0f, "Crater Lighting Area Two");
		WrapperSlider("Lighting Area Three", kiSection, 1.0f, "Crater Lighting Area Three");
		WrapperSlider("Lighting Area Four", kiSection, 1.0f, "Crater Lighting Area Four");
		WrapperSlider("Lighting Int One", kiSection, 1.0f, "Crater Lighting Intensity One");
		WrapperSlider("Lighting Int Two", kiSection, 1.0f, "Crater Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", kiSection, 1.0f, "Crater Lighting Intensity Three");
		WrapperSlider("Lighting Int Four", kiSection, 1.0f, "Crater Lighting Intensity Four");

		ImGui::TableNextColumn();

		WrapperSeparatorText("Players - Area Light");
		WrapperSlider("Lighting Size", kiSection, 1.0f, "Player Area Light Lighting Size");
		WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Player Area Light Lighting Intensity");

		WrapperSeparatorText("Players - Impact Light");
		WrapperSlider("Lighting Area One", kiSection, 1.0f, "Player Impact Lighting Area One");
		WrapperSlider("Lighting Area Two", kiSection, 1.0f, "Player Impact Lighting Area Two");
		WrapperSlider("Lighting Int One", kiSection, 1.0f, "Player Impact Lighting Intensity One");
		WrapperSlider("Lighting Int Two", kiSection, 1.0f, "Player Impact Lighting Intensity Two");

		WrapperSeparatorText("Players - Hex Shield");
		WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Hex Shield Lighting Intensity");

		WrapperSeparatorText("Missiles - Exhaust");
		WrapperSlider("Lighting Area", kiSection, 1.0f, "Missile Exhaust Lighting Area");
		WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Missile Exhaust Lighting Intensity");

		WrapperSeparatorText("Spaceships - Explosion");
		WrapperSlider("Intensity", kiSection, 1.0f, "Spaceship Explosion Intensity");

		WrapperSeparatorText("Spaceships - Enemy Blaster");
		WrapperSlider("Lighting Size", kiSection, 1.0f, "Enemy Blaster Lighting Size");
		WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Enemy Blaster Lighting Intensity");

		WrapperSeparatorText("Spaceships - Hit Flash");
		WrapperSlider("Lighting Area One", kiSection, 1.0f, "Hit Flash Lighting Area One");
		WrapperSlider("Lighting Area Two", kiSection, 1.0f, "Hit Flash Lighting Area Two");
		WrapperSlider("Lighting Int One", kiSection, 1.0f, "Hit Flash Lighting Intensity One");
		WrapperSlider("Lighting Int Two", kiSection, 1.0f, "Hit Flash Lighting Intensity Two");

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
