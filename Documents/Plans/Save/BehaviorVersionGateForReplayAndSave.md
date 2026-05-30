# Version Gating Misses Behavior-Only (Non-Layout) Sim Changes

## Context

A code-review audit of the just-shipped island-tour behavior fix (the flagship's island tour now advances
immediately on arrival rather than waiting) surfaced an out-of-scope architectural gap in how the engine
gates versions. The fix changed the deterministic-sim RNG stream / AI logic but touched **no serialized
layout**, so every existing version gate stayed numerically identical — yet old replays and saves now
re-simulate to a different result. This plan captures the gap; it does **not** prescribe the fix (see the
options below).

### What `kiGameVersion` actually gates today: nothing load-bearing

`game::kiGameVersion` (`Projects/BrokenEngineSandbox/Source/Version.h`, currently `13`) is **informational
only**. Its three consumers are all display/telemetry:

- Vulkan `applicationVersion` — `engine::InstanceManager::Create` populating `VkApplicationInfo.applicationVersion`
  (`Engine/Source/Graphics/Managers/InstanceManager.cpp:129`).
- Crash report line — `Engine/Source/CrashReport.cpp:47` (`"Game version: " << game::kiGameVersion`).
- Startup log line — `MainThread` in `Engine/Source/Main.cpp:44` (`LOG(kDefault, kInfo, "Game version: {}", game::kiGameVersion)`).

None of these gate a save load, a replay load, or the network handshake. The game `Source/CLAUDE.md`
documents the convention as *"`Version.h` holds a single `kiGameVersion` constant ... bump on
save/replay/protocol-incompatible changes"* — but bumping it has **no effect** on any of those paths.

### What the real gates check: layout, not behavior

The gates that actually reject incompatible data are all keyed on `game::Frame::kiVersion` (or a sibling
struct `kiVersion`):

- **Save-grid load** — `game::GameSaveLoad::ReadGrid` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:410`)
  rejects when `iVersion != game::Frame::kiVersion`.
- **Client/server handshake** — `Server::ClientHello` (`Engine/Source/Network/Server/ServerReceive.cpp:247`)
  rejects when `iClientFrameVersion != game::Frame::kiVersion`.
- **Replay load** — the per-coord `engine::DifferenceStreamReader<game::Frame, game::FrameInput>` header
  check (`Engine/Source/File/DifferenceStream.h:172` / `:179`) rejects when the stored
  `SAVED_TYPE::kiVersion` (= `game::Frame::kiVersion`) or `DIFFERENCE_TYPE::kiVersion`
  (= `game::FrameInput::kiVersion`) mismatches. The replay-meta sidecar
  (`game::ReplayMeta::kiVersion`, `Projects/BrokenEngineSandbox/Source/Game.h:49`) is read by
  `GameSaveLoad::LoadReplayIfRequested` (`GameSaveLoad.cpp:184`) through `engine::ReadVersionedFile`.

`game::Frame::kiVersion` is a **sum of layout sub-versions**:
`const int64_t Frame::kiVersion = 113 + engine::kiNavDataVersion + BlastersInterpolate::kiVersion + ... + TargetsPostRender::kiVersion;`
(`Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:11`). It only changes when a *serialized layout*
changes (a collection adds/reorders a member and bumps its `kiVersion`, or `kiNavDataVersion` moves). A
behavior-only edit to the same collection's update logic leaves every term untouched.

### Consequence

A deterministic-sim change that alters the RNG stream / AI logic but **not** any serialized layout (the
island-tour fix is exactly this class) is **invisible to all three gates**:

- The handshake passes (same `Frame::kiVersion`), so a stale client and updated server connect, then
  diverge on the first divergent tick.
- An old save loads cleanly (same `Frame::kiVersion`), then plays forward with the new behavior — for a
  save this is arguably *fine* (you just keep playing).
- An old replay loads cleanly, then **CRC-desyncs at the first divergent tick**:
  `DifferenceStreamReader::ValidateChecksum` via `GameSaveLoad::SyncReplayTick`
  (`GameSaveLoad.cpp:349`). Instead of a clean "incompatible version, can't replay" rejection at load,
  the user gets a noisy mid-playback checksum failure. The replay feature is debug-only (gated by
  `kbDebugInput`, the `F7.replay.*` files), so this is low blast-radius, but it is a confusing failure
  mode.

### Why it matters

The documented convention ("bump `kiGameVersion` for replay-incompatible changes") gives a **false sense
of protection**: nothing reads `kiGameVersion` on any load path, so bumping it achieves nothing. Either the
convention is wrong, or a gate needs to start consuming a behavior-version constant. Today the only thing
that "catches" a behavior-only change is a downstream CRC desync — late, noisy, and (for replay) at an
arbitrary tick rather than at load.

## Design

This is an **architectural decision** (it changes the version-gating model), so this plan presents the
problem and options rather than prescribing one. Resolve the option choice with the user before
implementing.

The crux: layout-version (`Frame::kiVersion`) and behavior-version are **orthogonal axes**. Today only the
layout axis is gated. The question is whether — and where — to add the behavior axis.

### Option A — Fold a behavior-version into the replay (and optionally save) gate

Add a behavior-version term to the load gates so a behavior-incompatible (non-layout) change rejects
cleanly. Two sub-shapes:

- **A1 — reuse `kiGameVersion`** as the behavior-version. Author bumps `kiGameVersion` on any
  sim-behavior change (the existing documented convention), and the replay-meta read (and/or the
  `DifferenceStream` header, and/or `ReadGrid`) starts comparing it.
- **A2 — introduce a dedicated `kiSimBehaviorVersion`** constant (keep `kiGameVersion` purely
  informational for crash/telemetry). Cleaner separation: `kiGameVersion` is the human-facing build
  number; `kiSimBehaviorVersion` is the determinism contract. The replay-meta / handshake gate compares
  `kiSimBehaviorVersion`.

The simplest concrete landing for either is to **add the behavior-version to `game::ReplayMeta`** (already
versioned via `ReadVersionedFile`, already read at replay-load before any tick replays) and reject at
`GameSaveLoad::LoadReplayIfRequested` when it mismatches — this rejects stale replays *at load* with a
clear message, no `DifferenceStream` template change required.

Pros:
- Stale replays reject cleanly at load with an explicit "incompatible sim version" message instead of a
  mid-playback CRC desync.
- Makes the documented `kiGameVersion` convention actually do something (A1), or replaces it with an
  honest one (A2).
- If also folded into the handshake (`ServerReceive.cpp:247`), a stale client is rejected at connect
  instead of desyncing — but see the con.

Cons:
- **Author discipline burden**: the behavior-version is *manual* (unlike `Frame::kiVersion`'s
  auto-summation). A behavior change that forgets to bump it is back to silent desync — same failure mode
  as today, just relocated. There is no compile-time forcing function (the layout locks in
  `DataFile.h`/collection `kiVersion`s have no behavior analogue).
- Folding into the **handshake** is heavier-risk: it touches the live network protocol and would reject
  cross-version play that today "works" until divergence. Probably out of scope for a debug-only-replay
  motivation (keep handshake on layout only unless the user wants cross-version-play hardening too).
- A1 overloads `kiGameVersion` with a determinism contract on top of its telemetry role; A2 adds a second
  constant to keep straight.

### Option B — Harden the REPLAY gate only; leave saves and handshake alone

Same as A's replay-meta mechanism, but **scope it strictly to replay**. Rationale: replay is the only path
that *re-simulates recorded inputs and demands bit-exact reproduction*; a save just resumes play (new
behavior is acceptable), and the handshake's desync is already covered by the runtime CRC reconcile path.

Pros:
- Smallest footprint: one constant + one comparison in `GameSaveLoad::LoadReplayIfRequested` (the
  `ReplayMeta` read site). No save-format change, no protocol change, no `DifferenceStream` template
  change.
- Matches the actual problem severity: replay is debug-only and is the only path that produces the
  *confusing* failure (mid-playback desync vs. clean load rejection).
- Leaves the (defensible) "old save + new behavior = keep playing" semantics intact.

Cons:
- Still manual-bump (same discipline con as A).
- Does nothing for the stale-client handshake case (accepted as out of scope — runtime CRC catches it).

### Option C — Do nothing; accept CRC-desync as the detection mechanism

Document that behavior-only changes are *intentionally* not version-gated and that the runtime/replay CRC
is the detection mechanism. Fix only the **documentation** (the game `Source/CLAUDE.md` line and any
comment near `kiGameVersion`) so the convention stops claiming protection it doesn't provide.

Pros:
- Zero code risk; no manual-bump discipline to maintain.
- Honest: the CRC genuinely does detect every divergence, just late.

Cons:
- Replay keeps its confusing mid-playback failure instead of a clean load-time rejection.
- Leaves a documented convention that misleads; at minimum the docs must change even under C.

### Recommendation

**Option B** (harden the replay gate only, via a behavior-version term in `ReplayMeta`), with a
**dedicated `kiSimBehaviorVersion`** constant (the A2 separation) rather than overloading `kiGameVersion`.
Reasoning:

- It targets exactly the path with the bad failure mode (debug-only replay), at the lowest cost (one
  constant + one comparison at the existing `ReplayMeta` read site), without touching the live protocol or
  the save semantics that are fine as-is.
- A dedicated constant keeps the telemetry `kiGameVersion` honest and makes the determinism contract
  explicit at its definition.
- Whichever option lands, the **documentation must be corrected** (Option C's doc fix is a prerequisite of
  A and B too): the game `Source/CLAUDE.md` "bump `kiGameVersion` on save/replay/protocol-incompatible
  changes" line currently describes behavior that does not exist.

Confirm with the user before implementing — the manual-bump discipline burden and whether the handshake
should also be hardened are the two judgment calls.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Version.h` — `game::kiGameVersion` (currently `13`); the
  informational-only constant. Home of a new `kiSimBehaviorVersion` under A2/recommendation.
- `Projects/BrokenEngineSandbox/Source/Game.h:47-53` — `game::ReplayMeta` (`kiVersion = 2`); the
  replay sidecar, the natural carrier for a behavior-version under B/A.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `LoadReplayIfRequested` reads `ReplayMeta`
  at `:184` (the clean replay-load rejection site); `ReadGrid` gates saves on `Frame::kiVersion` at
  `:410`; `SyncReplayTick`'s `ValidateChecksum` at `:349` is the current (late) replay desync detector;
  the writer side stamps `ReplayMeta` around `:311`.
- `Engine/Source/File/DifferenceStream.h:172`/`:179` — the per-coord replay header gate on
  `SAVED_TYPE::kiVersion` / `DIFFERENCE_TYPE::kiVersion` (= `Frame`/`FrameInput`). An alternative carry
  site if the behavior-version must travel with the stream rather than the meta sidecar (heavier — template
  change).
- `Engine/Source/Network/Server/ServerReceive.cpp:247` — `Server::ClientHello` handshake gate on
  `game::Frame::kiVersion`. Touch only if the user opts to harden cross-version play (the heavier scope).
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:11` — `Frame::kiVersion` layout-sum definition;
  read-only reference (shows why behavior changes don't move it).
- `Engine/Source/Graphics/Managers/InstanceManager.cpp:129`, `Engine/Source/CrashReport.cpp:47`,
  `Engine/Source/Main.cpp:44` — the three informational `kiGameVersion` consumers; read-only reference
  (confirms it gates nothing).
- `Projects/BrokenEngineSandbox/Source/CLAUDE.md` — the "Save-format version" bullet claiming
  `kiGameVersion` is bumped for "save/replay/protocol-incompatible changes". Must be corrected under
  **every** option (it is currently false).

## Out of scope

- **The island-tour behavior fix itself** — already shipped; this plan only captures the gating gap it
  exposed.
- **Hardening the network handshake for cross-version play** — possible under Option A but heavier (live
  protocol, rejects play that "works today"). Pull in only on explicit user request; the default
  recommendation leaves the handshake on layout-version only.
- **Changing the save semantics** ("old save + new behavior = keep playing" is acceptable). Saves are not
  the confusing-failure path; only revisit if the user wants saves gated too.
- **The auto-summed layout-version machinery** (`Frame::kiVersion`, collection `kiVersion`s,
  `kiNavDataVersion`, the `DataFile.h` layout locks). Those work as designed for layout changes; this gap
  is the orthogonal *behavior* axis, not a defect in the layout axis.
- **Any compile-time "did you bump the behavior version?" forcing function.** There is no obvious
  mechanism (behavior changes leave no layout signature), and inventing one is its own design effort —
  out of scope here.
- **`game::FrameInput::kiVersion` / other struct `kiVersion`s** beyond noting their role as the existing
  layout gates.

## Acceptance criteria

(Only meaningful once an option is chosen with the user. For the recommended Option B:)

- A replay recorded under the old sim behavior is **rejected at load** with a clear "incompatible sim
  version" message at the `ReplayMeta` read in `GameSaveLoad::LoadReplayIfRequested`, instead of
  CRC-desyncing mid-playback in `SyncReplayTick`.
- A replay recorded under the matching sim behavior still loads and replays clean.
- `game::Frame::kiVersion`, the save-grid gate, and the network handshake are unchanged (Option B leaves
  saves and protocol alone).
- The game `Source/CLAUDE.md` "Save-format version" wording is corrected to describe what actually gates
  what (true under any chosen option).
- Client and server build clean.

## Notes

- All symbols and line numbers verified against current source: `kiGameVersion` (`Version.h:3`) and its
  three informational consumers; `Frame::kiVersion` sum (`Frame.cpp:11`); the save-grid gate
  (`GameSaveLoad.cpp:410`); the handshake gate (`ServerReceive.cpp:247`); the `DifferenceStream` header
  gates (`DifferenceStream.h:172`/`:179`); the `ReplayMeta` struct (`Game.h:49`) and its read at
  `GameSaveLoad.cpp:184`; the replay CRC validation at `GameSaveLoad.cpp:349`.
- **Low priority** — the only *confusing* failure (mid-playback CRC desync vs. clean load rejection) is on
  the debug-only replay feature (`kbDebugInput`, `F7.replay.*`). The save and handshake cases either
  behave acceptably (save) or are already covered by the runtime CRC reconcile (handshake).
- The recurring theme: layout-version is auto-summed and self-enforcing; behavior-version is inherently
  manual. Any option that adds a behavior gate inherits the "author must remember to bump" discipline
  burden — that trade-off is the core decision for the user.
