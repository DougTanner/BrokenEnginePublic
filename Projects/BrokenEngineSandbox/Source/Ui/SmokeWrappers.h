#pragma once

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace game
{

// Explosions - Primary puff
extern engine::Wrapper gExplosionPrimaryPuffAreaOne;
extern engine::Wrapper gExplosionPrimaryPuffAreaTwo;
extern engine::Wrapper gExplosionPrimaryPuffIntensityOne;
extern engine::Wrapper gExplosionPrimaryPuffIntensityTwo;

// Explosions - Secondary puff
extern engine::Wrapper gExplosionSecondaryPuffAreaOne;
extern engine::Wrapper gExplosionSecondaryPuffAreaTwo;
extern engine::Wrapper gExplosionSecondaryPuffIntensityOne;
extern engine::Wrapper gExplosionSecondaryPuffIntensityTwo;

// Explosions - Primary trail
extern engine::Wrapper gExplosionPrimaryTrailIntensity;
extern engine::Wrapper gExplosionPrimaryTrailLength;
extern engine::Wrapper gExplosionPrimaryTrailDuration;

// Explosions - Secondary trail
extern engine::Wrapper gExplosionSecondaryTrailIntensity;
extern engine::Wrapper gExplosionSecondaryTrailLength;
extern engine::Wrapper gExplosionSecondaryTrailDuration;

// Blasters - Terrain puff
extern engine::Wrapper gBlasterPuffAreaStart;
extern engine::Wrapper gBlasterPuffAreaEnd;
extern engine::Wrapper gBlasterPuffIntensityStart;
extern engine::Wrapper gBlasterPuffIntensityEnd;

// Players - Impact puff
extern engine::Wrapper gPlayerImpactPuffAreaOne;
extern engine::Wrapper gPlayerImpactPuffAreaTwo;
extern engine::Wrapper gPlayerImpactPuffIntensityOne;
extern engine::Wrapper gPlayerImpactPuffIntensityTwo;

// Missiles - Trail
extern engine::Wrapper gMissileTrailIntensity;

} // namespace game

#endif // defined(BT_CLIENT)
