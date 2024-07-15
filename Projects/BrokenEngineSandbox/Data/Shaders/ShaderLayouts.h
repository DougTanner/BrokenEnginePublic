#if defined(BT_ENGINE)
#pragma once
#endif

#include "../../../Engine/Data/Shaders/ShaderLayoutsBase.h"

#if defined(BT_ENGINE)
namespace shaders
{
#endif

CONSTEXPR int kiMaxTextureCount = 200 - 16;

// You need to manually change this to match the same values in Data.h
CONSTEXPR int kiTextureCount = 111;
CONSTEXPR int kiUiTextureCount = 9;

#if defined(BT_ENGINE)
}
#endif
