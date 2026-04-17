# `/Engine/Source/Debug/` - Client-Only Vulkan Debug Utilities

Vulkan enum-to-string conversion for logging and error messages. Client-only; included via `Engine.h` under `BT_CLIENT`.

## Architecture Notes

Compile-time type dispatch via `std::is_same_v` selects the lookup map per Vulkan enum type. Scratch storage uses the shared workbuffer (`PushBuffer`), not `thread_local`.

**Lifetime contract**: conversion returns a `common::ScopedWorkbufferPop` owning the scratch allocation; the yielded `const char*` is valid only while that scoped object lives. Callers must keep the return value on the stack across any use of the string.

**Non-logging builds**: with `kbLogging` false, lookup tables drop out and conversion falls back to `std::to_chars` returning the numeric enum. Call sites always receive a valid C-string regardless of build flavor.

**`std::formatter<VkResult>`**: pulls scratch from `common::gpThreadLocal->mWorkbuffer` — only safe on threads with an initialized thread-local (see [Common/CLAUDE.md](../../../Common/CLAUDE.md)).

Supported enum set is closed; unmapped values trip `DEBUG_BREAK()` to flag tables lagging the Vulkan SDK.
