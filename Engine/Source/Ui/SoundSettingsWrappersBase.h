#pragma once

#include "WrapperBase.h"

namespace engine
{

extern Wrapper gMasterVolume;
extern Wrapper gMusicVolume;
extern Wrapper gSoundVolume;

// Listener Distance Start (manual fade start, height-lerped)
extern Wrapper gListenerDistanceStartStartHeight;
extern Wrapper gListenerDistanceStartEndHeight;
extern Wrapper gListenerDistanceStartLow;
extern Wrapper gListenerDistanceStartHigh;

// Listener Distance End (manual fade end, height-lerped)
extern Wrapper gListenerDistanceEndStartHeight;
extern Wrapper gListenerDistanceEndEndHeight;
extern Wrapper gListenerDistanceEndLow;
extern Wrapper gListenerDistanceEndHigh;

// Listener Curve (X3DAudio CurveDistanceScaler, height-lerped)
extern Wrapper gListenerCurveStartHeight;
extern Wrapper gListenerCurveEndHeight;
extern Wrapper gListenerCurveLow;
extern Wrapper gListenerCurveHigh;

} // namespace engine
