#pragma once

#include "WrapperBase.h"

namespace engine
{

// UI color theme, selectable in the game's settings menu. Values are persisted (ClientSettings GameSettings) — append only.
enum class UiTheme : uint8_t
{
	kNavalSteel = 0,
	kDarkAmber,
	kMidnightMauve,

	kCount,
};

extern Wrapper gFullscreen;
extern Wrapper gPresentMode;
extern Wrapper gMultisampling;
extern Wrapper gSampleCount;
extern Wrapper gAnisotropy;
extern Wrapper gMaxAnisotropy;
extern Wrapper gSampleShading;
extern Wrapper gMinSampleShading;
extern Wrapper gMipLodBias;
extern Wrapper gWaterShapeDetail;
extern Wrapper gSmokeSimulationPixels;
extern Wrapper gSmokeSimulationArea;
extern Wrapper gOpaqueUi;
extern Wrapper gUiOpacity;
extern Wrapper gUiFontScale;
extern Wrapper gUiTheme;
extern Wrapper gSmokeEnabled;
extern Wrapper gWindEnabled;
extern Wrapper gSunAngleOverride;

// Clamps because the backing value crosses a trust boundary (persisted GraphicsSettings.bin) and Wrapper::Set<T> soft-falls without clamping
inline UiTheme GetUiTheme()
{
	return static_cast<UiTheme>(std::clamp(gUiTheme.Get<int64_t>(), 0ll, static_cast<int64_t>(UiTheme::kCount) - 1));
}

} // namespace engine
