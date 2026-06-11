# Architecture: VkEnum String Library Replacement

## Context

Source: `/external-architecture-review` on `Engine/Source/Debug` (non-recursive). `engine::EnumToString` (`Engine/Source/Debug/EnumToString.h`, 409 lines) hand-maintains five `std::unordered_map<VkEnum, std::string_view>` tables (~293 entries, ~312 lines of data, `:75-386`) for logging Vulkan enums. The LunarG SDK already on the include path (`$(VK_SDK_PATH)\Include`, `BrokenEngineSandbox.vcxproj:101`) ships the generated `vulkan/vk_enum_string_helper.h` (KhronosGroup/Vulkan-Utility-Libraries, **Apache-2.0** — on the `ThirdParty/CLAUDE.md` allow list; verified present in SDK 1.4.341.1), providing `string_VkResult`/`string_VkFormat`/`string_VkObjectType`/`string_VkPresentModeKHR`/`string_VkColorSpaceKHR` as allocation-free `static inline const char*` switch functions covering every enumerator including extensions — the exact header the Vulkan validation layers use for their own enum-to-string output.

Replacing the hand tables deletes ~400 in-house lines, removes five heap-allocating static-init maps from every client launch, retires the `Convert` workbuffer/RAII lifetime contract, and permanently eliminates the table-lags-SDK failure class. That class is live today: the `VkResult` map (`EnumToString.h:355-386`) is missing driver-plausible `VK_ERROR_UNKNOWN`, `VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS`, `VK_PIPELINE_COMPILE_REQUIRED`, and `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`; the `VkColorSpaceKHR` map (`:75-92`) is missing `VK_COLOR_SPACE_DISPLAY_NATIVE_AMD`, reachable from the driver-enumerated surface-format loop (`InstanceManager.cpp:588`) on AMD FreeSync-HDR systems. Each absent value trips `DEBUG_BREAK()` inside the logger (`EnumToString.h:60`) on a legitimate driver value — masking the real failure under investigation.

## Design

### Common/ExternalHeaders.h
- Add `#include <vulkan/vk_enum_string_helper.h>` after `<Volk/volk.h>` (`ExternalHeaders.h:230`). Note: the Vulkan include block there is **unguarded** — shared by client, server, and DataPacker builds (only PerlinNoise above it is `BT_CLIENT`-gated). Gate the new include `#if defined(BT_CLIENT)` to keep the server/DataPacker PCH untouched (all consumers are client-only) — unless the formatter moves to the shared `Engine.h` block, which would require it unguarded (see below). The helper includes `<vulkan/vulkan.h>` itself and is safe under the existing `VK_NO_PROTOTYPES`/`VK_USE_PLATFORM_WIN32_KHR` setup [~5m]

### Call sites — replace `gEnumToString.Convert(x, rWorkbuffer)` with direct `string_VkX(x)`
At each site, also delete the `ScopedWorkbufferAllocation` locals / workbuffer plumbing that existed solely for `Convert`:
- `CheckVkFailed` and `VkNameImpl` in `Engine/Source/Graphics/GraphicsUtils.cpp` (`:16`, `:51`) — `string_VkResult` / `string_VkObjectType` [~10m]
- Present-mode transition LOG in `Engine/Source/Graphics/Graphics.cpp` (`:375-376`) — `string_VkPresentModeKHR` [~5m]
- Present-mode logging in `Engine/Source/Graphics/Managers/SwapchainManager.cpp` (`:164`, `:175`) — `string_VkPresentModeKHR` [~5m]
- `Engine/Source/Graphics/Managers/InstanceManager.cpp` (`:308`, `:370`, `:587-588`, `:598-599`, `:614-615`, `:628-629`, `:651`, `:667`) — `string_VkResult` / `string_VkFormat` / `string_VkColorSpaceKHR` [~20m]

### Engine/Source/Debug/EnumToString.h
- Delete the `EnumToString` class, the five maps, and the `gEnumToString` inline global (`:6-389`) [~10m]
- Reduce the `std::formatter<VkResult>` specialization body to a single `string_VkResult(vkResult)` forward (drops the `gpThreadLocal` guard and the `to_chars` fallback — the helper is allocation-free and thread-safe everywhere). Trivial placement choice at execution: keep a slim `EnumToString.h` as the formatter's home, or move it into the engine formatter block in `Engine/Source/Engine.h` (`:99-145`) and delete the file plus the `Engine.h:29` include and the `BrokenEngineSandbox.vcxproj:333` / `.filters:213` entries. Caveat on the move variant: the `Engine.h` formatter block sits *outside* the `BT_CLIENT` span (compiles in the server build), so the relocated formatter must either be wrapped `#if defined(BT_CLIENT)` or the helper include in `ExternalHeaders.h` left unguarded — keep the two gating choices consistent [~20m]

### Documentation
- Update the `Adopt` comment in `Common/Workbuffer.h` (`:199`) — it names `EnumToString::Convert` as the motivating case for the cross-type ownership transfer; reword or cite a surviving exemplar [~5m]
- Rewrite or delete `Engine/Source/Debug/CLAUDE.md` (its lifetime-contract and map documentation all describes deleted machinery) and update the Debug subsystem row in `Engine/Source/CLAUDE.md` per the chosen file fate [~10m]

## Critical files
- `Engine/Source/Debug/EnumToString.h` (deleted or slimmed to the formatter)
- `Common/ExternalHeaders.h`
- `Engine/Source/Engine.h`
- `Engine/Source/Graphics/GraphicsUtils.cpp`, `Engine/Source/Graphics/Graphics.cpp`, `Engine/Source/Graphics/Managers/InstanceManager.cpp`, `Engine/Source/Graphics/Managers/SwapchainManager.cpp`
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` + `.filters` (only if the file is deleted)
- `Common/Workbuffer.h` (comment only)
- `Engine/Source/Debug/CLAUDE.md`, `Engine/Source/CLAUDE.md` (docs)

## Out of scope
- Refreshing the hand tables in place (adding the missing `VkResult`/`VkColorSpaceKHR` entries) — that is the fallback if the library is declined, not an addition to it (see Notes)
- The unguarded `gpThreadLocal` deref in `CheckVkFailed` (`GraphicsUtils.cpp:15`) — *not* mooted by this plan: the workbuffer stays for the exception-message `PushBuffer` (`:20`); only the `Convert` allocation goes away. Remains out of scope
- `kbLogging=false` string-stripping: current code renders numeric `to_chars` when `kbLogging` is false (`EnumToString.h:63-70`); the helper always returns name strings. Accepted change — no build flavor sets `kbLogging` false today (`Pch.h:7` is unconditionally `true`). Wrap call sites in `if constexpr (kbLogging)` only if that flavor is ever revived
- Any other ThirdParty additions or Vulkan SDK version changes

## Acceptance criteria
- Client builds with zero references to `gEnumToString` / `EnumToString`; server build untouched (the header was never in its vcxproj or `BT_CLIENT` span reach)
- Former `Convert` log sites print the same enum-name strings (superset coverage; unknown values now render as the helper's `"Unhandled VkX"` strings instead of breaking)

## Notes
- **Invariant exposure**: none — client-only logging path; no determinism/CRC, no `kiVersion`/`.pack` layout, no network protocol, no allocation-tracked main-loop change (the helper allocates nothing; the static-init maps it removes were startup-sanctioned anyway).
- **Behavior changes to accept**: (1) unknown enum values return `"Unhandled VkResult"`-style strings instead of tripping the `DEBUG_BREAK()` SDK-lag tripwire (`EnumToString.h:60`, documented as intended in `Debug/CLAUDE.md` and `Engine/WrapperGetIndexFailLoud.md`) — with a generated SDK-shipped header the tripwire's purpose is moot, since the table can no longer lag the SDK it ships with; (2) string coverage now tracks whatever SDK `VK_SDK_PATH` points at — the same coupling Volk/Vma already accept (`ThirdParty/CLAUDE.md:26` precedent).
- **Pre-staged grill decision**: per `ThirdParty/CLAUDE.md`, adding a library requires explicit user approval even when no files are imported (SDK-header precedent: Volk/Vma). Approve `vk_enum_string_helper.h` (recommended), or decline and fall back to refreshing the hand tables — add the missing `VkResult` entries and `VK_COLOR_SPACE_DISPLAY_NATIVE_AMD`, optionally collapsing `Convert`'s five duplicated `if constexpr` dispatch blocks into a single type-keyed map accessor.

## Verification Notes (2026-06-10)

Phase-4 verification against current source; all file paths and line numbers checked.

- **Line citations confirmed**: `EnumToString.h` class+maps+`gEnumToString` `:6-389` (file is 409 lines; map data `:75-386` = 312 lines, 293 entries exactly: 15+193+7+49+29); `VkColorSpaceKHR` map `:75-92`; `VkResult` map `:355-386`; `DEBUG_BREAK()` `:60`; `to_chars` fallback `:63-70`; `std::formatter<VkResult>` `:393-409`. `Engine.h:29` include inside the `BT_CLIENT` span (`:21-84`); formatter block `:99-145`. `ExternalHeaders.h:230` is `<Volk/volk.h>`. `Workbuffer.h:199` `Adopt` comment names `EnumToString::Convert`. `BrokenEngineSandbox.vcxproj:333` / `.filters:213` confirmed; `BrokenEngineSandboxServer.vcxproj` has no EnumToString entry. `Pch.h:7` `kbLogging = true` unconditional. `BrokenEngineSandbox.vcxproj:101` carries `$(VK_SDK_PATH)\Include`.
- **Missing enumerators confirmed**: `VK_ERROR_UNKNOWN`, `VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS`, `VK_PIPELINE_COMPILE_REQUIRED`, `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`, `VK_COLOR_SPACE_DISPLAY_NATIVE_AMD` all absent from `EnumToString.h`; all present in the SDK helper.
- **Call-site list complete**: repo-wide grep for `gEnumToString` matches exactly the sites listed (GraphicsUtils.cpp:16,51; Graphics.cpp:375-376; SwapchainManager.cpp:164,175; InstanceManager.cpp:308,370,587,588,598,599,614,615,628,629,651,667) plus the header itself and docs. Enum type at every site matches the named `string_VkX` replacement (verified against declarations: `mFramebufferVkFormat`/`mFramebufferVkColorSpace` are `VkFormat`/`VkColorSpaceKHR`, InstanceManager.h:55-56).
- **SDK helper verified on this machine** (`VULKAN_SDK = C:\SDK\VulkanSDK\1.4.341.1`): `Include/vulkan/vk_enum_string_helper.h` exists, `SPDX-License-Identifier: Apache-2.0` (Khronos/Valve/LunarG, generated), provides `string_VkResult`/`string_VkObjectType`/`string_VkFormat`/`string_VkPresentModeKHR`/`string_VkColorSpaceKHR` as `static inline const char*` switches with `"Unhandled VkX"` defaults, and includes `<vulkan/vulkan.h>` itself. Apache-2.0 is on the `ThirdParty/CLAUDE.md` allow list; no enum-string library exists under `ThirdParty/`; `ThirdParty/CLAUDE.md:26` is the Volk/Vma SDK-header precedent as cited. Other machines need an SDK shipping the helper — any modern LunarG SDK does.
- **No plan overlap**: only other `EnumToString` mention in `Documents/` is `Engine/WrapperGetIndexFailLoud.md:28`, which explicitly *excludes* the `:60` fallback as working-as-intended — consistent with this plan's accepted-behavior-change framing (tripwire moot once strings are generated from the SDK they ship with).
- **Corrections applied during verification**: (1) `ExternalHeaders.h` Vulkan include block is unguarded (shared client/server/DataPacker), not a "client Vulkan span" — bullet reworded with the gating decision; (2) `Engine.h` formatter-block move variant needs a `BT_CLIENT` guard or an unguarded include — caveat added; (3) `GraphicsUtils.cpp:15` `gpThreadLocal` deref is not mooted (workbuffer still feeds the exception buffer at `:20`) — out-of-scope item reworded.
- **Order.md row**: added in the same session (row #10) as **Tier Small, Effort 2, Impact 3, Risks 1, Score 0**. The verifier suggested Effort 3 / Medium, but the plan's own item estimates total ~90 minutes within one subsystem — the Effort-2 anchor ("one subsystem, half- to full-day, narrow scope"); Impact 3 (removes ~400 lines + a live `DEBUG_BREAK` failure class) and Risks 1 (mechanical, compile-checked, client-only logging) stand as suggested.
