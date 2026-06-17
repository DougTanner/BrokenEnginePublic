#pragma once

namespace common
{

// Log levels (priority hierarchy: Verbose < Debug < Info < Warning < Error)
enum class LogLevel : int8_t
{
	kVerbose = 0,
	kDebug   = 1,
	kInfo    = 2,
	kWarning = 3,
	kError   = 4,
};

// Log categories — dense 0-based indices used directly as array subscripts (gLogRingBuffers, kpcLogCategoryNames,
// keLogLevels). Append new categories immediately before kCount; never reorder or leave gaps.
enum class LogCategory : int8_t
{
	kDefault  = 0,
	kTemp     = 1,

	kAudio    = 2,
	kGraphics = 3,
	kLoading  = 4,
	kNavData  = 5,
	kNetwork  = 6,
	kInput    = 7,

	kCount, // index-only sentinel — no call-site alias, no name/level table entry
};
inline constexpr int64_t kiLogCategoryCount = static_cast<int64_t>(LogCategory::kCount);

} // namespace common

// Level aliases
inline constexpr common::LogLevel kVerbose = common::LogLevel::kVerbose;
inline constexpr common::LogLevel kDebug   = common::LogLevel::kDebug;
inline constexpr common::LogLevel kInfo    = common::LogLevel::kInfo;
inline constexpr common::LogLevel kWarning = common::LogLevel::kWarning;
inline constexpr common::LogLevel kError   = common::LogLevel::kError;

// Category aliases (ChunkFlags uses kChunkAudio to avoid collision with kAudio here)
inline constexpr common::LogCategory kDefault  = common::LogCategory::kDefault;
inline constexpr common::LogCategory kTemp     = common::LogCategory::kTemp;

inline constexpr common::LogCategory kAudio    = common::LogCategory::kAudio;
inline constexpr common::LogCategory kGraphics = common::LogCategory::kGraphics;
inline constexpr common::LogCategory kLoading  = common::LogCategory::kLoading;
inline constexpr common::LogCategory kNavData  = common::LogCategory::kNavData;
inline constexpr common::LogCategory kNetwork  = common::LogCategory::kNetwork;
inline constexpr common::LogCategory kInput    = common::LogCategory::kInput;

using common::LogLevel;
using common::LogCategory;
