# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/Debug

## Changes

### Engine/Source/Debug/EnumToString.h
- Remove `mVkDebugReportFlagsEXTMap` (lines 96–103) — zero callers of `Convert(VkDebugReportFlagsEXT)` anywhere in engine source. Codebase fully migrated to `VK_DEBUG_UTILS_*` [~5m]
- Remove the `if constexpr (std::is_same_v<T, VkDebugReportFlagsEXT>)` branch (lines 23–28) — dead dispatch for removed map [~5m]
- Remove `VK_COLORSPACE_SRGB_NONLINEAR_KHR` entry (line 93) — deprecated alias, same integer value as `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` (line 78), silently discarded by unordered_map [~5m]
- Remove `VK_PRESENT_MODE_MAX_ENUM_KHR` entry (line 311) — sentinel value (0x7FFFFFFF) to force 32-bit enum size, never returned by drivers or queried by code [~5m]

## Verification Notes
- PASS: All 4 items verified against source
- `VkDebugReportFlagsEXT`: Confirmed zero callers outside ThirdParty/ (which is not engine code). Map and branch are dead code
- `VK_COLORSPACE_SRGB_NONLINEAR_KHR`: Confirmed duplicate key (same int as VK_COLOR_SPACE_SRGB_NONLINEAR_KHR on line 78). Note: InstanceManager.h:51 uses the deprecated alias as a default value for a member, but that is unrelated to the enum-to-string map entry
- `VK_PRESENT_MODE_MAX_ENUM_KHR`: Confirmed sentinel, only appears in EnumToString.h and ThirdParty/
- Line numbers all verified correct
