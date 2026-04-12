#include "TweaksScreen.h"

#include "Game.h"
#include "Ui/LightingWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

TweaksScreen::TweaksScreen()
{
	// Heap: inserting game-specific entries into the slider map once
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	engine::TweaksSliderMap::Get().insert(
	{
		// Hex Shield - Edge
		{"Grow", &gHexShieldGrow},
		{"Edge Distance", &gHexShieldEdgeDistance},
		{"Edge Power", &gHexShieldEdgePower},
		{"Edge Multiplier", &gHexShieldEdgeMultiplier},
		// Hex Shield - Wave
		{"Wave Multiplier", &gHexShieldWaveMultiplier},
		{"Wave Dot", &gHexShieldWaveDotMultiplier},
		{"Wave Intensity", &gHexShieldWaveIntensityMultiplier},
		{"Wave Intensity Power", &gHexShieldWaveIntensityPower},
		{"Wave Falloff Power", &gHexShieldWaveFalloffPower},
		// Hex Shield - Direction
		{"Direction Falloff Power", &gHexShieldDirectionFalloffPower},
		{"Direction Multiplier", &gHexShieldDirectionMultiplier},
		// Wind - Deposit (Player)
		{"Player Deposit Width", &gWindDepositPlayerWidth},
		{"Player Deposit Intensity", &gWindDepositPlayerIntensity},
		{"Player Deposit Length Multiplier", &gWindDepositPlayerLengthMultiplier},
		// Wind - Deposit (Spaceships)
		{"Spaceships Deposit Width", &gWindDepositSpaceshipsWidth},
		{"Spaceships Deposit Intensity", &gWindDepositSpaceshipsIntensity},
		{"Spaceships Deposit Length Multiplier", &gWindDepositSpaceshipsLengthMultiplier},
		// Wind - Deposit (Player Blasters)
		{"Player Blasters Deposit Width", &gWindDepositPlayerBlastersWidth},
		{"Player Blasters Deposit Intensity", &gWindDepositPlayerBlastersIntensity},
		{"Blasters Deposit Length Multiplier", &gWindDepositBlastersLengthMultiplier},
		// Wind - Deposit (Spaceships Blasters)
		{"Spaceships Blasters Deposit Width", &gWindDepositSpaceshipsBlastersWidth},
		{"Spaceships Blasters Deposit Intensity", &gWindDepositSpaceshipsBlastersIntensity},
		// Wind - Deposit (Explosions)
		{"Explosions Deposit Width", &gWindDepositExplosionsWidth},
		{"Explosions Deposit Intensity", &gWindDepositExplosionsIntensity},
		// Lighting Effects - Explosions
		{"Exp Primary Visible Area", &gExpPrimaryVisibleArea},
		{"Exp Primary Visible Intensity", &gExpPrimaryVisibleIntensity},
		{"Exp Primary Lighting Area", &gExpPrimaryLightingArea},
		{"Exp Primary Lighting Intensity", &gExpPrimaryLightingIntensity},
		{"Exp Secondary Visible Area", &gExpSecondaryVisibleArea},
		{"Exp Secondary Visible Intensity", &gExpSecondaryVisibleIntensity},
		{"Exp Secondary Lighting Area", &gExpSecondaryLightingArea},
		{"Exp Secondary Lighting Intensity", &gExpSecondaryLightingIntensity},
		{"Exp Primary Puff Area Start", &gExpPrimaryPuffAreaStart},
		{"Exp Primary Puff Area End", &gExpPrimaryPuffAreaEnd},
		{"Exp Primary Puff Intensity", &gExpPrimaryPuffIntensity},
		{"Exp Secondary Puff Area Start", &gExpSecondaryPuffAreaStart},
		{"Exp Secondary Puff Area End", &gExpSecondaryPuffAreaEnd},
		{"Exp Secondary Puff Intensity", &gExpSecondaryPuffIntensity},
		// Lighting Effects - Blasters
		{"Crater Visible Area Start", &gCraterVisibleAreaStart},
		{"Crater Visible Area End", &gCraterVisibleAreaEnd},
		{"Crater Visible Intensity Start", &gCraterVisibleIntensityStart},
		{"Crater Visible Intensity End", &gCraterVisibleIntensityEnd},
		{"Crater Lighting Area", &gCraterLightingArea},
		{"Crater Lighting Intensity Start", &gCraterLightingIntensityStart},
		{"Crater Lighting Intensity Mid", &gCraterLightingIntensityMid},
		{"Crater Lighting Intensity End", &gCraterLightingIntensityEnd},
		{"Blaster Puff Area Start", &gBlasterPuffAreaStart},
		{"Blaster Puff Area End", &gBlasterPuffAreaEnd},
		{"Blaster Puff Intensity Start", &gBlasterPuffIntensityStart},
		{"Blaster Puff Intensity End", &gBlasterPuffIntensityEnd},
		// Lighting Effects - Players
		{"Player Area Light Visible Intensity", &gPlayerAreaLightVisibleIntensity},
		{"Player Area Light Lighting Size", &gPlayerAreaLightLightingSize},
		{"Player Area Light Lighting Intensity", &gPlayerAreaLightLightingIntensity},
		{"Player Impact Start Visible Area", &gPlayerImpactStartVisibleArea},
		{"Player Impact Start Visible Intensity", &gPlayerImpactStartVisibleIntensity},
		{"Player Impact Start Lighting Area", &gPlayerImpactStartLightingArea},
		{"Player Impact Start Lighting Intensity", &gPlayerImpactStartLightingIntensity},
		{"Player Impact End Visible Intensity", &gPlayerImpactEndVisibleIntensity},
		{"Player Impact End Lighting Intensity", &gPlayerImpactEndLightingIntensity},
		{"Player Impact Puff Area Start", &gPlayerImpactPuffAreaStart},
		{"Player Impact Puff Area End", &gPlayerImpactPuffAreaEnd},
		{"Player Impact Puff Intensity Start", &gPlayerImpactPuffIntensityStart},
		{"Hex Shield Intensity Decay", &gHexShieldIntensityDecay},
		{"Hex Shield Lighting Intensity", &gHexShieldLightingIntensity},
		// Lighting Effects - Missiles
		{"Missile Exhaust Visible Intensity", &gMissileExhaustVisibleIntensity},
		{"Missile Exhaust Lighting Area", &gMissileExhaustLightingArea},
		{"Missile Exhaust Lighting Intensity", &gMissileExhaustLightingIntensity},
		{"Missile Trail Intensity", &gMissileTrailIntensity},
		// Lighting Effects - Spaceships
		{"Spaceship Explosion Intensity", &gSpaceshipExplosionIntensity},
		{"Spaceship Explosion Particle Lighting Size", &gSpaceshipExplosionParticleLightingSize},
		{"Spaceship Explosion Particle Lighting Intensity", &gSpaceshipExplosionParticleLightingIntensity},
		{"Enemy Blaster Visible Intensity", &gEnemyBlasterVisibleIntensity},
		{"Enemy Blaster Lighting Size", &gEnemyBlasterLightingSize},
		{"Enemy Blaster Lighting Intensity", &gEnemyBlasterLightingIntensity},
		{"Hit Flash Visible Area", &gHitFlashStartVisibleArea},
		{"Hit Flash Visible Intensity", &gHitFlashStartVisibleIntensity},
		{"Hit Flash Lighting Area", &gHitFlashStartLightingArea},
		{"Hit Flash Lighting Intensity", &gHitFlashStartLightingIntensity},
	});
}

void TweaksScreen::Render()
{
	if constexpr (kbDebugInput)
	{
		if (!game::gpGame->mbShowImGui)
		{
			return;
		}
	}

	TweaksScreenBase::Render();
}

} // namespace game

#endif // BT_CLIENT
