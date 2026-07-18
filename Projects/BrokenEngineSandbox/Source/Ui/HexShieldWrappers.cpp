#if defined(BT_CLIENT)

#include "HexShieldWrappers.h"

namespace game
{

// Hex shield
engine::Wrapper gHexShieldGrow(2.0f, 1.0f, 4.0f);
engine::Wrapper gHexShieldEdgeDistance(18.8f, 18.0f, 19.1f);
engine::Wrapper gHexShieldEdgePower(1.0f, 0.5f, 2.0f);
engine::Wrapper gHexShieldEdgeMultiplier(0.5f, 0.25f, 1.0f);

engine::Wrapper gHexShieldWaveMultiplier(7.0f, 0.0f, 20.0f);
engine::Wrapper gHexShieldWaveDotMultiplier(5.0f, 0.5f, 10.0f);
engine::Wrapper gHexShieldWaveIntensityMultiplier(12.0f, 0.5f, 20.0f);
engine::Wrapper gHexShieldWaveIntensityPower(1.6f, 0.25f, 4.0f);
engine::Wrapper gHexShieldWaveFalloffPower(2.1f, 0.25f, 4.0f);

engine::Wrapper gHexShieldDirectionFalloffPower(4.35f, 2.0f, 10.0f);
engine::Wrapper gHexShieldDirectionMultiplier(4.5f, 0.5f, 8.0f);

} // namespace game

#endif // defined(BT_CLIENT)
