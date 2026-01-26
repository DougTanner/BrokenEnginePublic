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

// Convert UTF-32 string to UTF-8 for ImGui
inline std::string ToUtf8(std::u32string_view u32str)
{
	std::string result;
	result.reserve(u32str.size() * 4);
	for (char32_t c : u32str)
	{
		if (c < 0x80)
		{
			result += static_cast<char>(c);
		}
		else if (c < 0x800)
		{
			result += static_cast<char>(0xC0 | (c >> 6));
			result += static_cast<char>(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000)
		{
			result += static_cast<char>(0xE0 | (c >> 12));
			result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			result += static_cast<char>(0x80 | (c & 0x3F));
		}
		else
		{
			result += static_cast<char>(0xF0 | (c >> 18));
			result += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			result += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return result;
}

inline bool WrapperToggle(std::string_view label, engine::Wrapper* pWrapper)
{
	bool bValue = pWrapper->Get<bool>();
	if (ImGui::Checkbox(label.data(), &bValue))
	{
		pWrapper->Set(bValue);
		return true;
	}
	return false;
}

inline bool WrapperSlider(std::string_view label, engine::Wrapper* pWrapper)
{
	float fValue = pWrapper->Get();
	if (ImGui::SliderFloat(label.data(), &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.2f"))
	{
		pWrapper->Set(fValue);
		return true;
	}
	return false;
}

} // namespace game
