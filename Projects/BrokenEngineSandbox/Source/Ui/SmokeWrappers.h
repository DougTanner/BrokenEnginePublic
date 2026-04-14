#pragma once

#include "Ui/WrapperBase.h"

namespace game
{

// Explosions - Primary puff
extern engine::Wrapper gExpPrimaryPuffAreaStart;
extern engine::Wrapper gExpPrimaryPuffAreaEnd;
extern engine::Wrapper gExpPrimaryPuffIntensity;

// Explosions - Secondary puff
extern engine::Wrapper gExpSecondaryPuffAreaStart;
extern engine::Wrapper gExpSecondaryPuffAreaEnd;
extern engine::Wrapper gExpSecondaryPuffIntensity;

// Blasters - Terrain puff
extern engine::Wrapper gBlasterPuffAreaStart;
extern engine::Wrapper gBlasterPuffAreaEnd;
extern engine::Wrapper gBlasterPuffIntensityStart;
extern engine::Wrapper gBlasterPuffIntensityEnd;

// Players - Impact puff
extern engine::Wrapper gPlayerImpactPuffAreaStart;
extern engine::Wrapper gPlayerImpactPuffAreaEnd;
extern engine::Wrapper gPlayerImpactPuffIntensityStart;

// Missiles - Trail
extern engine::Wrapper gMissileTrailIntensity;

} // namespace game
