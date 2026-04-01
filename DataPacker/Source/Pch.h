#pragma once

#define ENABLE_CRT_DEBUG_HEAP
#include "ExternalHeaders.h"

inline constexpr bool kbAlsoLogToPrintf = true;
inline constexpr bool kbDebugBreak = true;
inline constexpr bool kbLogging = true;
inline constexpr bool kbIsDataPacker = true;

#include "Common.h"

inline constexpr LogCategory keFocusedLogCategoryDefault = kLogDefault;
inline constexpr LogLevel keLogLevelDefault = kVerbose;
