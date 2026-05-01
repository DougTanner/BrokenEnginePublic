#include "Wrapper.h"

namespace game
{

// Wind - Per-entity deposits
engine::Wrapper gWindDepositExplosionsWidth(6.0f, 4.0f, 20.0f);
engine::Wrapper gWindDepositExplosionsIntensity(0.02f, 0.0f, 0.05f);

engine::Wrapper gWindDepositPlayerBlastersWidth(1.0f, 0.1f, 2.0f);
engine::Wrapper gWindDepositPlayerBlastersIntensity(0.05f, 0.0f, 0.2f);
engine::Wrapper gWindDepositBlastersLengthMultiplier(3.0f, 0.5f, 6.0f);

engine::Wrapper gWindDepositPlayerWidth(3.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositPlayerIntensity(0.03f, 0.0f, 0.1f);
engine::Wrapper gWindDepositPlayerLengthMultiplier(5.0f, 0.5f, 10.0f);

engine::Wrapper gWindDepositSpaceshipsWidth(2.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositSpaceshipsIntensity(0.03f, 0.0f, 0.1f);
engine::Wrapper gWindDepositSpaceshipsLengthMultiplier(3.0f, 0.5f, 5.0f);

engine::Wrapper gWindDepositSpaceshipsBlastersWidth(1.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositSpaceshipsBlastersIntensity(0.05f, 0.0f, 0.1f);
engine::Wrapper gWindDepositSpaceshipsBlastersLengthMultiplier(2.0f, 0.5f, 3.0f);

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
