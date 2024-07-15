// Use XINPUT because it's supported on the Steam Deck
#if !defined(USING_XINPUT)
	#error
#endif

#include "../../../ThirdParty/DirectXTK/Audio/AudioEngine.cpp"
#include "../../../ThirdParty/DirectXTK/Audio/SoundCommon.cpp"
#include "../../../ThirdParty/DirectXTK/Src/GamePad.cpp"
#include "../../../ThirdParty/DirectXTK/Src/Mouse.cpp"
