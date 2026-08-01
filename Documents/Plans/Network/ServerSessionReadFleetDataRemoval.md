<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T21:20:45.562Z","dependsOn":[]} -->
# Remove the Unused ServerSession::ReadFleetData Wrapper

## Context

Found while verifying the completed `FleetRandomStateLoadValidation` change; pre-existing and outside that change's boundary, which only touched the random-engine state readers.

`ServerSession::ReadFleetData` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:639-644`, declared at `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.h:52`) has no callers. A repository-wide search for `ReadFleetData` returns only the free function `game::ReadFleetData` (`ServerFleetSerialization.h:14`, `ServerFleetSerialization.cpp:138`), the live call to it from the load path, this wrapper's own declaration and definition, and two comments that refer to the free function (`ServerFleetManager.cpp:550`, `ServerFleetManager.h:80`, `ServerSession.cpp:626`, `GameSaveLoad.cpp:465`).

Its sibling `ServerSession::WriteFleetData` (`ServerSession.cpp:634-637`, declared at `ServerSession.h:51`) is live — `GameSaveLoad.cpp:1128` calls it — so the pair is deliberately asymmetric today, not uniformly dead.

Leaving the wrapper in place is not merely unused code; it contradicts a live invariant. The real load path reads fleet data into a `StagedGrid` that is adopted only after the whole read succeeds (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:1205` reads into `rStagedGrid`, the post-read stream gate at `:1252-1256` can still abort, and `AdoptGrid` at `:1262-1272` installs the staged fleets, GUID map, and random engine only after that). The wrapper instead reads straight into the live `mpFleetManager->mFleets`, `mpFleetManager->mGuidToClientId`, and `mpFleetManager->mRandomEngine`. Anyone wiring it up would silently lose the "a rejected load installs nothing" guarantee, and would do so in a place that looks like the sanctioned session API.

## Design

Delete the wrapper rather than route it through staging. There is no caller to preserve, so adding a staged variant would be building an interface for a hypothetical user.

1. Delete the definition `ServerSession::ReadFleetData` at `ServerSession.cpp:639-644`, including its `ScopedSuppressAllocationTracking` guard and `// Heap:` comment, which exist only for that body.
2. Delete the declaration at `ServerSession.h:52`.
3. Leave `WriteFleetData` (`ServerSession.h:51`, `ServerSession.cpp:634-637`) untouched — it is live through `GameSaveLoad.cpp:1128`. The resulting write-only asymmetry is correct: the read side is owned by the staged load path.
4. Leave the free `game::ReadFleetData` and every existing comment that names it untouched; those comments describe the free function, not the wrapper.

Decision already made, do not re-open: delete rather than convert the wrapper to a staged-load form or mark it as intentionally reserved.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — definition at `:639-644`; edited.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.h` — declaration at `:52`; edited.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — the live staged load at `:1205`, `:1252-1256`, `:1262-1272` and the live `WriteFleetData` call at `:1128`; read-only, the evidence that the wrapper is both unused and contrary to the staged guarantee.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.h` / `.cpp` — the free `ReadFleetData` that stays; read-only.

## In scope

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — delete the whole `ServerSession::ReadFleetData` definition at `:639-644`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.h` — delete the `void ReadFleetData(std::fstream& rFileStream);` declaration at `:52`.

## Out of scope

- `ServerSession::WriteFleetData` and its declaration; it is live.
- The free `game::WriteFleetData` / `game::ReadFleetData` in `ServerFleetSerialization.*` and their signatures.
- The staged load path, `AdoptGrid`, fleet manager state, save format, `Frame::kiVersion`, and any serialization or wire behavior.
- Rewording the existing comments in `ServerFleetManager.cpp:550`, `ServerFleetManager.h:80`, `ServerSession.cpp:626`, and `GameSaveLoad.cpp:465`; they describe the free function and remain correct.
- Any other unused-member sweep in `ServerSession` or elsewhere.

## Risk tier and invariants

Change Workflow Tier 1 — trigger: mechanical removal of unreachable code with no public signature or invariant exposure. Deleting a member with no callers cannot change runtime behavior; a missed caller would be a compile error, so compilation of the server build is the decisive check. No determinism/CRC, wire, serialization, save/replay, threading, allocation, shader, or project-membership exposure.

## Acceptance criteria

- A repository-wide search for `ServerSession::ReadFleetData` and for the declaration text returns nothing after the change, while `game::ReadFleetData` still resolves at `GameSaveLoad.cpp:1205`.
- `WriteFleetData` is unchanged and `GameSaveLoad.cpp:1128` still compiles against it.
- The Debug server build compiles and links.

## Notes

- Line citations are tip-of-2026-08-01; refresh them if `ServerSession` moves first.
