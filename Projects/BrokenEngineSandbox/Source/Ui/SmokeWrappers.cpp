#if defined(BT_CLIENT)

#include "SmokeWrappers.h"

namespace game
{

// Explosions - Primary puff
engine::Wrapper gExplosionPrimaryPuffAreaOne(0.2f, 0.02f, 1.0f);
engine::Wrapper gExplosionPrimaryPuffAreaTwo(1.0f, 0.1f, 5.0f);
engine::Wrapper gExplosionPrimaryPuffIntensityOne(2.0f, 0.25f, 12.0f);
engine::Wrapper gExplosionPrimaryPuffIntensityTwo(1.0f, 0.25f, 12.0f);

// Explosions - Secondary puff
engine::Wrapper gExplosionSecondaryPuffAreaOne(0.1f, 0.02f, 1.0f);
engine::Wrapper gExplosionSecondaryPuffAreaTwo(0.5f, 0.06f, 3.0f);
engine::Wrapper gExplosionSecondaryPuffIntensityOne(2.0f, 0.05f, 2.5f);
engine::Wrapper gExplosionSecondaryPuffIntensityTwo(1.0f, 0.05f, 2.5f);

// Explosions - Primary trail
engine::Wrapper gExplosionPrimaryTrailIntensity(20.0f, 0.05f, 50.0f);
engine::Wrapper gExplosionPrimaryTrailLength(1.5f, 0.1f, 4.0f);
engine::Wrapper gExplosionPrimaryTrailDuration(3.0f, 0.1f, 4.0f);

// Explosions - Secondary trail
engine::Wrapper gExplosionSecondaryTrailIntensity(10.0f, 0.05f, 50.0f);
engine::Wrapper gExplosionSecondaryTrailLength(1.0f, 0.1f, 4.0f);
engine::Wrapper gExplosionSecondaryTrailDuration(1.0f, 0.1f, 8.0f);

// Blasters - Terrain puff
engine::Wrapper gBlasterPuffAreaStart(0.25f, 0.03f, 1.25f);
engine::Wrapper gBlasterPuffAreaEnd(1.0f, 0.08f, 3.75f);
engine::Wrapper gBlasterPuffIntensityStart(4.0f, 0.6f, 30.0f);
engine::Wrapper gBlasterPuffIntensityEnd(1.0f, 0.05f, 2.5f);

// Players - Impact puff
engine::Wrapper gPlayerImpactPuffAreaOne(0.135f, 0.01f, 0.7f);
engine::Wrapper gPlayerImpactPuffAreaTwo(0.45f, 0.05f, 2.25f);
engine::Wrapper gPlayerImpactPuffIntensityOne(4.0f, 0.4f, 20.0f);
engine::Wrapper gPlayerImpactPuffIntensityTwo(0.0f, 0.0f, 20.0f);

// Missiles - Trail
engine::Wrapper gMissileTrailIntensity(0.5f, 0.05f, 2.5f);

} // namespace game

#endif // defined(BT_CLIENT)
