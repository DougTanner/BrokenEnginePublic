#pragma once

#define ENABLE_CRT_DEBUG_HEAP
#include "ExternalHeaders.h"

inline constexpr bool kbAlsoLogToPrintf = true;
inline constexpr bool kbEnableDebugBreak = true;
inline constexpr bool kbEnableLogging = true;
inline constexpr bool kbIsDataPacker = true;

inline constexpr uint64_t kLogEnabledCategoriesDefault = ~0ULL;

#include "Common.h"
