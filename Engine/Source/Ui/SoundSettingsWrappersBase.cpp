#include "SoundSettingsWrappersBase.h"

namespace engine
{

Wrapper gMasterVolume(1.0f, 0.0f, 1.0f);
Wrapper gMusicVolume(0.25f, 0.0f, 1.0f);
Wrapper gSoundVolume(1.0f, 0.0f, 1.0f);

// Listener Distance Start (manual fade start, height-lerped)
Wrapper gListenerDistanceStartStartHeight(150.0f, 0.0f, 1000.0f);
Wrapper gListenerDistanceStartEndHeight(600.0f, 0.0f, 1000.0f);
Wrapper gListenerDistanceStartLow(200.0f, 0.0f, 1000.0f);
Wrapper gListenerDistanceStartHigh(600.0f, 0.0f, 1000.0f);

// Listener Distance End (manual fade end, height-lerped)
Wrapper gListenerDistanceEndStartHeight(150.0f, 0.0f, 1000.0f);
Wrapper gListenerDistanceEndEndHeight(600.0f, 0.0f, 1000.0f);
Wrapper gListenerDistanceEndLow(400.0f, 0.0f, 2000.0f);
Wrapper gListenerDistanceEndHigh(1200.0f, 0.0f, 2000.0f);

// Listener Curve (X3DAudio CurveDistanceScaler, height-lerped)
Wrapper gListenerCurveStartHeight(150.0f, 0.0f, 1000.0f);
Wrapper gListenerCurveEndHeight(600.0f, 0.0f, 1000.0f);
Wrapper gListenerCurveLow(10.0f, 1.0f, 100.0f);
Wrapper gListenerCurveHigh(10.0f, 1.0f, 100.0f);

// Listener Audible Floor (mfManualFadeVolume multiplier, height-lerped)
Wrapper gListenerAudibleFloorStartHeight(150.0f, 0.0f, 1000.0f);
Wrapper gListenerAudibleFloorEndHeight(600.0f, 0.0f, 1000.0f);
Wrapper gListenerAudibleFloorLow(0.15f, 0.0f, 0.5f);
Wrapper gListenerAudibleFloorHigh(0.15f, 0.0f, 0.5f);

} // namespace engine
