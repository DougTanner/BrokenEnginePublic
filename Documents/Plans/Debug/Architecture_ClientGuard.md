# Architecture: Client Guard

Source: /external-architecture-review on Engine/Source/Debug

## Changes

### Engine/Source/Engine.h
- Wrap the `#include "Debug/EnumToString.h"` (line 4) in `#ifdef BT_CLIENT` / `#endif` — all callers are in Graphics/ (client-only), server builds parse all Vulkan map data and instantiate `gEnumToString` for no reason [~5m]

## Verification Notes
- PASS: Item verified against source
- Confirmed `#include "Debug/EnumToString.h"` is at line 4 of Engine.h, outside the existing `#ifdef BT_CLIENT` block (which starts at line 28)
- Confirmed all `gEnumToString` callers are in Graphics/ .cpp files (GraphicsUtils.cpp, Graphics.cpp, InstanceManager.cpp, SwapchainManager.cpp) which are all client-only
- ProfileManagerBase.cpp uses VkResult but only inside `#if defined(BT_CLIENT)` blocks
- No server-side code (Network/, Server/) references VkResult or gEnumToString
- The `std::formatter<VkResult>` at the bottom of EnumToString.h will also be excluded from server builds, which is correct since no server code formats VkResult values
