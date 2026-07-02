#pragma once

namespace game
{

// UI scale factor for menu screens
inline constexpr float kfMenuUiScale = 2.0f;

// Heading scale multiplier on top of kfMenuUiScale for menu/panel titles
inline constexpr float kfMenuHeadingScale = 1.6f;

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

// Append UTF-32 string as null-terminated UTF-8 into the Workbuffer. Returns a move-only RAII handle that
// owns the workbuffer frame; the const char* it yields (via implicit conversion) is valid only for the
// handle's lifetime — pass it inline to the consuming ImGui call, never store it past the full-expression.
common::ScopedWorkbufferAllocation<char*> AppendUtf8(common::Workbuffer& rWorkbuffer, std::u32string_view u32String);

bool WrapperToggle(std::string_view label, engine::Wrapper* pWrapper);
bool WrapperSlider(std::string_view label, engine::Wrapper* pWrapper);
bool WrapperPlusMinus(std::string_view label, engine::Wrapper* pWrapper, float fStep);

#if defined(BT_CLIENT)

// RAII font push for menu text: selects the CJK font when the language needs it and sizes via the dynamic-font
// API (SetWindowFontScale is obsolete). fScale multiplies the pre-global-scale base size, so the user's
// gUiFontScale still applies exactly once on top. Construct either before Begin() (destructs after End()) or
// fully inside the window (destructs before End()) — a font pushed after Begin() and still on the stack at
// End() trips ImGui's per-window error recovery ("Missing PopFont()" force-pop, then the dtor double-pops).
class ScopedMenuFont
{
public:
	explicit ScopedMenuFont(float fScale = kfMenuUiScale);
	~ScopedMenuFont();
};

// Full-screen dim behind pause-style overlays (background draw list, behind all windows)
void DrawFullScreenDim();

// Layered vector panel: rounded fill, vertical gradient, border, top accent strip. Alphas compose with the
// user's UI opacity (gOpaqueUi/gUiOpacity). Call right after Begin() so widgets render on top.
void DrawPanelBackground(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax);

// Border + top accent strip only (no fill) — for opaque-WindowBg panels whose background must stay intact for
// RegisterOpaqueRect occlusion (HUD)
void DrawPanelAccents(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax);

// Custom-drawn menu button: InvisibleButton semantics (ID/click/keyboard/gamepad nav) with animated rounded
// fill, border, and left accent bar. rfHoverAnim is per-button state owned by the caller (screen member — no
// heap). Pass vSize x or y as 0.0f to auto-size that axis from the label.
bool MenuButton(const char* pcLabel, const ImVec2& vSize, float& rfHoverAnim, bool bSelected = false);

#endif // BT_CLIENT

} // namespace game
