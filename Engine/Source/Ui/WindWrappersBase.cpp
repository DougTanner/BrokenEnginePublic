#include "WindWrappersBase.h"

namespace engine
{

// Time & Global
Wrapper gWindTimeScale(0.35f, 0.005f, 0.5f);
Wrapper gWindThresholdLow(0.02f, 0.0f, 0.05f);
Wrapper gWindThresholdHigh(0.05f, 0.05f, 0.1f);

// Propagation (Low/High side-by-side in UI)
Wrapper gWindAdvectionScaleLow(1.0f, 0.0f, 1.0f);
Wrapper gWindAdvectionScaleHigh(5.0f, 0.5f, 5.0f);
Wrapper gWindSwirlScaleLow(1.0f, 0.5f, 3.0f);
Wrapper gWindSwirlScaleHigh(0.5f, 0.5f, 4.0f);
Wrapper gWindSwirlAmountLow(90.0f, 0.0f, 100.0f);
Wrapper gWindSwirlAmountHigh(110.0f, 0.0f, 500.0f);
Wrapper gWindSwirlSpeedLow(5.0f, 0.0f, 10.0f);
Wrapper gWindSwirlSpeedHigh(2.0f, 0.0f, 10.0f);
Wrapper gWindVorticityConfinementLow(0.5f, 0.0f, 1.0f);
Wrapper gWindVorticityConfinementHigh(4.0f, 0.0f, 4.0f);
Wrapper gWindDecayLow(0.4f, 0.0f, 0.9f); // max < 1.0f load-bearing: keeps fDecayRate = 1 - mix(low, high, t) > 0 in WindSpreadCommon.h:70.
Wrapper gWindDecayHigh(0.99f, 0.5f, 0.999f); // max < 1.0f load-bearing: keeps fDecayRate = 1 - mix(low, high, t) > 0 in WindSpreadCommon.h:70.
Wrapper gWindMomentumLow(0.75f, 0.0f, 1.0f);
Wrapper gWindMomentumHigh(0.6f, 0.0f, 1.0f);
Wrapper gWindDiffusionLow(100.0f, 0.0f, 100.0f);
Wrapper gWindDiffusionHigh(0.0f, 0.0f, 100.0f);

} // namespace engine
