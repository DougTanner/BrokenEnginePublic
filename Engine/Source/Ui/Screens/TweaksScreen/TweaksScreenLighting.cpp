#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderLightingSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kLighting);

	if (ImGui::BeginTable("LightingColumns", 2))
	{
		// Left column: pipeline phases
		ImGui::TableNextColumn();

		WrapperSeparatorText("1. Deposit");
		WrapperSlider("Deposit Texture Multiplier", kiSection, 1.0f);
		WrapperSlider("Deposit Energy Normalize", kiSection, 1.0f);

		WrapperSeparatorText("2. Spread");
		WrapperSlider("Spread Directionality", kiSection, 1.0f);
		WrapperSlider("Spread Direction Count", kiSection, 1.0f);
		WrapperSlider("Spread Texture Multiplier", kiSection, 1.0f);
		WrapperSlider("Spread Distance", kiSection, 1.0f);
		WrapperSlider("Spread Ring Count", kiSection, 1.0f);
		WrapperSlider("Spread Jitter", kiSection, 1.0f);
		WrapperSlider("Spread Decay", kiSection, 1.0f);
		WrapperSlider("Spread Pass Count", kiSection, 1.0f);

		WrapperSeparatorText("3. Combine");
		WrapperSlider("Combine Exposure", kiSection, 1.0f);
		WrapperSlider("Combine Power", kiSection, 1.0f);
		WrapperSlider("Combine Linear Clamp", kiSection, 1.0f);

		// Right column: reading from the lighting texture (how it's applied)
		ImGui::TableNextColumn();

		WrapperSeparatorText("Directional");
		WrapperSlider("Directional", kiSection, 1.0f);
		WrapperSlider("Indirect", kiSection, 1.0f);
		WrapperSlider("Terrain", kiSection, 1.0f);
		WrapperSlider("Terrain Add", kiSection, 1.0f);
		WrapperSlider("Objects", kiSection, 1.0f);
		WrapperSlider("Objects Add", kiSection, 1.0f);
		WrapperSlider("Time of Day Multiplier", kiSection, 1.0f);

		WrapperSeparatorText("New Lighting");
		WrapperSlider("New Directional", kiSection, 1.0f);
		WrapperSlider("New Ambient", kiSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace engine
