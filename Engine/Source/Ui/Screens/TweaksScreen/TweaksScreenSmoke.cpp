#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderSmokeSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSmoke);

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
		WrapperSlider("Smoke Lighting Multiplier", kiSection, 1.0f);

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
		WrapperSlider("Wind Smoke Advection", kiSection, 1.0f);

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

		WrapperSeparatorText("Puff Deposit Intensities");
		WrapperSlider("Explosion Primary Intensity One", kiSection, 1.0f, "Explosion Primary Puff Intensity One");
		WrapperSlider("Explosion Primary Intensity Two", kiSection, 1.0f, "Explosion Primary Puff Intensity Two");
		WrapperSlider("Explosion Secondary Intensity One", kiSection, 1.0f, "Explosion Secondary Puff Intensity One");
		WrapperSlider("Explosion Secondary Intensity Two", kiSection, 1.0f, "Explosion Secondary Puff Intensity Two");
		WrapperSlider("Blaster Intensity Start", kiSection, 1.0f, "Blaster Puff Intensity Start");
		WrapperSlider("Blaster Intensity End", kiSection, 1.0f, "Blaster Puff Intensity End");
		WrapperSlider("Player Impact Intensity One", kiSection, 1.0f, "Player Impact Puff Intensity One");
		WrapperSlider("Player Impact Intensity Two", kiSection, 1.0f, "Player Impact Puff Intensity Two");

		ImGui::EndTable();
	}
}

} // namespace engine

#endif // BT_CLIENT
