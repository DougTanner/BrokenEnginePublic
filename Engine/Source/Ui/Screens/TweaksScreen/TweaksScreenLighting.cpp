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

		WrapperSeparatorText("1. Pre-Blur");
		WrapperSlider("Sigma", kiSection, 1.0f, "Lighting Blur Sigma");
		WrapperSlider("Sample Count", kiSection, 1.0f, "Lighting Blur Sample Count");
		WrapperSlider("Edge Falloff", kiSection, 1.0f, "Lighting Blur Edge Falloff");

		WrapperSeparatorText("2. Deposit");
		WrapperSlider("Texture Multiplier", kiSection, 1.0f, "Deposit Texture Multiplier");

		WrapperSeparatorText("3a. Spread - Pixel Multiplier");
		WrapperSlider("Texture Multiplier", kiSection, 1.0f, "Spread Texture Multiplier");
		WrapperSlider("Pass Count", kiSection, 1.0f, "Spread Pass Count");

		WrapperSeparatorText("3b. Spread Start");
		WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality");
		WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count");
		WrapperSlider("Distance", kiSection, 1.0f, "Spread Distance");
		WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count");
		WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter");
		WrapperSlider("Decay", kiSection, 1.0f, "Spread Decay");
		WrapperSlider("Accumulation Decay", kiSection, 1.0f, "Spread Accumulation Decay");

		WrapperSeparatorText("3c. Spread End");
		WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality End");
		WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count End");
		WrapperSlider("Distance", kiSection, 1.0f, "Spread Distance End");
		WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count End");
		WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter End");
		WrapperSlider("Decay", kiSection, 1.0f, "Spread Decay End");
		WrapperSlider("Accumulation Decay", kiSection, 1.0f, "Spread Accumulation Decay End");

		WrapperSeparatorText("3d. Spread Height");
		WrapperSlider("Height Distance", kiSection, 1.0f, "Spread Height Distance");
		WrapperSlider("Height Intensity", kiSection, 1.0f, "Spread Height Intensity");

		// Right column
		ImGui::TableNextColumn();

		WrapperSeparatorText("4. Combine");
		WrapperSlider("Exposure", kiSection, 1.0f, "Combine Exposure");
		WrapperSlider("Power", kiSection, 1.0f, "Combine Power");
		WrapperSlider("Pass Normalize", kiSection, 1.0f, "Combine Pass Normalize");
		WrapperSlider("Exposure Pass Scale", kiSection, 1.0f, "Combine Exposure Pass Scale");
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
		WrapperSlider("New Directional Power", kiSection, 1.0f);
		WrapperSlider("New Ambient", kiSection, 1.0f);
		WrapperSlider("New Ambient Power", kiSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace engine
