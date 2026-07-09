# Server Save Reports Success on a Failed Write

## Context

Surfaced by the AgentHarness2 session (agent `save` command). `GameSaveLoad::WriteGrid` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:405`) captures `engine::gpFileManager->WriteFileAtomically(...)`'s `bWritten` result but only folds it into a `kDebug` log (`:439`, `Committed: {}`) — it never propagates. `WriteGrid` is `void`, and both `ServerSave()` overloads (`:45`, `:50`) are `void`. So a failed atomic write (disk full, a filename that passes `BareFilenameParam` validation but the OS rejects) is invisible to callers.

Consequently the agent `save` command reports false success: `CommandSave` (`AgentCommandsServer.cpp:86`) calls `gpGame->mGameSaveLoad.ServerSave(file)` and unconditionally sets `rResult["file"]` — the SDK caller gets a success envelope for a write that silently failed. The client-requested path (`kClientSaveRequest` → `ServerSession.cpp:204` → no-arg `ServerSave()`) is fire-and-forget with no ack, so it likewise cannot surface a failure.

Rider (independent, same file): `BareFilenameParam` (`AgentCommandsServer.cpp:22`) rejects path separators, `:`, `..`, and empty — but lets Windows reserved device names through (`CON`, `NUL`, `PRN`, `AUX`, `COM1`-`COM9`, `LPT1`-`LPT9`, with or without extension). Sandboxed to appdata (no escape), but such a name writes to a device rather than a file; `WriteFileAtomically`'s rename step will typically fail, which — once failure is threaded out — becomes a reported error rather than a silent one, but rejecting up front gives a clearer message.

## Design

Thread the write result outward and report it:

1. **`GameSaveLoad::WriteGrid`** (`.h:40`, `.cpp:405`) → return `bool` (the captured `bWritten`). Keep the existing `Committed:` log. Existing void-context callers — `Quicksave` (`:40`), `Autosave` (`:96`), the F7 replay-grid write (`:304`) — discard the return (do **not** add `[[nodiscard]]`, or they must each `static_cast<void>` it; prefer no attribute to keep the touch minimal).
2. **`GameSaveLoad::ServerSave(const std::filesystem::path&)`** (`.h:23`, `.cpp:50`) → return `bool` (forward `WriteGrid`'s result). **`ServerSave()`** no-arg (`.h:21`, `.cpp:45`) → return `bool` (forward the path overload).
3. **`CommandSave`** (`AgentCommandsServer.cpp:86`) → on `false`, `throw std::runtime_error("save failed to write '<file>'")` (matches the command layer's throw-on-failure convention — the dispatcher turns a thrown `std::exception` into an error envelope; confirm at edit via the surrounding command handlers). Only set `rResult["file"]` after a confirmed-successful write.
4. **`kClientSaveRequest` path** (`ServerSession.cpp:201-204`): the no-arg `ServerSave()` now returns `bool`; there is no client ack channel, so at minimum log a `kWarning` on `false` (upgrade from the current unconditional `kDebug` line). Do not invent a new wire ack — see Out of scope.
5. **Rider (optional, same touch)**: in `BareFilenameParam`, after the separator checks, reject the Windows reserved device basenames (case-insensitive, matching the stem before any extension). Throw the same `std::runtime_error` shape as the existing guards.

Interfaces:
- `game::GameSaveLoad::WriteGrid(const engine::FileFlags_t&, const std::filesystem::path&, engine::GridCoord)` → `bool`.
- `game::GameSaveLoad::ServerSave()` and `ServerSave(const std::filesystem::path&)` → `bool`.
- `game::CommandSave(const nlohmann::json&, nlohmann::json&)` (AgentCommandsServer.cpp) — throw on failure.
- `game::BareFilenameParam(const nlohmann::json&)` — optional reserved-name rejection.
- `engine::FileManager::WriteFileAtomically` — the underlying `bWritten` source (unchanged).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.{h,cpp}` — `WriteGrid`, both `ServerSave` overloads, their void-context callers.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp` — `CommandSave`, `BareFilenameParam`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `kClientSaveRequest` handler (log upgrade only).

## Out of scope

- No new wire message / ack for `kClientSaveRequest` (fire-and-forget stays; only server-side logging upgrades). Adding a client-facing save-result ack is a separate wire change, not this plan.
- `ReadGrid` / `ServerLoad` failure reporting (load already returns `bool` and is handled at `CommandLoad`).
- No change to `WriteFileAtomically`'s atomic-rename mechanics or `FileFlags`.
- The rider is a filename-validation hardening only — no broader path-sanitization overhaul (`BareFilenameParam`'s existing separator/`..`/`:` rejection stays as the sandbox boundary).

## Acceptance criteria

- A `save` agent command whose write fails returns an error envelope (thrown), not a `file` success envelope.
- `WriteGrid` / `ServerSave` propagate the atomic-write result; existing void-context save callers compile unchanged (return discarded).
- (If rider taken) a `file` param of `NUL` / `CON` / `COM1` etc. is rejected by `BareFilenameParam` before any write attempt.

## Notes

- **Invariant exposure**: server-only (`BT_SERVER` — `GameSaveLoad` and `AgentCommandsServer.cpp` are both server-side). No wire-format change, no `game::Frame::kiVersion` / `.pack` / save-format change (only the C++ return-type plumbing and one validation guard), no CRC/determinism path.
- No open design decision requiring grill — mechanical result-threading plus an optional, self-contained validation rider.
