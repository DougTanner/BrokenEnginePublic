#pragma once

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace game
{

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

#endif // defined(BT_CLIENT)
