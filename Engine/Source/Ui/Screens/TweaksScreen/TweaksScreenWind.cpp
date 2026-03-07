#include "TweaksScreen.h"

#include "Game.h"

namespace engine
{

void TweaksScreen::RenderWindSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kWind);

	WrapperSeparatorText("Time & Global");
	WrapperSlider("Wind Time Scale", kiSection);
	WrapperSlider("Wind Threshold Low", kiSection);
	WrapperSlider("Wind Threshold High", kiSection);

	WrapperSeparatorText("Propagation");
	if (ImGui::BeginTable("WindPropagation", 2))
	{
		ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Decay Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Decay High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Momentum Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Momentum High", kiSection, 1.0f);

		ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion Low", kiSection, 1.0f);
		ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion High", kiSection, 1.0f);

		ImGui::EndTable();
	}

	WrapperSeparatorText("Particles");
	WrapperSlider("Particles Wind Strength", kiSection);
}

} // namespace engine
