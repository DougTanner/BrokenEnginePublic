#pragma once

namespace engine
{

enum class TweakSection : int
{
	kTest = 0,
	kGltf,
	kTerrain,
	kWaterSpecular,
	kWaterLow,
	kLighting,
	kWaterLighting,
	kShadow,
	kMisc,
	kHexShield,
	kSmoke,
	kCount
};

class TweaksScreen
{
public:

	TweaksScreen();
	~TweaksScreen() = default;

	void Render();

private:

	void RenderToggleBar();
	void RenderSectionWindow(TweakSection eSection);

	void RenderTestSection();
	void RenderGltfSection();
	void RenderTerrainSection();
	void RenderWaterSpecularSection();
	void RenderWaterLowSection();
	void RenderLightingSection();
	void RenderWaterLightingSection();
	void RenderShadowSection();
	void RenderMiscSection();
	void RenderHexShieldSection();
	void RenderSmokeSection();

	void WrapperSlider(std::string_view label, int iSection);

	const char* mpcActiveSlider = nullptr;
	int miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections
	ImVec2 mActiveSliderPos {};           // Screen position of slider when it became active
	ImVec2 mActiveSliderWindowOffset {};  // Offset from window position to slider position

	std::array<bool, static_cast<size_t>(TweakSection::kCount)> mSectionVisible {};
	std::array<ImVec2, static_cast<size_t>(TweakSection::kCount)> mWindowPositions;
};

} // namespace engine
