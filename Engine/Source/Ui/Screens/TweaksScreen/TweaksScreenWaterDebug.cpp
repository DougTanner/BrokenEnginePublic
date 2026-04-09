#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderWaterDebugSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterDebug);

	if (ImGui::BeginTable("WaterDebugColumns", 2))
	{
		// Left column: Vertex shader offsets
		ImGui::TableNextColumn();

		WrapperSeparatorText("Vertex Shader");
		WrapperSlider("Low Wave Offset", kiSection, 1.0f, "Water Debug Low Wave Offset");
		WrapperSlider("Medium Wave Offset", kiSection, 1.0f, "Water Debug Medium Wave Offset");

		// Right column: Fragment shader offsets
		ImGui::TableNextColumn();

		WrapperSeparatorText("Fragment Shader");
		WrapperSlider("Normal One Offset", kiSection, 1.0f, "Water Debug Normal One Offset");
		WrapperSlider("Normal Two Offset", kiSection, 1.0f, "Water Debug Normal Two Offset");
		WrapperSlider("Noise Offset", kiSection, 1.0f, "Water Debug Noise Offset");

		ImGui::EndTable();
	}
}

} // namespace engine

#endif // BT_CLIENT
