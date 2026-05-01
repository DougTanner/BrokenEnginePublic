#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderSmokeDepositsTab()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kSmoke);

	if (ImGui::BeginTable("SmokeDepositsColumns", 3))
	{
		// Column 1: Explosions
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Explosion Primary Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Explosion Primary Puff Area Two");
		WrapperSlider("Intensity One", kiSection, 1.0f, "Explosion Primary Puff Intensity One");
		WrapperSlider("Intensity Two", kiSection, 1.0f, "Explosion Primary Puff Intensity Two");

		WrapperSeparatorText("Explosions - Secondary Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Explosion Secondary Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Explosion Secondary Puff Area Two");
		WrapperSlider("Intensity One", kiSection, 1.0f, "Explosion Secondary Puff Intensity One");
		WrapperSlider("Intensity Two", kiSection, 1.0f, "Explosion Secondary Puff Intensity Two");

		WrapperSeparatorText("Explosions - Primary Trail");
		WrapperSlider("Intensity", kiSection, 1.0f, "Explosion Primary Trail Intensity");
		WrapperSlider("Length", kiSection, 1.0f, "Explosion Primary Trail Length");
		WrapperSlider("Duration", kiSection, 1.0f, "Explosion Primary Trail Duration");

		WrapperSeparatorText("Explosions - Secondary Trail");
		WrapperSlider("Intensity", kiSection, 1.0f, "Explosion Secondary Trail Intensity");
		WrapperSlider("Length", kiSection, 1.0f, "Explosion Secondary Trail Length");
		WrapperSlider("Duration", kiSection, 1.0f, "Explosion Secondary Trail Duration");

		// Column 2: Blasters / Players / Missiles
		ImGui::TableNextColumn();

		WrapperSeparatorText("Blasters - Terrain Puff");
		WrapperSlider("Area Start", kiSection, 1.0f, "Blaster Puff Area Start");
		WrapperSlider("Area End", kiSection, 1.0f, "Blaster Puff Area End");
		WrapperSlider("Intensity Start", kiSection, 1.0f, "Blaster Puff Intensity Start");
		WrapperSlider("Intensity End", kiSection, 1.0f, "Blaster Puff Intensity End");

		WrapperSeparatorText("Players - Impact Puff");
		WrapperSlider("Area One", kiSection, 1.0f, "Player Impact Puff Area One");
		WrapperSlider("Area Two", kiSection, 1.0f, "Player Impact Puff Area Two");
		WrapperSlider("Intensity One", kiSection, 1.0f, "Player Impact Puff Intensity One");
		WrapperSlider("Intensity Two", kiSection, 1.0f, "Player Impact Puff Intensity Two");

		WrapperSeparatorText("Missiles - Trail");
		WrapperSlider("Intensity", kiSection, 1.0f, "Missile Trail Intensity");

		// Column 3: Engine smoke trail rendering params
		ImGui::TableNextColumn();

		WrapperSeparatorText("Trails");
		WrapperSlider("Smoke Trails Quantity", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Width Current", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Width Previous", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Length", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Length Jitter", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Side Jitter", kiSection, 1.0f);
		WrapperSlider("Smoke Intensity Falloff", kiSection, 1.0f);
		WrapperSlider("Smoke Trails Follow", kiSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
