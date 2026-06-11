# Architecture: Client/Server Guard Scope

## Context

Source: /external-architecture-review on `Engine/Source/Profile`. Three guard-placement misalignments between what actually differs per build and what is gated:

1. The entire body of `UpdateProfileText()` is `#if defined(BT_CLIENT)` (`ProfileManagerBase.cpp:392-431`), including build-shared work: `SmoothCpuTimers()` (`:395`) and the `mSmoothedAllocations` latch + `giAllocationsThisFrame.exchange(0)` reset (`:397-398`). The server must therefore remember to call `SmoothCpuTimers()` itself (`ServerDisplay.cpp:80`) — a contract that lives only in CLAUDE.md prose, and a future server caller of `UpdateProfileText()` gets a silent no-op. `giAllocationsThisFrame` is also never reset on the server (grows monotonically; per-timer diffs still work via the clamped diff at `:183`, but "per-frame counter" is untrue server-side).
2. GPU-timer state compiles into the server build: the methods and `mVkQueryPool` are gated (`ProfileManagerBase.h:188-196`, `:342-344`) but the `GpuTimers` enum (`:90-128`), `GpuTimer` struct (`:130-135`), `mGpuTimers` array (`:273-309`), and `GetGpuTimers()` (`:216`) are not — the server carries ~34 KB of dead `Smoothed` rings. Every consumer is client-only (the `LogTimers` GPU section is gated at `ProfileManagerBase.cpp:348-362`; `FormatFpsHeader`/`FormatGpuScreen` live in the `ProfileScreens.cpp:110-355` client span).
3. The mimalloc stat fields (`ProfileManagerBase.h:224-229`) are gated only on `!ENABLE_CRT_DEBUG_HEAP`, but their sole writer/readers are server-only (`ServerDisplay.cpp:74-77`, `:382-385`, `:535-538`) — four dead int64s on the client.

## Design

### Engine/Source/Profile/ProfileManagerBase.cpp
- Restructure `UpdateProfileText()` (`:388-433`): hoist the shared prefix — `ScopedCpuProfile(kCpuTimerUpdateProfileText)`, `SmoothCpuTimers()`, and the `mSmoothedAllocations` latch/reset (`:393-398`) — out of the `#if defined(BT_CLIENT)` span; keep the GPU-timer `Update()` loop, the `meProfileScreen` early-out, and the screen formatting (`:400-430`) gated. [~15m]

### Projects/BrokenEngineSandbox/Source/Server/ServerDisplay.cpp
- `ServerUpdateDisplayStats()`: replace the direct `gpProfileManager->SmoothCpuTimers()` call (`:80`) with `UpdateProfileText()` so the server runs the same shared smoothing + allocation latch; per-tick cadence unchanged. [~5m]

### Engine/Source/Profile/ProfileManagerBase.h
- Wrap the `GpuTimers` enum (`:90-128`), `GpuTimer` struct (`:130-135`), `mGpuTimers` array (`:273-309`), and `GetGpuTimers()` (`:216`) in the existing `BT_CLIENT` spans. [~10m]
- Change the mimalloc fields gate (`:224-229`) to `#if defined(BT_SERVER) && !defined(ENABLE_CRT_DEBUG_HEAP)` (the ServerDisplay write site already carries the heap-debug gate at `:67`). [~5m]

### Engine/Source/Profile/CLAUDE.md
- Rewrite the `:11` server contract sentence: the server display now calls `UpdateProfileText()`; the "must call `SmoothCpuTimers()` itself" special case is gone. [~5m]

## Critical files
- Engine/Source/Profile/ProfileManagerBase.h
- Engine/Source/Profile/ProfileManagerBase.cpp
- Projects/BrokenEngineSandbox/Source/Server/ServerDisplay.cpp
- Engine/Source/Profile/CLAUDE.md

## Acceptance criteria
- Both client and server build clean; the server GDI Profile tab's timers/counters behave as before.
- `giAllocationsThisFrame` resets once per tick on the server (per-timer allocation columns unchanged in mechanism).

## Out of scope
- Demoting `SmoothCpuTimers()` from public — revisit after this lands (the ServerDisplay swap removes its last external caller).
- The `bSmoothNow` double-latch inside `SmoothCpuTimers` (`Architecture_SmoothNowDoubleLatch.md`) — disjoint from this guard restructure.
- Relocating the mimalloc stats to a server-only type — YAGNI; gating the fields suffices.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — profiling/display state only; the header layout change is not serialized.
- `giAllocationsThisFrame` is declared in `Memory/MemoryManager.h`; `ProfileManagerBase.cpp` currently reaches it via a fragile transitive chain — land `Architecture_IncludeHygiene.md`'s direct-include item before or with this plan.
- Grill decision (pre-staged): server adoption shape — swap `ServerDisplay.cpp:80` to `UpdateProfileText()` (recommended; deletes the prose contract) vs hoist-only and keep the direct `SmoothCpuTimers()` call (server then keeps the monotonic `giAllocationsThisFrame`).

## Verification Notes
All items verified against source (2026-06-10 pass):
- GPU-timer surface has zero server-side consumers: repo grep shows `GetGpuTimers`/`mGpuTimers`/`kGpuTimer*` only in client-gated engine code (`ProfileScreens.cpp:110-355` client span, `LogTimers` GPU section `:348-362`, `UpdateProfileText` loop `:400-403` inside the `BT_CLIENT` span, `Graphics.cpp`/`CommandBufferManager.cpp` which are client-only); nothing under `Projects/`. Size estimate checks out (~34 `GpuTimer` × ~1 KB `Smoothed<int64_t,128>` ring).
- mimalloc fields confirmed written/read only in `ServerDisplay.cpp` (`:74-77` writes, `:382-385`/`:535-538` reads), each site already under `!ENABLE_CRT_DEBUG_HEAP` — the compound `BT_SERVER && !ENABLE_CRT_DEBUG_HEAP` gate is safe.
- Server `UpdateProfileText()` adoption verified harmless: the hoisted `ScopedCpuProfile(kCpuTimerUpdateProfileText)` just accumulates a tiny per-tick scope latched by the same `SmoothCpuTimers` call (hidden by the `<50us` visibility cull); `meProfileScreen` is never read in the server display path; the new per-tick `giAllocationsThisFrame.exchange(0)` runs outside every server timer window (`Main.cpp:309` sits after `ServerUpdate`, outside the `kCpuTimerMessagesAndInput` bracket at `:263-282`), so no per-timer diff-clamp distortion.
- `giAllocationsThisFrame` monotonic-on-server claim verified — the sole reset is the client-gated `:397`; per-timer diffs clamp at `:183` as stated.
- Cross-references to `Architecture_SmoothNowDoubleLatch.md` and `Architecture_IncludeHygiene.md` consistent (the include-ordering dependency in Notes matches that plan's direct-include item).
