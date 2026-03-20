#pragma once

#include "Ui/WrapperBase.h"

namespace game
{

// Wind - Per-entity deposits
extern engine::Wrapper gWindDepositPlayerWidth;
extern engine::Wrapper gWindDepositPlayerIntensity;
extern engine::Wrapper gWindDepositPlayerLengthMultiplier;
extern engine::Wrapper gWindDepositSpaceshipsWidth;
extern engine::Wrapper gWindDepositSpaceshipsIntensity;
extern engine::Wrapper gWindDepositSpaceshipsLengthMultiplier;
extern engine::Wrapper gWindDepositPlayerBlastersWidth;
extern engine::Wrapper gWindDepositPlayerBlastersIntensity;
extern engine::Wrapper gWindDepositBlastersLengthMultiplier;
extern engine::Wrapper gWindDepositSpaceshipsBlastersWidth;
extern engine::Wrapper gWindDepositSpaceshipsBlastersIntensity;
extern engine::Wrapper gWindDepositSpaceshipsBlastersLengthMultiplier;
extern engine::Wrapper gWindDepositExplosionsWidth;
extern engine::Wrapper gWindDepositExplosionsIntensity;

// Hex shield
extern engine::Wrapper gHexShieldGrow;
extern engine::Wrapper gHexShieldEdgeDistance;
extern engine::Wrapper gHexShieldEdgePower;
extern engine::Wrapper gHexShieldEdgeMultiplier;

extern engine::Wrapper gHexShieldWaveMultiplier;
extern engine::Wrapper gHexShieldWaveDotMultiplier;
extern engine::Wrapper gHexShieldWaveIntensityMultiplier;
extern engine::Wrapper gHexShieldWaveIntensityPower;
extern engine::Wrapper gHexShieldWaveFalloffPower;

extern engine::Wrapper gHexShieldDirectionFalloffPower;
extern engine::Wrapper gHexShieldDirectionMultiplier;

} // namespace game
