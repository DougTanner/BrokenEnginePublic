// Use XINPUT because it's supported on the Steam Deck
#if !defined(USING_XINPUT)
	#error
#endif

#include <codeanalysis/warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#include "../../../ThirdParty/DirectXTK/Audio/AudioEngine.cpp"
#include "../../../ThirdParty/DirectXTK/Audio/SoundCommon.cpp"
#include "../../../ThirdParty/DirectXTK/Src/GamePad.cpp"
#include "../../../ThirdParty/DirectXTK/Src/Mouse.cpp"

#pragma warning(pop)
