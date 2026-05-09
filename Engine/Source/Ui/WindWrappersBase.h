#pragma once

#include "WrapperBase.h"

namespace engine
{

// Time & Global
extern Wrapper gWindTimeScale;
extern Wrapper gWindThresholdLow;
extern Wrapper gWindThresholdHigh;

// Propagation (Low/High side-by-side in UI)
extern Wrapper gWindAdvectionScaleLow;
extern Wrapper gWindAdvectionScaleHigh;
extern Wrapper gWindSwirlScaleLow;
extern Wrapper gWindSwirlScaleHigh;
extern Wrapper gWindSwirlAmountLow;
extern Wrapper gWindSwirlAmountHigh;
extern Wrapper gWindSwirlSpeedLow;
extern Wrapper gWindSwirlSpeedHigh;
extern Wrapper gWindVorticityConfinementLow;
extern Wrapper gWindVorticityConfinementHigh;
extern Wrapper gWindDecayLow;
extern Wrapper gWindDecayHigh;
extern Wrapper gWindMomentumLow;
extern Wrapper gWindMomentumHigh;
extern Wrapper gWindDiffusionLow;
extern Wrapper gWindDiffusionHigh;

} // namespace engine
