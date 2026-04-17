# Refactor: Dead Code Removal

Source: /external-refactor-clean on Engine/Source/Network (recursive)

## Changes

### Engine/Source/Network/Client/Client.cpp
- Lines 41-42: constructor populates `localAddress` and `pcServerAddress[64]` but never references them (no LOG uses them). Delete. [~5m]
- Lines 103-107: `ScopedLogIndent scopedLogIndent;`, `pcServerAddress[64]`, `localAddress` declared then immediately out of scope without a read. Delete. [~5m]

### Engine/Source/Network/Server/ServerReceive.cpp
- Lines 468, 485, 502, 519, 536: 5 unreachable `if (iSize < 1) return;` — superseded by the line-171 gate in `Server::Receive`. (Also covered by `Refactor_GuardConsolidation.md`; de-dupe when executing — do whichever plan runs first.) [~5m]

### Engine/Source/Network/Client/ClientSessionBase.cpp
- Lines 55-79: `PollLANDiscovery` returns `bool` but the only caller (`ClientSession.cpp:282`) discards the return value. Change return type to `void`. [~10m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp
- Line 613: `LookupFleetWantedCoord` takes `[[maybe_unused]] engine::GridCoord spawnCoord` that's never consumed. Audit callers (Grep for `LookupFleetWantedCoord`); if all callers discard the parameter at the call site, remove it from the signature. If callers pass meaningful data, the `[[maybe_unused]]` is covering a bug — investigate. [~15m]

## Verification
- Rebuild both configs after each deletion.
- No behavior change expected.

## Verification Notes
Verified at commit d08678d3:
- `Client.cpp:39-42` and `:103-107`: `pcServerAddress[64]` and `localAddress` are populated via `enet_address_get_host_ip` / `enet_socket_get_address` but never read (no `LOG` references them). Safe to delete. Corrected line refs from original (41-42 and 103-107).
- `ServerReceive.cpp` 468, 485, 502, 519, 536: unreachable `iSize < 1` checks confirmed — `Server.cpp:171` entry gate dominates. (De-dupe with `Refactor_GuardConsolidation.md`.)
- `ClientSessionBase.cpp:55-79`: `PollLANDiscovery` return value discarded at `ClientSession.cpp:282` — corrected line ref from 63-77.
- `ServerFleetManager.cpp:613`: `[[maybe_unused]] engine::GridCoord spawnCoord` confirmed; `LookupFleetWantedCoord` caller audit required before deletion.
