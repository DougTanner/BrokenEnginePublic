#include "WindDepositsWrappers.h"

namespace game
{

// Wind - Per-entity deposits
engine::Wrapper gWindDepositPlayerWidth(3.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositPlayerIntensity(0.03f, 0.0f, 0.1f);
engine::Wrapper gWindDepositPlayerLengthMultiplier(5.0f, 0.5f, 10.0f);

engine::Wrapper gWindDepositSpaceshipsWidth(2.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositSpaceshipsIntensity(0.03f, 0.0f, 0.1f);
engine::Wrapper gWindDepositSpaceshipsLengthMultiplier(3.0f, 0.5f, 5.0f);

engine::Wrapper gWindDepositPlayerBlastersWidth(1.0f, 0.1f, 2.0f);
engine::Wrapper gWindDepositPlayerBlastersIntensity(0.05f, 0.0f, 0.2f);
engine::Wrapper gWindDepositPlayerBlastersLengthMultiplier(3.0f, 0.5f, 6.0f);

engine::Wrapper gWindDepositSpaceshipsBlastersWidth(1.0f, 0.1f, 5.0f);
engine::Wrapper gWindDepositSpaceshipsBlastersIntensity(0.05f, 0.0f, 0.1f);
engine::Wrapper gWindDepositSpaceshipsBlastersLengthMultiplier(2.0f, 0.5f, 3.0f);

engine::Wrapper gWindDepositExplosionsWidth(10.0f, 4.0f, 50.0f);
engine::Wrapper gWindDepositExplosionsIntensity(0.003f, 0.0f, 0.005f);

} // namespace game
