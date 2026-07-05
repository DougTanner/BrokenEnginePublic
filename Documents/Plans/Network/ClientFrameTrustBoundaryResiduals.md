# Client / Frame Trust-Boundary Residuals

## Context

Two trust-boundary hardening residuals surfaced by a codebase sweep + adversarial extension-review gate during the server-side trust-boundary fix (the `ServerTrustBoundaryResiduals` work, which closed the server game-packet dispatch backstop and the `ReadFleet` `fNavigationDelay`/`fFrameChangeTimer` file-read finite-checks). Both are SIBLINGS of those server fixes but were deferred out of that plan because they breach its declared server-only (`BT_SERVER`) envelope — they live in `BT_CLIENT` / shared-serialization code and each carries an open design decision. They are collected here.

Current server-side state (present tense, no live dependency): the server game-packet dispatch in `ServerSession::ParseReceivedGamePackets` is already wrapped in a log-and-drop `try/catch`, and `ReadFleet` already finite-checks `fNavigationDelay`/`fFrameChangeTimer` at the save/replay file-read boundary. This plan mirrors the FIRST of those (the dispatch backstop) onto the client, and extends the same read-side finite-check discipline to `FrameInterpolate::fSpawnTimer`.

Neither residual has a confirmed live crash path today — the client packet parsers are already bounds-safe (`ParsePlayerEvents` per-branch size checks, `ParseFleetSync` `BoundedCursor`), and a non-finite `fSpawnTimer` only arises from a corrupt save or a hostile full-state. Both are defense-in-depth parity closing a demonstrable hazard class.

## Design

### Residual A1 — client game-packet dispatch backstop

`ClientSession::PollNetwork` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:59`) dispatches parsed game packets on the `ClientUpdate` call stack — which is NOT under the engine `Client::Receive` try/catch. That engine backstop wraps only engine control-packet dispatch and game-packet BUFFERING (the drain-into-queue step), not the later game-side parse/apply that `PollNetwork` performs. An uncaught throw from any handler here would tear down `ClientUpdate` and crash the client.

The untrusted parse/apply region is three sub-sections:
1. `ParsePlayerEvents` + the `ApplyPlayerEvent` loop (`:74-81`).
2. The `kServerTimespeedUpdate` decode loop (`:83-100`).
3. `ParseFleetSync` + `SyncFleets` (`:102-112`).

Immediately after (`:114-117`) come `ApplyReceivedStaticData` / `ApplyReceivedFullStates` / `UpdateSubscriptions` / `ApplyReceivedUpdates` — the data-receiver / reconciliation-adjacent tail. These MUST stay OUTSIDE any backstop: a throw there (e.g. a corrupt-index `.at()` on the full-state apply path) is a desync/CRC signal that should surface to the desync manager, not be silently swallowed.

Fix: wrap only the three untrusted parse/apply sub-sections in a log-and-drop `try { … } catch (const std::exception& rException)`, mirroring the server `ParseReceivedGamePackets` backstop form — LOG at `kNetwork`/`kWarning` with packet context, drop, never re-throw. The whole function already runs under `ScopedSuppressAllocationTracking suppress;` (`:62`) and `rException.what()` is a `const char*`, so keep `{}` LOG placeholders (no allocating spec).

OPEN DECISION (A1 wrap scope) — staged for grill:
- Option A (recommended, KISS): one `try/catch` spanning all three sub-sections (`:74-112`), excluding the `:114-117` tail. Simplest; a throw in an early section skips the remaining sections for that one poll (benign — the next poll re-drains).
- Option B: three independent `try/catch` blocks (one per sub-section) so a throw in one section can't skip the others. Finer isolation, closer to the server's per-packet granularity, at the cost of three catch sites.

No confirmed throw path exists today, so isolation granularity is a judgment call, not a correctness requirement.

### Residual B2 — `fSpawnTimer` file-read finite-check

`FrameInterpolate::fSpawnTimer` is read with no finite-check in two places:
- `FrameInterpolate::Read` (`Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:588`, save/replay full-state path via `operator>>`).
- `FrameInterpolate::ServerRead` (`Frame.cpp:603`, the client full-state wire-receive path, reached via `ClientReceive.cpp:42`).

It drives the spawn drain loop in `FramePostRender::Spawn` (`Frame.cpp:379`): `while (rInterpolate.fSpawnTimer >= kfSpawnInterval) { rInterpolate.fSpawnTimer -= kfSpawnInterval; SpawnSpaceshipGroup(...); }`. A non-finite value from a corrupt save OR a hostile client full-state is dangerous: `NaN` makes the `>=` compare false forever (spawning silently freezes), and `+Inf` never drains below the interval → **infinite loop / hard hang** on whichever side reads it (server for a corrupt save/replay; client for a hostile full-state). This is strictly worse than the fleet-timer freeze the server plan closed.

Fix: after each `common::Read(rStream, fSpawnTimer)` (`:588` and `:603`), neutralize non-finite values, mirroring the server plan's read-side corrections.

OPEN DECISION (B2 finite-check form) — staged for grill:
- Option A (recommended): finite-check → `0.0f` (`fSpawnTimer = std::isfinite(fSpawnTimer) ? fSpawnTimer : 0.0f;`). `0.0f` matches the field's reset-to-zero drain semantics (the loop subtracts down toward zero) and is safe against the `>= kfSpawnInterval` gate: `0.0f < 0.5f`, so the loop simply does not fire and normal accumulation resumes next tick. Neutralizes both `NaN` and `+Inf`.
- Option B: finite-check + upper clamp (e.g. `[0, kfSpawnInterval]`). Rejected as the default: bounding benign accumulation is a SEPARATE concern already owned by `Frame/FrameTickMinorHardening.md` item 2 (main-menu unbounded `fSpawnTimer` growth) — B2 should stay a pure trust-boundary finite-check and not double-own the accumulation bound.

A read-side finite-check is a no-op on any validly-produced stream — a bit-deterministic sim never emits a non-finite `fSpawnTimer` — so, exactly like the server plan's clamps, it needs NO `Frame::kiVersion` bump and introduces NO CRC / stream-format change.

Relationship to `FrameTickMinorHardening.md` item 2 — complementary, not duplicate. That item bounds the ACCUMULATE side (`Frame.cpp:102` unconditional add; drain skipped under `kMainMenu` at `:372`) to prevent a benign menu→game burst; B2 hardens the READ side against non-finite garbage. The two edits touch the same field in different locations for different hazards — co-schedule to keep the `Frame.cpp` `fSpawnTimer` cites in sync.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — `ClientSession::PollNetwork` (A1 backstop; wrap `:74-112`, exclude `:114-117`).
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `FrameInterpolate::Read` (`:588`) and `FrameInterpolate::ServerRead` (`:603`) (B2 finite-checks); the drain site `FramePostRender::Spawn` (`:379`) is a read-only reference.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReceive.cpp:42` — the `ServerRead` full-state wire-receive caller (context for B2's `BT_CLIENT` exposure; not edited).

## Out of scope

- The server-side dispatch backstop and `ReadFleet` `fNavigationDelay`/`fFrameChangeTimer` finite-checks — already present in the server net path.
- Engine `Client::Receive` / `Server::Receive` backstops — already wrapped.
- Bounding benign `fSpawnTimer` accumulation (main-menu unbounded growth) — owned by `Frame/FrameTickMinorHardening.md` item 2 (co-scheduled, complementary).
- Any other `FrameInterpolate` / collection deserialized-field finite/range validation — the broader shared-serialization index/finite hardening is `Frame/CollectionReadIndexHardening.md`; B2 is scoped to the single `fSpawnTimer` hang hazard.
- Any `Frame::kiVersion` bump, stream-format change, or new serialized field.

## Invariant exposure

- **A1**: `BT_CLIENT` guard scope only. Runs on the `ClientUpdate` request-handling stack, NOT the CRC'd sim tick. A log-and-drop try/catch around packet parse/apply — no wire format, no `kuiProtocolVersion`, no `Frame::kiVersion`, no CRC/determinism exposure. The reconciler/CRC tail is deliberately excluded so its exceptions still surface.
- **B2**: touches SHARED client/server serialization — `FrameInterpolate::Read` (save/replay, `Frame::kiVersion`-gated stream) and `FrameInterpolate::ServerRead` (the `BT_CLIENT` full-state wire-receive path). `fSpawnTimer` sits in the replicated shared subset (`ServerRead` consumes it), so this is determinism-adjacent. BUT a read-side finite-check is a no-op on any validly-produced stream — it only rewrites non-finite garbage a bit-deterministic sim can never emit — so a matched client/server pair produces identical results and there is NO format / CRC / `Frame::kiVersion` change.

## Notes

**Decision plan (present options).** Two open decisions to resolve at `/external-grill-plan`:
1. **A1 wrap scope** — one `try/catch` spanning the three untrusted parse/apply sub-sections (recommended, KISS) vs three independent per-section wraps (finer isolation, server-parity granularity). Either way, EXCLUDE the `:114-117` data-receiver/reconciler tail.
2. **B2 finite-check form** — finite-check → `0.0f` (recommended; matches the drain's reset-to-zero semantics, safe against the `>= kfSpawnInterval` gate) vs finite-check + upper range clamp (rejected default — the accumulation bound is owned by `FrameTickMinorHardening.md` item 2).

Both residuals are defense-in-depth parity with no confirmed live throw/non-finite path today; the value is closing a demonstrable client-crash (A1) and server/client-hang (B2) hazard class at the two trust boundaries the server plan could not reach.
