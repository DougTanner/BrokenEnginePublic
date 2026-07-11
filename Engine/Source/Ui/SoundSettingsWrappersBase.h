#pragma once

#include "HeightLerpWrapperQuartet.h"
#include "WrapperBase.h"

namespace engine
{

extern Wrapper gMasterVolume;
extern Wrapper gMusicVolume;
extern Wrapper gSoundVolume;

// Listener distance / curve / audible-floor are camera-height-lerped per the canonical
// "Camera-Height-Conditional Uniforms" pattern.
extern HeightLerpWrapperQuartet gListenerDistanceStart;  // mfEffectiveFadeStart
extern HeightLerpWrapperQuartet gListenerDistanceEnd;    // mfEffectiveFadeEnd
extern HeightLerpWrapperQuartet gListenerCurve;          // mfCurveDistanceScaler (X3DAudio)
extern HeightLerpWrapperQuartet gListenerAudibleFloor;   // mfManualFadeVolume

} // namespace engine
