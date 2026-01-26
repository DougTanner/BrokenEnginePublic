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
	kWaterMedium,
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

	void RenderToggleBar();
	void RenderSectionWindow(TweakSection eSection);

	void RenderTestSection();
	void RenderGltfSection();
	void RenderTerrainSection();
	void RenderWaterSpecularSection();
	void RenderWaterLowSection();
	void RenderWaterMediumSection();
	void RenderLightingSection();
	void RenderWaterLightingSection();
	void RenderShadowSection();
	void RenderMiscSection();
	void RenderHexShieldSection();
	void RenderSmokeSection();

	void WrapperSlider(std::string_view label, int iSection);
	void WrapperSeparatorText(std::string_view label);

private:

	std::string_view mActiveSlider;
	int miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	std::array<bool, static_cast<size_t>(TweakSection::kCount)> mSectionVisible {};
	std::array<ImVec2, static_cast<size_t>(TweakSection::kCount)> mWindowPositions;
};

} // namespace engine
