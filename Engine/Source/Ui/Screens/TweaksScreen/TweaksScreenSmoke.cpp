#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderSmokeSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kSmoke);

	if (ImGui::BeginTable("SmokeColumns", 2))
	{
		// Left column
		ImGui::TableNextColumn();

		WrapperSeparatorText("Decay");
		WrapperSlider("Smoke Max", kiSection, 1.0f);
		WrapperSlider("Smoke Power", kiSection, 1.0f);
		WrapperSlider("Smoke Decay", kiSection, 1.0f);
		WrapperSlider("Smoke Edge Decay Distance", kiSection, 1.0f);

		WrapperSeparatorText("Color");
		WrapperSlider("Smoke Color Min", kiSection, 1.0f);
		WrapperSlider("Smoke Color Multiplier", kiSection, 1.0f);

		WrapperSeparatorText("Noise");
		WrapperSlider("Smoke Noise Scale One", kiSection, 1.0f);
		WrapperSlider("Smoke Noise Scale Two", kiSection, 1.0f);
		WrapperSlider("Smoke Wind Noise Scale", kiSection, 1.0f);
		WrapperSlider("Smoke Noise Quantity", kiSection, 1.0f);
		WrapperSlider("Smoke Wind Noise Quantity", kiSection, 1.0f);

		WrapperSeparatorText("Wind Displacement");
		WrapperSlider("Wind To Smoke Strength", kiSection, 1.0f);
		WrapperSlider("Wind To Smoke Power", kiSection, 1.0f);
		WrapperSlider("Wind Displacement Noise Scale", kiSection, 1.0f);
		WrapperSlider("Wind Smoke Retention", kiSection, 1.0f);

		WrapperSlider("Smoke Object Height", kiSection, 1.0f);

		// Right column
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

} // namespace engine
