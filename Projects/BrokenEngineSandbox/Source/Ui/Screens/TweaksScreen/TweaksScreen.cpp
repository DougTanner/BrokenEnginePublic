#include "TweaksScreen.h"

#include "Game.h"
#include "Ui/LightingWrappers.h"
#include "Ui/SmokeWrappers.h"

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
		{"Explosion Primary Visible Area", &gExplosionPrimaryVisibleArea},
		{"Explosion Primary Visible Intensity", &gExplosionPrimaryVisibleIntensity},
		{"Explosion Primary Lighting Area", &gExplosionPrimaryLightingArea},
		{"Explosion Primary Lighting Intensity", &gExplosionPrimaryLightingIntensity},
		{"Explosion Secondary Visible Area", &gExplosionSecondaryVisibleArea},
		{"Explosion Secondary Visible Intensity", &gExplosionSecondaryVisibleIntensity},
		{"Explosion Secondary Lighting Area", &gExplosionSecondaryLightingArea},
		{"Explosion Secondary Lighting Intensity", &gExplosionSecondaryLightingIntensity},
		{"Explosion Particle Lighting Area", &gExplosionParticleLightingArea},
		{"Explosion Particle Lighting Intensity", &gExplosionParticleLightingIntensity},
		{"Explosion Primary Puff Area Start", &gExpPrimaryPuffAreaStart},
		{"Explosion Primary Puff Area End", &gExpPrimaryPuffAreaEnd},
		{"Explosion Primary Puff Intensity", &gExpPrimaryPuffIntensity},
		{"Explosion Secondary Puff Area Start", &gExpSecondaryPuffAreaStart},
		{"Explosion Secondary Puff Area End", &gExpSecondaryPuffAreaEnd},
		{"Explosion Secondary Puff Intensity", &gExpSecondaryPuffIntensity},
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
		{"Player Area Light Lighting Size", &gPlayersBlasterLightingArea},
		{"Player Area Light Lighting Intensity", &gPlayersBlasterLightingIntensity},
		{"Player Impact Visible Area", &gPlayerImpactVisibleArea},
		{"Player Impact Start Visible Intensity", &gPlayerImpactStartVisibleIntensity},
		{"Player Impact Lighting Area", &gPlayerImpactLightingArea},
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
		{"Spaceship Explosion Intensity", &gSpaceshipExplosionLightingIntensity},
		{"Spaceship Explosion Particle Lighting Size", &gSpaceshipExplosionParticleLightingArea},
		{"Spaceship Explosion Particle Lighting Intensity", &gSpaceshipExplosionParticleLightingIntensity},
		{"Enemy Blaster Visible Intensity", &gEnemyBlasterVisibleIntensity},
		{"Enemy Blaster Lighting Area", &gEnemyBlasterLightingArea},
		{"Enemy Blaster Lighting Intensity", &gEnemyBlasterLightingIntensity},
		{"Hit Flash Visible Area", &gHitFlashVisibleArea},
		{"Hit Flash Visible Intensity", &gHitFlashVisibleIntensity},
		{"Hit Flash Lighting Area", &gHitFlashLightingArea},
		{"Hit Flash Lighting Intensity", &gHitFlashLightingIntensity},
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
