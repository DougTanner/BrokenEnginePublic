#pragma once

#define ENABLE_CRT_DEBUG_HEAP
#include "ExternalHeaders.h"

inline constexpr bool kbAlsoLogToPrintf = true;
inline constexpr bool kbDebugBreak = true;
inline constexpr bool kbLogging = true;

#include "Log/LogTypes.h"

inline constexpr LogLevel keLogLevelDefault  = kVerbose;
inline constexpr LogLevel keLogLevelTemp     = kVerbose;

inline constexpr LogLevel keLogLevelAudio    = kVerbose;
inline constexpr LogLevel keLogLevelGraphics = kVerbose;
inline constexpr LogLevel keLogLevelLoading  = kVerbose;
inline constexpr LogLevel keLogLevelNavData  = kVerbose;
inline constexpr LogLevel keLogLevelNetwork  = kVerbose;
inline constexpr LogLevel keLogLevelInput    = kVerbose;

#include "Common.h"
