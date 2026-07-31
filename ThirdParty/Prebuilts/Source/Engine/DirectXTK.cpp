// Use XINPUT because it's supported on the Steam Deck
#if !defined(USING_XINPUT)
	#error
#endif

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

// Src/Keyboard.cpp is deliberately omitted: the in-house Raw Input path owns keyboard state under
// RIDEV_NOLEGACY, and DirectXTK's Keyboard would break the generic Alt/Shift/Ctrl bindings.
#include "../../../ThirdParty/DirectXTK/Audio/AudioEngine.cpp"
#include "../../../ThirdParty/DirectXTK/Audio/SoundCommon.cpp"
#include "../../../ThirdParty/DirectXTK/Src/GamePad.cpp"
#include "../../../ThirdParty/DirectXTK/Src/Mouse.cpp"

#pragma warning(pop)
