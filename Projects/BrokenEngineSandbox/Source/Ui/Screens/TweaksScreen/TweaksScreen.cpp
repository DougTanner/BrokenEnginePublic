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
		// Lighting Effects - Explosions Primary
		{"Explosion Primary Visible Area One", &gExplosionPrimaryVisibleAreaOne},
		{"Explosion Primary Visible Area Two", &gExplosionPrimaryVisibleAreaTwo},
		{"Explosion Primary Visible Area Three", &gExplosionPrimaryVisibleAreaThree},
		{"Explosion Primary Visible Intensity One", &gExplosionPrimaryVisibleIntensityOne},
		{"Explosion Primary Visible Intensity Two", &gExplosionPrimaryVisibleIntensityTwo},
		{"Explosion Primary Visible Intensity Three", &gExplosionPrimaryVisibleIntensityThree},
		{"Explosion Primary Lighting Area One", &gExplosionPrimaryLightingAreaOne},
		{"Explosion Primary Lighting Area Two", &gExplosionPrimaryLightingAreaTwo},
		{"Explosion Primary Lighting Area Three", &gExplosionPrimaryLightingAreaThree},
		{"Explosion Primary Lighting Intensity One", &gExplosionPrimaryLightingIntensityOne},
		{"Explosion Primary Lighting Intensity Two", &gExplosionPrimaryLightingIntensityTwo},
		{"Explosion Primary Lighting Intensity Three", &gExplosionPrimaryLightingIntensityThree},
		// Lighting Effects - Explosions Secondary
		{"Explosion Secondary Visible Area One", &gExplosionSecondaryVisibleAreaOne},
		{"Explosion Secondary Visible Area Two", &gExplosionSecondaryVisibleAreaTwo},
		{"Explosion Secondary Visible Area Three", &gExplosionSecondaryVisibleAreaThree},
		{"Explosion Secondary Visible Intensity One", &gExplosionSecondaryVisibleIntensityOne},
		{"Explosion Secondary Visible Intensity Two", &gExplosionSecondaryVisibleIntensityTwo},
		{"Explosion Secondary Visible Intensity Three", &gExplosionSecondaryVisibleIntensityThree},
		{"Explosion Secondary Lighting Area One", &gExplosionSecondaryLightingAreaOne},
		{"Explosion Secondary Lighting Area Two", &gExplosionSecondaryLightingAreaTwo},
		{"Explosion Secondary Lighting Area Three", &gExplosionSecondaryLightingAreaThree},
		{"Explosion Secondary Lighting Intensity One", &gExplosionSecondaryLightingIntensityOne},
		{"Explosion Secondary Lighting Intensity Two", &gExplosionSecondaryLightingIntensityTwo},
		{"Explosion Secondary Lighting Intensity Three", &gExplosionSecondaryLightingIntensityThree},
		// Lighting Effects - Explosions Particle
		{"Explosion Particle Visible Intensity", &gExplosionParticleVisibleIntensity},
		{"Explosion Particle Lighting Area", &gExplosionParticleLightingArea},
		{"Explosion Particle Lighting Intensity", &gExplosionParticleLightingIntensity},
		// Lighting Effects - Explosion Puffs
		{"Explosion Primary Puff Area One", &gExplosionPrimaryPuffAreaOne},
		{"Explosion Primary Puff Area Two", &gExplosionPrimaryPuffAreaTwo},
		{"Explosion Primary Puff Intensity One", &gExplosionPrimaryPuffIntensityOne},
		{"Explosion Primary Puff Intensity Two", &gExplosionPrimaryPuffIntensityTwo},
		{"Explosion Secondary Puff Area One", &gExplosionSecondaryPuffAreaOne},
		{"Explosion Secondary Puff Area Two", &gExplosionSecondaryPuffAreaTwo},
		{"Explosion Secondary Puff Intensity One", &gExplosionSecondaryPuffIntensityOne},
		{"Explosion Secondary Puff Intensity Two", &gExplosionSecondaryPuffIntensityTwo},
		// Lighting Effects - Crater
		{"Crater Visible Area One", &gCraterVisibleAreaOne},
		{"Crater Visible Area Two", &gCraterVisibleAreaTwo},
		{"Crater Visible Area Three", &gCraterVisibleAreaThree},
		{"Crater Visible Area Four", &gCraterVisibleAreaFour},
		{"Crater Visible Intensity One", &gCraterVisibleIntensityOne},
		{"Crater Visible Intensity Two", &gCraterVisibleIntensityTwo},
		{"Crater Visible Intensity Three", &gCraterVisibleIntensityThree},
		{"Crater Visible Intensity Four", &gCraterVisibleIntensityFour},
		{"Crater Lighting Area One", &gCraterLightingAreaOne},
		{"Crater Lighting Area Two", &gCraterLightingAreaTwo},
		{"Crater Lighting Area Three", &gCraterLightingAreaThree},
		{"Crater Lighting Area Four", &gCraterLightingAreaFour},
		{"Crater Lighting Intensity One", &gCraterLightingIntensityOne},
		{"Crater Lighting Intensity Two", &gCraterLightingIntensityTwo},
		{"Crater Lighting Intensity Three", &gCraterLightingIntensityThree},
		{"Crater Lighting Intensity Four", &gCraterLightingIntensityFour},
		// Lighting Effects - Blaster Puff
		{"Blaster Puff Area Start", &gBlasterPuffAreaStart},
		{"Blaster Puff Area End", &gBlasterPuffAreaEnd},
		{"Blaster Puff Intensity Start", &gBlasterPuffIntensityStart},
		{"Blaster Puff Intensity End", &gBlasterPuffIntensityEnd},
		// Lighting Effects - Players
		{"Player Area Light Visible Intensity", &gPlayerAreaLightVisibleIntensity},
		{"Player Area Light Lighting Size", &gPlayersBlasterLightingArea},
		{"Player Area Light Lighting Intensity", &gPlayersBlasterLightingIntensity},
		{"Player Impact Visible Area One", &gPlayerImpactVisibleAreaOne},
		{"Player Impact Visible Area Two", &gPlayerImpactVisibleAreaTwo},
		{"Player Impact Visible Intensity One", &gPlayerImpactVisibleIntensityOne},
		{"Player Impact Visible Intensity Two", &gPlayerImpactVisibleIntensityTwo},
		{"Player Impact Lighting Area One", &gPlayerImpactLightingAreaOne},
		{"Player Impact Lighting Area Two", &gPlayerImpactLightingAreaTwo},
		{"Player Impact Lighting Intensity One", &gPlayerImpactLightingIntensityOne},
		{"Player Impact Lighting Intensity Two", &gPlayerImpactLightingIntensityTwo},
		{"Player Impact Puff Area One", &gPlayerImpactPuffAreaOne},
		{"Player Impact Puff Area Two", &gPlayerImpactPuffAreaTwo},
		{"Player Impact Puff Intensity One", &gPlayerImpactPuffIntensityOne},
		{"Player Impact Puff Intensity Two", &gPlayerImpactPuffIntensityTwo},
		{"Hex Shield Intensity Decay", &gHexShieldIntensityDecay},
		{"Hex Shield Lighting Intensity", &gHexShieldLightingIntensity},
		// Lighting Effects - Missiles
		{"Missile Exhaust Visible Intensity", &gMissileExhaustVisibleIntensity},
		{"Missile Exhaust Lighting Area", &gMissileExhaustLightingArea},
		{"Missile Exhaust Lighting Intensity", &gMissileExhaustLightingIntensity},
		{"Missile Trail Intensity", &gMissileTrailIntensity},
		// Lighting Effects - Spaceships
		{"Spaceship Explosion Intensity", &gSpaceshipExplosionLightingIntensity},
		{"Enemy Blaster Visible Intensity", &gEnemyBlasterVisibleIntensity},
		{"Enemy Blaster Lighting Area", &gEnemyBlasterLightingArea},
		{"Enemy Blaster Lighting Intensity", &gEnemyBlasterLightingIntensity},
		{"Hit Flash Visible Area One", &gHitFlashVisibleAreaOne},
		{"Hit Flash Visible Area Two", &gHitFlashVisibleAreaTwo},
		{"Hit Flash Visible Intensity One", &gHitFlashVisibleIntensityOne},
		{"Hit Flash Visible Intensity Two", &gHitFlashVisibleIntensityTwo},
		{"Hit Flash Lighting Area One", &gHitFlashLightingAreaOne},
		{"Hit Flash Lighting Area Two", &gHitFlashLightingAreaTwo},
		{"Hit Flash Lighting Intensity One", &gHitFlashLightingIntensityOne},
		{"Hit Flash Lighting Intensity Two", &gHitFlashLightingIntensityTwo},
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
