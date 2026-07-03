# Architecture: Profile Index-Routing Virtual Layer Collapse

## Context
Source: /external-architecture-review on Engine/Source (recursive). The engine↔game profile-timer seam is a shallow virtual layer: six virtuals each hide a single comparison, the base bodies are unreachable in practice, and the game override restates the same routing four times — plus virtual dispatch inside the hot `CpuStart`/`CpuStop` mutex sections.

## Design

### Engine/Source/Profile/ProfileManagerBase.{h,cpp}
- Replace the six index-routing virtuals (`GetCpuCounter`/`GetCpuTimer`/`GetCpuCounterName`/`GetCpuTimerName`/`GetCpuCounterCount`/`GetCpuTimerCount`, `ProfileManagerBase.cpp:428-456`) with base-held spans/counts registered by the derived constructor (game passes its counter/timer arrays + names once at construction; base does the `iIndex < engine::kEngineCpuTimerCount ? engineArray : gameArray[iIndex - count]` routing in non-virtual code). The only instance ever constructed is `game::ProfileManager` (`Main.cpp:88`), whose overrides restate the same routing four times (`Projects/.../Profile/ProfileManager.cpp:55-73`) [~1h]
- Keep the existing `static_assert` index contracts (`ProfileManagerBase.cpp:12-13`) — they are the load-bearing part

## Critical files
- `Engine/Source/Profile/ProfileManagerBase.h`, `ProfileManagerBase.cpp`
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.{h,cpp}`

## Out of scope
- The overlay text/formatting layer (`ProfileScreens.cpp` — verified clean)
- The Tracy-complement idea from the library-replacement review (explicitly not filed as replacement)
- GPU timer machinery

## Notes
- Invariant exposure: none — profiling-only, no determinism/CRC/wire; `kbProfiling`-gated paths. The game CPU-timer enum the FPS overlay reads by name is a sanctioned engine→game contract — the registration API must preserve that name lookup
- Grill decision: registration shape — spans passed to base ctor vs virtual `GetTables()` called once; recommend ctor spans (deletes the virtuals entirely)

## Verification Notes
- Checked against the root CLAUDE.md engine-reads-game policy: this plan does NOT decouple/inject the sanctioned engine→game reads — the `static_assert` index contracts (`ProfileManagerBase.cpp:12-13`) and the FPS overlay's by-name `GetCpuTimer(game::kCpuTimerFrameUpdate)` read (`ProfileManagerBase.cpp:409`) are explicitly preserved. What collapses is only the virtual-accessor indirection (`ProfileManagerBase.cpp:428-456`), which the sanctioned direct-read policy makes unnecessary.
- All claims verified: sole instance is `game::ProfileManager` (`Main.cpp:88`); the game override restates the routing four times (`ProfileManager.cpp:55-73`, counts at `:75-83`); `GetCpuTimer`/`GetCpuTimerCount` are virtual-dispatched inside the `mCpuTimerMutex` sections of `CpuStart`/`CpuStop` (`ProfileManagerBase.cpp:112-169`) and in the locked formatter/log loops.
- Design constraint for execution: `ProfileManagerBase.h` is `Engine.h`'s first include and must stay engine-include-free (header comment, `ProfileManagerBase.h:3-5`), so the base header cannot name game types/constants — ctor-registered spans/pointers satisfy this; sizing base-held arrays by game counts would not. `ProfileManagerBase.cpp` already includes the game `Profile/ProfileManager.h`, so cpp-side routing may reference `game::` symbols freely.
- Profile/CLAUDE.md notes the accessors are deliberately outside `if constexpr (kbProfiling)` so the server GDI display renders zeros when profiling is off — the non-virtual replacements must keep that property (they do trivially).
