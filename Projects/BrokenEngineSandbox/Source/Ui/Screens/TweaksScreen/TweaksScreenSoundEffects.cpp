#include "TweaksScreen.h"

#include "Ui/SoundWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gSoundEffectsRegistrar
{
	// Blasters - Player
	{"Player Blaster Volume", &gPlayerBlasterVolume},
	{"Player Blaster Pitch Min", &gPlayerBlasterPitchMin},
	{"Player Blaster Pitch Random", &gPlayerBlasterPitchRandom},
	// Blasters - Enemy
	{"Enemy Blaster Volume", &gEnemyBlasterVolume},
	{"Enemy Blaster Pitch Min", &gEnemyBlasterPitchMin},
	{"Enemy Blaster Pitch Random", &gEnemyBlasterPitchRandom},
	// Blasters - Terrain Impact
	{"Terrain Impact Volume", &gTerrainImpactVolume},
	// Missiles
	{"Missile Launch Volume", &gMissileLaunchVolume},
	{"Missile Loop Volume", &gMissileLoopVolume},
	{"Missile Pitch Min", &gMissilePitchMin},
	{"Missile Pitch Random", &gMissilePitchRandom},
	// Explosions
	{"Explosion Volume", &gExplosionVolume},
	// Players - Shield
	{"Shield Hit Volume Base", &gShieldHitVolumeBase},
	{"Shield Hit Volume Scale", &gShieldHitVolumeScale},
	{"Shield Down Volume", &gShieldDownVolume},
	// Players - Armor
	{"Armor Hit Volume Base", &gArmorHitVolumeBase},
	{"Armor Hit Volume Scale", &gArmorHitVolumeScale},
	// Spaceships
	{"Spaceship Death Volume", &gSpaceshipDeathVolume},
	{"Spaceship Death Pitch Min", &gSpaceshipDeathPitchMin},
	{"Spaceship Death Pitch Random", &gSpaceshipDeathPitchRandom},
	{"Spaceship Hit Volume", &gSpaceshipHitVolume},
};
}

void TweaksScreen::RenderSoundEffects()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kSound);

	if (ImGui::BeginTable("SoundEffectsColumns", 2))
	{
		ImGui::TableNextColumn();

		WrapperSeparatorText("Blasters - Player");
		WrapperSlider("Volume", kiSection, 1.0f, "Player Blaster Volume");

		WrapperSeparatorText("Blasters - Enemy");
		WrapperSlider("Volume", kiSection, 1.0f, "Enemy Blaster Volume");

		WrapperSeparatorText("Blasters - Terrain Impact");
		WrapperSlider("Volume", kiSection, 1.0f, "Terrain Impact Volume");

		WrapperSeparatorText("Missiles");
		WrapperSlider("Launch Volume", kiSection, 1.0f, "Missile Launch Volume");
		WrapperSlider("Loop Volume", kiSection, 1.0f, "Missile Loop Volume");

		WrapperSeparatorText("Explosions");
		WrapperSlider("Volume", kiSection, 1.0f, "Explosion Volume");

		WrapperSeparatorText("Players - Shield");
		WrapperSlider("Hit Volume Base", kiSection, 1.0f, "Shield Hit Volume Base");
		WrapperSlider("Hit Volume Scale", kiSection, 1.0f, "Shield Hit Volume Scale");
		WrapperSlider("Down Volume", kiSection, 1.0f, "Shield Down Volume");

		WrapperSeparatorText("Players - Armor");
		WrapperSlider("Hit Volume Base", kiSection, 1.0f, "Armor Hit Volume Base");
		WrapperSlider("Hit Volume Scale", kiSection, 1.0f, "Armor Hit Volume Scale");

		WrapperSeparatorText("Spaceships");
		WrapperSlider("Death Volume", kiSection, 1.0f, "Spaceship Death Volume");
		WrapperSlider("Hit Volume", kiSection, 1.0f, "Spaceship Hit Volume");

		ImGui::TableNextColumn();

		WrapperSeparatorText("Blasters - Player");
		WrapperSlider("Pitch Min", kiSection, 1.0f, "Player Blaster Pitch Min");
		WrapperSlider("Pitch Random", kiSection, 1.0f, "Player Blaster Pitch Random");

		WrapperSeparatorText("Blasters - Enemy");
		WrapperSlider("Pitch Min", kiSection, 1.0f, "Enemy Blaster Pitch Min");
		WrapperSlider("Pitch Random", kiSection, 1.0f, "Enemy Blaster Pitch Random");

		WrapperSeparatorText("Missiles");
		WrapperSlider("Pitch Min", kiSection, 1.0f, "Missile Pitch Min");
		WrapperSlider("Pitch Random", kiSection, 1.0f, "Missile Pitch Random");

		WrapperSeparatorText("Spaceships");
		WrapperSlider("Death Pitch Min", kiSection, 1.0f, "Spaceship Death Pitch Min");
		WrapperSlider("Death Pitch Random", kiSection, 1.0f, "Spaceship Death Pitch Random");

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
