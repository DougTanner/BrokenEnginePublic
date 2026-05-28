# `/Engine/Source/Debug/` - Client-Only Vulkan Debug Utilities

Vulkan enum-to-string conversion for logging and error messages. Client-only; included via `Engine.h` under `BT_CLIENT`.

## Architecture Notes

Compile-time type dispatch via `std::is_same_v` selects the lookup map per Vulkan enum type. Scratch storage uses the shared workbuffer (`PushBuffer`), not `thread_local`.

**Lifetime contract**: conversion returns a `common::ScopedWorkbufferAllocation<const char*>` owning the scratch frame. The yielded `const char*` is valid only while that scoped object lives — its dtor pops the workbuffer frame regardless of whether the pointer aims at static map data (logging path) or the scratch itself (`std::to_chars` fallback path), via the RAII `Adopt<U>` cross-type ownership transfer. Callers must keep the return value on the stack across any use of the string.

**Non-logging builds**: with `kbLogging` false, lookup tables drop out and conversion falls back to `std::to_chars` returning the numeric enum. Call sites always receive a valid C-string regardless of build flavor.

**`std::formatter<VkResult>`**: pulls scratch from `common::gpThreadLocal->mWorkbuffer` — only safe on threads with an initialized thread-local (see [Common/CLAUDE.md](../../../Common/CLAUDE.md)).

Supported enum set is closed; any value absent from the map trips `DEBUG_BREAK()` (then returns `"UNKNOWN_VK_ENUM"`), flagging tables lagging the Vulkan SDK.
