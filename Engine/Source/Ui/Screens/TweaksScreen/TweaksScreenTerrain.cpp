#include "TweaksScreen.h"

#include "Game.h"

namespace engine
{

void TweaksScreen::RenderTerrainSection()
{
	WrapperSeparatorText("Beach");
	WrapperSlider("Snow Multiplier", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Height", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Size", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Blend", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 1", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 2", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 3", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Blend", static_cast<int>(TweakSection::kTerrain));

	WrapperSeparatorText("Rock");
	WrapperSlider("Island Height", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Multiplier", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Size", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Blend", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 1", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 2", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 3", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Blend", static_cast<int>(TweakSection::kTerrain));
}

} // namespace engine
