#pragma once

namespace game
{

// UI scale factor for menu screens
inline constexpr float kfMenuUiScale = 2.0f;

// Heading scale multiplier on top of kfMenuUiScale for menu/panel titles
inline constexpr float kfMenuHeadingScale = 1.6f;
inline constexpr float kfMainMenuHeadingScale = 1.3f;

// Language-row utility tier: absolute font scale plus a multiplier on the already menu-scaled geometry
inline constexpr float kfLanguageMenuFontScale = 1.0f;
inline constexpr float kfLanguageMenuGeometryScale = 0.5f;

// Shared menu-layout constants (source of truth: Documents/UserInterfaceDesign.txt). Values shared by multiple
// screens live here; screen-specific dimensions remain named constants in their screen .cpp. Two kinds per the
// sizing standard: DisplaySize fractions (content-independent anchors/extents) and 4K-authored pixel constants
// multiplied by engine::UiScale() at each use site.
//
// Anchors / extents as DisplaySize fractions:
inline constexpr float kfMainMenuCenterFractionX = 1.0f / 3.0f; // MainMenu group center on first vertical third
inline constexpr float kfMainMenuCenterFractionY = 0.5f;        // MainMenu group anchored at semantic screen center
inline constexpr float kfHudEdgeMarginFraction = 0.05f;        // HUD panel inset from the screen edge
inline constexpr float kfHudPanelTopFraction = 0.125f;         // HUD panel top edge
inline constexpr float kfHudPanelMaxHeightFraction = 0.75f;    // HUD panel height cap + fixed hover-zone extent
inline constexpr float kfGraphicsMaxHeightFraction = 0.9f;     // Graphics settings-panel height cap
inline constexpr float kfModalAnchorFractionY = 0.4f;          // Modal center Y, seated slightly above screen center
// 4K-authored pixels (multiply by engine::UiScale() at use):
inline constexpr float kfHeadingGapPixels = 22.0f;             // Gap below a MenuHeading
inline constexpr float kfMainMenuOpticalOffsetYPixels = 20.0f; // Screenshot-derived upward correction for visible title/action composition
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

// Float-backed RadioButton row: optional header text, then one RadioButton per option on a single line. The
// checked button is the option whose value equals the wrapper's current value; clicking one writes that value.
// One place for the enum<->index mapping of present mode, sample count, quality level, and theme — every Wrapper
// flavor is float-backed, so equality/Set on the raw float is exact for the discrete values used here. Rows are
// scoped by the wrapper address, so repeated option labels across rows keep distinct ImGui IDs. Returns true on
// the frame a radio was clicked.
// RadioButton and header labels are the harness automation API — do not rename.
bool RadioRow(const char* pcHeader, engine::Wrapper* pWrapper, float fCurrent, std::initializer_list<std::pair<const char*, float>> aOptions);

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
// standard kfHeadingGapPixels * UiScale() gap below it. Screens override the scale when their hierarchy requires it.
void MenuHeading(const char* pcLabel, float fHeadingScale = kfMenuHeadingScale);

// Full-screen dim behind pause-style overlays (background draw list, behind all windows)
void DrawFullScreenDim();

// Border + top accent strip only (no fill) — for panels whose own WindowBg must stay intact as the fill.
void DrawPanelAccents(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax);

// Custom-drawn menu button: InvisibleButton semantics (ID/click/keyboard/gamepad nav) with animated rounded
// fill, border, and left accent bar. rfHoverAnim is per-button state owned by the caller (screen member — no
// heap). Pass vSize x or y as 0.0f to auto-size that axis from the label.
bool MenuButton(const char* pcLabel, const ImVec2& vSize, float& rfHoverAnim, bool bSelected = false);

#endif // BT_CLIENT

} // namespace game
