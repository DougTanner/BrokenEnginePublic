#include "SmokeWrappers.h"

namespace game
{

// Explosions - Primary puff
engine::Wrapper gExpPrimaryPuffAreaStart(0.175f, 0.02f, 1.0f);
engine::Wrapper gExpPrimaryPuffAreaEnd(1.05f, 0.1f, 5.0f);
engine::Wrapper gExpPrimaryPuffIntensity(2.5f, 0.25f, 12.0f);

// Explosions - Secondary puff
engine::Wrapper gExpSecondaryPuffAreaStart(0.175f, 0.02f, 1.0f);
engine::Wrapper gExpSecondaryPuffAreaEnd(0.6f, 0.06f, 3.0f);
engine::Wrapper gExpSecondaryPuffIntensity(0.5f, 0.05f, 2.5f);

// Blasters - Terrain puff
engine::Wrapper gBlasterPuffAreaStart(0.25f, 0.03f, 1.25f);
engine::Wrapper gBlasterPuffAreaEnd(0.75f, 0.08f, 3.75f);
engine::Wrapper gBlasterPuffIntensityStart(6.0f, 0.6f, 30.0f);
engine::Wrapper gBlasterPuffIntensityEnd(0.5f, 0.05f, 2.5f);

// Players - Impact puff
engine::Wrapper gPlayerImpactPuffAreaStart(0.135f, 0.01f, 0.7f);
engine::Wrapper gPlayerImpactPuffAreaEnd(0.45f, 0.05f, 2.25f);
engine::Wrapper gPlayerImpactPuffIntensityStart(4.0f, 0.4f, 20.0f);

// Missiles - Trail
engine::Wrapper gMissileTrailIntensity(0.5f, 0.05f, 2.5f);

} // namespace game
