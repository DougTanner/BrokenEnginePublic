#include "SoundSettingsWrappersBase.h"

namespace engine
{

Wrapper gMasterVolume(1.0f, 0.0f, 1.0f);
Wrapper gMusicVolume(0.25f, 0.0f, 1.0f);
Wrapper gSoundVolume(1.0f, 0.0f, 1.0f);

HeightLerpWrapperQuartet gListenerDistanceStart
{
	Wrapper(150.0f, 0.0f, 1000.0f),  // StartHeight
	Wrapper(600.0f, 0.0f, 1000.0f),  // EndHeight
	Wrapper(200.0f, 0.0f, 1000.0f),  // Low
	Wrapper(600.0f, 0.0f, 1000.0f),  // High
};

HeightLerpWrapperQuartet gListenerDistanceEnd
{
	Wrapper(150.0f, 0.0f, 1000.0f),  // StartHeight
	Wrapper(600.0f, 0.0f, 1000.0f),  // EndHeight
	Wrapper(400.0f, 0.0f, 2000.0f),  // Low
	Wrapper(1200.0f, 0.0f, 2000.0f), // High
};

HeightLerpWrapperQuartet gListenerCurve
{
	Wrapper(150.0f, 0.0f, 1000.0f),  // StartHeight
	Wrapper(600.0f, 0.0f, 1000.0f),  // EndHeight
	Wrapper(10.0f, 1.0f, 100.0f),    // Low
	Wrapper(10.0f, 1.0f, 100.0f),    // High
};

HeightLerpWrapperQuartet gListenerAudibleFloor
{
	Wrapper(150.0f, 0.0f, 1000.0f),  // StartHeight
	Wrapper(600.0f, 0.0f, 1000.0f),  // EndHeight
	Wrapper(0.15f, 0.0f, 0.5f),      // Low
	Wrapper(0.15f, 0.0f, 0.5f),      // High
};

} // namespace engine
