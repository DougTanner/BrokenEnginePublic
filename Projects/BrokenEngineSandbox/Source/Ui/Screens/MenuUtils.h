#pragma once

namespace game
{

// UI scale factor for menu screens
inline constexpr float kfMenuUiScale = 2.0f;

// Heading scale multiplier on top of kfMenuUiScale for menu/panel titles
inline constexpr float kfMenuHeadingScale = 1.6f;
inline constexpr float kfMainMenuHeadingScale = 1.3f;

// Main-menu utility footer scale: absolute font scale plus a multiplier on the already menu-scaled geometry
inline constexpr float kfLanguageMenuFontScale = 1.0f;
inline constexpr float kfLanguageMenuGeometryScale = 0.5f;

// Shared menu-layout constants (source of truth: Documents/UserInterfaceDesign.txt). No inline magic layout
// literal belongs in a screen .cpp — every anchor/gap/min-width lives here. Two kinds per the sizing standard:
// DisplaySize fractions (content-independent anchors/extents) and 4K-authored
// pixel constants multiplied by engine::UiScale() at each use site.
//
// Anchors / extents as DisplaySize fractions:
inline constexpr float kfMainMenuCenterFractionX = 1.0f / 3.0f; // MainMenu group center on first vertical third
inline constexpr float kfMainMenuCenterFractionY = 0.5f;        // MainMenu group anchored at semantic screen center
inline constexpr float kfHudEdgeMarginFraction = 0.05f;        // HUD panel inset from the screen edge
inline constexpr float kfHudPanelTopFraction = 0.125f;         // HUD panel top edge
inline constexpr float kfHudPanelMaxHeightFraction = 0.75f;    // HUD panel height cap + fixed hover-zone extent
inline constexpr float kfGraphicsMaxHeightFraction = 0.9f;     // Graphics settings-panel height cap
inline constexpr float kfModalWidthFraction = 0.35f;           // Modal TextWrapped wrap width
inline constexpr float kfModalAnchorFractionY = 0.4f;          // Modal center Y, seated slightly above screen center
inline constexpr float kfSettingsPanelWidthFraction = 0.6f;    // Graphics settings-panel width (Sound auto-resizes)
// 4K-authored pixels (multiply by engine::UiScale() at use):
inline constexpr float kfHeadingGapPixels = 22.0f;             // Gap below a MenuHeading
inline constexpr float kfMainMenuOpticalOffsetYPixels = 20.0f; // Screenshot-derived upward correction for visible title/action composition
inline constexpr float kfSectionGapPixels = 65.0f;             // Gap between major sections
inline constexpr float kfScreenBottomMarginPixels = 44.0f;     // Margin above a screen's bottom edge
inline constexpr float kfPrimaryButtonMinWidthPixels = 760.0f; // Menu primary-button minimum width
inline constexpr float kfModalButtonMinWidthPixels = 380.0f;   // Modal button minimum width

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

// RAII font push for menu text: sizes the default Latin/CJK font via the dynamic-font API
// (SetWindowFontScale is obsolete). fScale multiplies the pre-global-scale base size, so the user's
// gUiFontScale still applies exactly once on top. Construct either before Begin() (destructs after End()) or
// fully inside the window (destructs before End()) — a font pushed after Begin() and still on the stack at
// End() trips ImGui's per-window error recovery ("Missing PopFont()" force-pop, then the dtor double-pops).
class ScopedMenuFont
{
public:
	explicit ScopedMenuFont(float fScale = kfMenuUiScale);
	~ScopedMenuFont();
};

// Shared text-driven button width for a screen: max CalcTextSize over the given labels (measured under the live
// pushed font — localization-proof) plus FramePadding.x * 4. Labels are UTF-32 views (pass TranslatedString(...)
// results); each is UTF-8 encoded through the Workbuffer for measurement. Screens clamp the result up to a
// k*MinWidthPixels * UiScale() floor at the call site. Must be called inside the window with the menu font pushed.
float MenuButtonsWidth(std::initializer_list<std::u32string_view> aLabels);

// One menu/panel heading: pushes the heading font (kfMenuUiScale * fHeadingScale), emits pcLabel, then a
// standard kfHeadingGapPixels * UiScale() gap below it. The default preserves settings/pause heading scale.
void MenuHeading(const char* pcLabel, float fHeadingScale = kfMenuHeadingScale);

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
