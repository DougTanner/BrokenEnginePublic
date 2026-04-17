# Refactor: ScopedSuppressAllocationTracking Consistency

Source: /external-refactor-clean on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

The engine's memory hub requires `ScopedSuppressAllocationTracking` + `// Heap:` comment at every unavoidable main-loop heap allocation (Engine/Source/Memory/CLAUDE.md). Coverage is present but spotty: ~25 guards in the Network subtree lack the `// Heap:` comment explaining WHY the heap is unavoidable. Variable-name consistency is also split 50/50 between `scopedSuppressAllocationTracking` and `suppressAllocationTracking`.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp
- Add `// Heap:` one-line explanation above each `ScopedSuppressAllocationTracking` at lines 41, 61, 92, 116, 164, 232, 441, 506, 552, 631, 717. Each comment ≤ one line — name the container that's growing and why it can't live in the workbuffer (e.g., "// Heap: mClients map grows across ticks; long-lived state."). [~20m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp
- Add `// Heap:` comments at lines 196 (Disconnects), 249 (DetectPlayerDeaths inner). [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp
- Add `// Heap:` comments at lines 20, 97, 165. [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp
- Add `// Heap:` comments at lines 49, 67, 81, 185, 358, 392. [~10m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.cpp
- Add `// Heap:` comments at lines 38, 121. [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp
- Add `// Heap:` comments at lines 17, 31. [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp
- Add `// Heap:` comment at line 14. [~5m]

### Engine/Source/Network/Client/ClientSessionBase.cpp
- Lines 17, 25, 50 are setup/teardown (ConnectToServer, DisconnectFromServerBase, StartServerDiscovery). Evaluate: are these main-loop or init? If init, the guard can be removed; if main-loop reachable, add `// Heap:` comments. [~15m]

### Engine/Source/Network/Server/Server.cpp
- Line 38 (destructor) — teardown; remove guard or add `// Heap: teardown`. [~5m]
- Line 290 (`BufferFullFrame`) — add `// Heap:` comment explaining the per-coord ring buffer allocation. [~5m]

### Engine/Source/Network/Client/Client.cpp
- Lines 17, 69, 301 — constructor/destructor/`Disconnect`. These are startup/teardown; remove guards that aren't load-bearing. If removing breaks a debug-break, add `// Heap: startup` comment instead. [~10m]

### Variable-name standardization (single-pass rename)
- Across all Network cpps, rename all `scopedSuppressAllocationTracking` locals to `suppressAllocationTracking` (shorter form). Rule-3 Hungarian: local variables take no `sc`/`scoped` prefix. Use Grep + Edit with `replace_all` per file. [~20m]

## Verification
- Rebuild both configs. No behavior change expected.
- Run with `BT_ALLOCATION_TRACKING` enabled; confirm no new `DEBUG_BREAK()` fires in the main loop.

## Verification Notes
Verified — all listed `ScopedSuppressAllocationTracking` line numbers cross-checked and accurate at commit d08678d3:
- `ServerFleetManager.cpp` 41, 61, 92, 116, 164, 232, 441, 506, 552, 631, 717 — all confirmed.
- `Server.cpp` 38 (dtor), 290 (`BufferFullFrame`) — confirmed.
- `Client.cpp` 17, 69, 301 — confirmed (note: line 69 is dtor `enet_host_destroy` wrap; line 301 is `Disconnect`).
- `ClientSessionBase.cpp` 17, 25, 50 — confirmed (setup/teardown paths; mostly removable).
- Variable-name standardization: rename `scopedSuppressAllocationTracking` → `suppressAllocationTracking` aligns with Hungarian rule-3 (no `sc`/`scoped` prefix on locals).
