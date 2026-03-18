# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source (root files)

## Changes

### Engine/Source/GameBase.h
- Remove unused forward declaration `struct RawInput;` at line 22 [~2m]
- Remove `TickFramesAndRender()` declaration at line 110 [~2m]

### Engine/Source/GameBase.cpp
- Remove `TickFramesAndRender()` definition at lines 217-221 [~2m]

### Engine/Source/CrashReport.cpp
- Remove unused `#include "Memory/MemoryManager.h"` at line 3 — no symbols from this header are referenced [~2m]

### Engine/Source/Main.cpp
- Convert file-static `siBackgroundThreadCount` (line 11) to a local variable inside `MainThread()` — it is only written once and immediately passed to `Multithreading` constructor [~2m]

## Verification Notes

All items verified against source code on 2026-03-18:

1. **`TickFramesAndRender()`** — Confirmed dead. Only exists at GameBase.h:110 (declaration) and GameBase.cpp:217-221 (definition). Zero callers anywhere in the codebase. Main.cpp calls `ClientUpdate()` and `Render()` separately (lines 276, 284), making this wrapper redundant.
2. **`struct RawInput` forward decl** — Confirmed unused. The forward declaration at GameBase.h:22 is in the `engine` namespace but never referenced in GameBase.h or GameBase.cpp. The actual struct lives in `Input/RawInputManager.h`.
3. **`siBackgroundThreadCount`** — Confirmed safe to localize. Only referenced on Main.cpp lines 78, 81, 83, all within `MainThread()`. Written once per `#ifdef` branch and immediately consumed by `Multithreading` constructor.
4. **`#include "Memory/MemoryManager.h"` in CrashReport.cpp** — Confirmed unused. CrashReport.cpp references none of the symbols exported by MemoryManager.h (`ScopedSuppressAllocationTracking`, `EnableAllocationTracking`, `giAllocationsThisFrame`, `giAllocationTrackingSuppressed`).
