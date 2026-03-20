#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderPbrSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kModel);

	if (ImGui::BeginTable("PbrColumns", 2))
	{
		// Left column
		ImGui::TableNextColumn();

		WrapperSeparatorText("Engine Variables");
		WrapperSlider("Sun", kiSection, 1.0f);

		WrapperSeparatorText("BRDF");
		WrapperSlider("BRDF Diffuse", kiSection, 1.0f);
		WrapperSlider("BRDF Diffuse Power", kiSection, 1.0f);
		WrapperSlider("BRDF Specular", kiSection, 1.0f);
		WrapperSlider("BRDF Specular Power", kiSection, 1.0f);

		WrapperSeparatorText("Tone Mapping");
		WrapperSlider("Exposure", kiSection, 1.0f);
		WrapperSlider("Gamma", kiSection, 1.0f);

		WrapperSeparatorText("Post Lighting");
		WrapperSlider("Lighting Specular", kiSection, 1.0f);
		WrapperSlider("Lighting Specular Power", kiSection, 1.0f);
		WrapperSlider("Lighting", kiSection, 1.0f);
		WrapperSlider("Lighting Power", kiSection, 1.0f);

		// Right column
		ImGui::TableNextColumn();

		WrapperSeparatorText("IBL");
		WrapperSlider("IBL Ambient", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse Power", kiSection, 1.0f);
		WrapperSlider("IBL Specular", kiSection, 1.0f);
		WrapperSlider("IBL Specular Power", kiSection, 1.0f);
		WrapperSlider("IBL Shadow Blend", kiSection, 1.0f);
		WrapperSlider("IBL Ambient Color Blend", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Power", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Offset", kiSection, 1.0f);
		WrapperSlider("Shadow Floor", kiSection, 1.0f);

		WrapperSeparatorText("Smoke");
		WrapperSlider("Smoke", kiSection, 1.0f);

		WrapperSeparatorText("Emissive");
		WrapperSlider("Emissive", kiSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace engine
