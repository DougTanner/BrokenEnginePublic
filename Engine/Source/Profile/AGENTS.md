# Profile - Runtime Performance Telemetry

Engine CPU/GPU profiling, boot timing, overlays, and profile dumps. `ProfileManagerBase` supplies engine counters and timers; the game-derived `game::gpProfileManager` extends the same contiguous index space.

## Index and Lifetime Contracts

- Game CPU counter and timer enums start at the matching engine count. Name tables preserve enum order and exact extent; compile-time checks enforce both contracts.
- The base constructor stores pointers to derived arrays and tables but must not dereference them before derived construction completes.
- Recording code compiles out through `if constexpr (kbProfiling)`. Shared CPU accessors and text formatters remain available to the server display even when profiling is disabled.

## CPU Timing

- Per-thread timer state is mutex-protected; worker threads may contribute, and text formatting takes the same lock. Cross-thread scopes require the explicit cross-thread mode.
- Allocation counts are process-wide samples over a timer's wall-clock window, not thread-local counts.
- Normal CPU timers latch into smoothing rings once per render frame. Timers that complete outside that cadence request latch-at-stop.
- Server raw timer samples are diagnostic-only and latch after all active-cell workers join, but only an accepted normal one-tick update at timescale 1/1 advances the sample. Paused, burst, recording/replay, and no-dispatch paths discard pending raw values and cancel an unpublished event arm, so those updates never publish a sample.
- A raw activation notification is a one-slot retained event: publication never overwrites an available payload, an overrun remains visible, and only an acknowledgement of the exact event sequence clears the slot. Keep this handshake outside deterministic frame state.

## GPU Timing

- GPU queries and ImGui output are client-only. Query reads never stall; `VK_NOT_READY` retains the previous smoothed result, and devices without timestamp support leave the pool unused.
- Each command buffer resets one contiguous timer range. Insert a timer into the range recorded by that command buffer and keep the enum and name table aligned.
- Timestamp endpoints use bottom-of-pipe ordering so adjacent regions measure completed preceding work.

## Presentation and Dumps

- `TickVisibilityCadence` is the single synchronized visibility decision for CPU timers, CPU counters, and GPU rows. UI views hide sustained zero rows; raw agent queries include them.
- Overlay screens may be game-owned, but engine formatters own shared CPU text. Server formatting must remain server-safe and allocation-disciplined.
- When `kbProfilingDump` is enabled, CSV sampling continues with the overlay hidden and without GPU timestamp support. Its diagnostic-log lifetime spans graphics recreation and ends with manager destruction.

## See Also

- Memory (`../Memory/AGENTS.md`) - Allocation tracking
- Game Profile (`../../../Projects/BrokenEngineSandbox/Source/Profile/AGENTS.md`)
