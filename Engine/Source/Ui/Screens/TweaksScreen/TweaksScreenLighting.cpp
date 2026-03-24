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

		WrapperSeparatorText("2. First Spread");
		WrapperSlider("First Spread Texture Multiplier", kiSection, 1.0f);
		WrapperSlider("First Spread Kernel World", kiSection, 1.0f);
		WrapperSlider("First Spread Kernel Size", kiSection, 1.0f);
		WrapperSlider("First Spread Decay", kiSection, 1.0f);
		WrapperSlider("Occupancy Dilation", kiSection, 1.0f);
		WrapperSlider("Spread Terrain Cutoff", kiSection, 1.0f);

		WrapperSeparatorText("3. Blur");
		WrapperSlider("Blur Texture Multiplier", kiSection, 1.0f);
		WrapperSlider("Blur Distance", kiSection, 1.0f);
		WrapperSlider("Blur Directionality", kiSection, 1.0f);
		WrapperSlider("Blur Jitter", kiSection, 1.0f);
		WrapperSlider("Downscale", kiSection, 1.0f);

		WrapperSeparatorText("4. Combine");
		WrapperSlider("Combine Texture Multiplier", kiSection, 1.0f);
		WrapperSlider("Combine Index", kiSection, 1.0f);
		WrapperSlider("Blur First Divisor", kiSection, 1.0f);
		WrapperSlider("Blur Divisor", kiSection, 1.0f);
		WrapperSlider("Combine Decay", kiSection, 1.0f);
		WrapperSlider("Combine Power", kiSection, 1.0f);

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

		ImGui::EndTable();
	}
}

} // namespace engine
