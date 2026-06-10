# `/Engine/Source/Debug/` - Client-Only Vulkan Debug Utilities

Vulkan enum-to-string conversion for logging and error messages, exposed as the inline value global `engine::gEnumToString` (not a `gp*` manager — no construction-order dependency). Client-only with no internal `BT_CLIENT` guard: the `Engine.h` include placement (inside the client span) and client-only vcxproj listing are the gates.

## Architecture Notes

Compile-time type dispatch via `std::is_same_v` selects the lookup map per Vulkan enum type. Scratch storage uses the caller-provided workbuffer (`PushBuffer`), not `thread_local` — the maps are immutable after static init, so concurrent conversions are safe as long as each thread passes its own workbuffer.

**Lifetime contract**: conversion returns a `common::ScopedWorkbufferAllocation<const char*>` owning the scratch frame. The yielded `const char*` is valid only while that scoped object lives — its dtor pops the workbuffer frame regardless of whether the pointer aims at static map data (logging path) or the scratch itself (`std::to_chars` fallback path), via the RAII `Adopt<U>` cross-type ownership transfer. Bind the result to a named local for the duration of any use of the string, or pass it directly as a `LOG` argument — the companion `std::formatter` on the allocation type (`Common/Workbuffer.h`) keeps the temporary alive for the call.

**Non-logging builds**: with `kbLogging` false, the lookup code compiles out (the maps remain) and conversion falls back to `std::to_chars` rendering the numeric enum value. Call sites always receive a valid C-string regardless of build flavor.

**`std::formatter<VkResult>`**: pulls scratch from `common::gpThreadLocal->mWorkbuffer` when present, else falls back to a numeric `std::to_chars` rendering on a local stack buffer — safe on any thread regardless of thread-local state (see [Common/CLAUDE.md](../../../Common/CLAUDE.md)).

Supported enum set is closed (`VkResult`, `VkFormat`, `VkObjectType`, `VkPresentModeKHR`, `VkColorSpaceKHR`); a value absent from its map trips `DEBUG_BREAK()` then returns `"UNKNOWN_VK_ENUM"`, flagging tables lagging the Vulkan SDK. Adding a new enum type requires both a new map and a new dispatch branch.
