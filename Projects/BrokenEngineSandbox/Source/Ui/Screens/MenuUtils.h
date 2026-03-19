#pragma once

namespace game
{

// UI scale factor for menu screens
inline constexpr float kfMenuUiScale = 2.0f;

// RAII helper for scaling UI elements
class ScopedMenuScale
{
public:
	ScopedMenuScale()
	{
		ImGuiStyle& rStyle = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(rStyle.FramePadding.x * kfMenuUiScale, rStyle.FramePadding.y * kfMenuUiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(rStyle.ItemSpacing.x * kfMenuUiScale, rStyle.ItemSpacing.y * kfMenuUiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(rStyle.WindowPadding.x * kfMenuUiScale, rStyle.WindowPadding.y * kfMenuUiScale));
	}

	~ScopedMenuScale()
	{
		ImGui::PopStyleVar(3);
	}
};

// Append UTF-32 string as null-terminated UTF-8 into Workbuffer (Push first, returns const char*)
const char* AppendUtf8(common::Workbuffer& rWorkbuffer, std::u32string_view u32str);

bool WrapperToggle(std::string_view label, engine::Wrapper* pWrapper);
bool WrapperSlider(std::string_view label, engine::Wrapper* pWrapper);

} // namespace game
