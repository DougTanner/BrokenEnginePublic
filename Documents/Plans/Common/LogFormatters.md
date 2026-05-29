# LogFormatters: Remove Heap Allocations and Spec-Discarding from std::formatter Specializations

## Context

`Common/Log/LogFormatters.h` defines the `std::formatter` specializations that let `LOG`/`std::format` print engine types. The whole point of the `Wb`/`WbV2` wrappers (and the `NEVER use float format specs in LOG` rule) is allocation-free logging on the main loop, because any heap allocation on the main-loop LOG path trips the allocation tracker and fires `DEBUG_BREAK()` (see `Engine/Source/Memory/CLAUDE.md`, root `CLAUDE.md`).

Two specializations defeat that guarantee by heap-allocating an owning `std::string` while formatting, and a group of others silently discard the parsed format spec. Verified directly against source (`LogFormatters.h`) and `Common/StringUtils.cpp`.

## Design

### 1. `std::formatter<std::wstring>` allocates an owning `std::string` (`LogFormatters.h:16-25`) — effort 2
Line 22 does `std::string string = common::ToString(rPath);`. Confirmed in `Common/StringUtils.cpp:8-19`: `ToString(std::wstring_view)` unconditionally does `std::string result(iSize, '\0');` (owning, heap on any non-SSO length) then `WideCharToMultiByte` into it. Any `LOG("{}", someWideString)` on the main loop heap-allocates and trips the tracker.

Fix: format into the thread-local workbuffer instead of an owning string, mirroring the `Wb`/`WbV2` pattern (`LogFormatters.h:189-217`):
- `common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;`
- `common::ScopedWorkbufferArena arena = rWorkbuffer.Push();`
- Reserve a char span via `arena.PushBuffer<char>(size)` (size from the first `WideCharToMultiByte(CP_UTF8, ... nullptr, 0 ...)` measuring call), convert straight into that span with a second `WideCharToMultiByte` call, then `return std::formatter<std::string_view>::format(view, rContext);` so width/align specs are honored.
- This needs a small narrow-conversion-into-arena helper, since `common::ToString` only returns an owning `std::string` today. Add it next to `ToString` in `Common/StringUtils.h`/`.cpp` (e.g. a `ToString` overload taking a `common::ScopedWorkbufferArena&` and returning a `std::string_view` into the arena), keeping the `CP_UTF8` pinning consistent with the existing `ToString`. KISS: a single helper serves both wstring and path.

Also rename the misnamed parameter `rPath` -> `rString` (it is a `std::wstring`, copy-paste artifact). Only touch this line since it is part of the same edit.

### 2. `std::formatter<std::filesystem::path>` allocates via `path::string()` (`LogFormatters.h:27-35`) — effort 1
Line 33 does `rPath.string()`, which returns a freshly-allocated owning `std::string` and on MSVC runs a locale-driven wide->narrow conversion (also inconsistent with the codebase's `CP_UTF8`-pinned `common::ToString`). Same main-loop allocation-tracker hazard; paths are logged in asset/streaming code.

Fix: use `rPath.native()` (a `wchar_t` view, no narrowing alloc) and run it through the same arena narrow-conversion helper added in item 1, then `format(arena.View(), rContext)`. Avoid `path::string()` (heap + locale) entirely.

### 3. Spec-discarding formatters write directly to `rContext.out()` (`LogFormatters.h` various) — effort 2
These derive from `std::formatter<std::string_view>` (so `{:>20}`, `{:.3}`, `{:^10}` parse without error) but call `std::format_to(rContext.out(), ...)` directly, discarding the parsed width/precision/fill — silently wrong, with no diagnostic, for any non-default spec: `XMFLOAT3` (43), `XMFLOAT4` (53), `XMFLOAT4A` (63), `XMVECTOR` (75), `VkFilter` (85), `VkSamplerAddressMode` (95), the four chrono specializations (105/115/125/135), `XMFLOAT2` (165), `Flags` (175), `RandomEngine` (185). Contrast: `std::string` (6-14), `int8_t`/`uint8_t` (139-157), and `Wb`/`WbV2` (189-217) forward through a base `format` and DO honor the spec.

Fix: assemble the text into the thread-local workbuffer (`ScopedWorkbufferArena`, using `Append`/`AppendFloat` exactly as `WbV2` already does for the vector cases), then hand the assembled `std::string_view` to `std::formatter<std::string_view>::format(view, rContext)` so the inherited spec is applied. This is the same pattern `Wb`/`WbV2` already use, so it folds cleanly into the items above. For the float-bearing vector types, routing components through `arena.AppendFloat(value, precision)` also aligns them with the engine's fixed-precision float-logging convention (choose a default precision matching `WbV2`).

## Critical files
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Log\LogFormatters.h` — all three fixes
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\StringUtils.h` / `Common\StringUtils.cpp` — add the arena narrow-conversion helper used by items 1 and 2
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Workbuffer.h` — reference for `ScopedWorkbufferArena` API (`Push`/`PushBuffer<T>`/`Append`/`AppendFloat`/`View`)

## Out of scope
- **Re-specializing `std::formatter` for standard types** (`std::string` line 6, `int8_t` 139, `uint8_t` 149, `std::chrono::*` 99-137). [namespace.std] permits user specializations only when they depend on a program-defined type; these specialize standard templates for non-program-defined types and are technically non-conforming. It is real but compiles and runs correctly on the current MSVC toolchain, is unrelated to the heap-alloc theme, and changing it risks breaking compilation. Track separately if it ever surfaces a real ODR/ambiguity error.
- chrono coverage gaps (only ns/us/ms/s specialized) — API completeness, not a bug.
- `gpThreadLocal` / workbuffer-headroom preconditions — project policy assumes valid globals; no defensive checks per directives.
- Vk enum integer rendering / enumerator names, hex rendering for `Flags`, by-value vs by-const-ref for `XMFLOAT*`, `template<>` vs `template <>` spacing, the redundant `XMFLOAT4A f4 {}` zero-init in the `XMVECTOR` body, the `VkFilter` param misnamed `vkSamplerAddressMode` — cosmetic/style; do not touch unrelated lines.
- M1 "{} on float is shortest-round-trip not fixed precision" as a standalone item — `{}` on float is allocation-free, so it is a consistency note folded into item 3, not a separate fix.

## Acceptance criteria
- `LOG("{}", someWideString)` and `LOG("{}", somePath)` perform no heap allocation (no allocation-tracker `DEBUG_BREAK()` on the main loop); conversion goes through `gpThreadLocal->mWorkbuffer` with `CP_UTF8`.
- The formatters in item 3 honor a passed width/align spec (e.g. `LOG("{:>20}", vec)` is right-aligned to 20) by routing through the base `std::formatter<std::string_view>::format`.
- No new owning `std::string`/`std::vector` is constructed inside any formatter `format` body.
- Affected projects build clean.

## Notes
- Validated against ground-truth source: `LogFormatters.h:16-35` heap-alloc claims and the `std::format_to(rContext.out(), ...)` spec-discarding sites all match the file exactly; `ToString`'s owning-string allocation confirmed in `StringUtils.cpp`. No phantom/hallucinated symbols in the kept findings.
- Model the fix on the existing `Wb`/`WbV2` specializations (`LogFormatters.h:189-217`) — they already use `ScopedWorkbufferArena` + base-formatter forwarding correctly and are the in-file reference pattern.
- Search tooling (Grep/Glob/Bash text output) was unreliable in this session, so main-loop caller enumeration could not be exhaustively listed; the formatters themselves allocate unconditionally whenever selected, so the fix is warranted regardless of which specific call sites are hot.
