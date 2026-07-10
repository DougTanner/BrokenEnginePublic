# Bugfix: RenderFrame asserts on empty snapshot ring during connect→disconnect transition

## Context

Reproduced twice (crash report + agent-harness log). Debug client sitting at the main menu, the local server process is gone. User clicks **LOCAL SERVER** → client attempts a reconnect → ~15 s later ENet delivers `ENET_EVENT_TYPE_DISCONNECT` → client crashes instead of surfacing the connection-rejection modal:

```
Assert failed: "rFrames.iSnapshotCount > 0"
engine::GameBase::RenderFrame   Engine/Source/GameBase.cpp:289
engine::GameBase::Render        Engine/Source/GameBase.cpp:343   (MainThread render path, Main.cpp:389)
```

### Root cause (verified by reading `GameBase.cpp` / `GameBase.h` / `ClientSessionBase.cpp`)

`engine::GameBase::RenderFrame(GridCoord)` (`GameBase.cpp:280`) resolves `const CoordFrames& rFrames = mCoordFrames.at(coord)` (`:288`) and then asserts `ASSERT(rFrames.iSnapshotCount > 0)` (`:289`) — it has **no tolerance for an empty snapshot ring** and unconditionally dereferences `rFrames.snapshots[iPhysical]` afterward.

`RenderFrame` is called from two sites inside `engine::GameBase::Render()` (`GameBase.cpp:304`), both reached once the render block guard `if (!rActiveCoords.empty())` (`:323`) passes:

- **Camera anchor**: `RenderFrame(cameraCoord)` at `:343` (the crashing site).
- **Per-active-coord interpolate**: the `interpolateFrame` lambda (`:444-459`) calls `RenderFrame(rCoord)` at `:446` for `cameraCoord` and every coord in `rActiveCoords`.

The camera-coord fallback at `:311-319` only guards the *camera* coord: if `mClientGridCoord` is absent from `mCoordFrames` **or** has `iSnapshotCount == 0`, and `rActiveCoords` is non-empty, it falls back to `rActiveCoords.front()` — **but never verifies that the fallback coord (or any active coord) has a populated ring.** So a `cameraCoord` with an empty ring reaches `RenderFrame` at `:343`, and even a populated `cameraCoord` still hits `:446` for any *other* active coord whose ring is empty.

The empty-ring state arises legitimately in the reconnect transition. On the client `game::gpGame->mActiveCoords` always contains `mClientGridCoord` (never-empty invariant, documented at `MainUniforms.cpp:443`; produced by game `ComputeActiveSet`). During a failed reconnect that coord is subscribed into `mCoordFrames` but never receives a full state (server gone), so its `iSnapshotCount` stays `0`; and on the terminal `ENET_EVENT_TYPE_DISCONNECT`, `ClientSessionBase::DisconnectFromServerBase()` (`ClientSessionBase.cpp:55`) calls `CoordFrames::ResetClientState()` (`ClientSessionBase.cpp:65`) on every coord, which zeroes `iSnapshotCount` (`GameBase.h:106`) while the game-layer active set still lists the coord for at least the frames until the menu state fully unwinds. Either way, `Render()` enters its block with `rActiveCoords` non-empty but the relevant ring(s) empty → assert.

The correct precedent for this exact condition already exists on the render path: `Islands` iterates the active coords with `if (it == rFrames.end() || it->second.iSnapshotCount == 0) { continue; }` (`Islands.cpp:152`). `RenderFrame`'s callers lack that guard.

Consequence: the connection-rejection modal is unreachable via this path — the client crashes before the menu/modal can render the failure.

## Design

Make the client render path treat "active coord(s) with an empty snapshot ring" the same way it already treats "no active coords" and the way `Islands.cpp` already treats a zero-count coord: **never call `RenderFrame` on a coord whose `iSnapshotCount == 0`.**

Concretely, in `engine::GameBase::Render()`:

1. **Camera-coord selection (`:311-319`)** — when falling back off an empty/absent `mClientGridCoord`, select the first coord in `rActiveCoords` **with `iSnapshotCount > 0`**, not merely `rActiveCoords.front()`. If none qualifies, there is no renderable coord.
2. **Render-block gate (`:323`)** — enter the interpolate/camera-update block only when a renderable coord exists (camera coord resolves to one with `iSnapshotCount > 0`). When `rActiveCoords` is non-empty but every ring is empty, skip the block exactly as the existing `rActiveCoords.empty()` path does — the swapchain-recreate/present tail below (`:492+`) still runs, so the menu and any connection-rejection modal render normally.
3. **Per-coord interpolate loop (`interpolateFrame`, `:444-467`)** — skip any coord with `iSnapshotCount == 0` before calling `RenderFrame(rCoord)`, mirroring `Islands.cpp:152`. `mRenderInterpolates` for skipped coords is already pruned by the `std::erase_if` at `:434-437` (they are still in `rActiveCoords`, so keep the interpolate only for rendered coords — see Notes on the erase predicate interaction).

This converts the assert into a defined skip. `RenderFrame` itself keeps its `ASSERT(iSnapshotCount > 0)` as a genuine internal invariant (callers must now guarantee it), consistent with the no-useless-ASSERT rule — the assert stops being reachable from valid transition state.

Secondary (surface the failure, not just suppress the crash): confirm the connection-rejection `ModalScreen` is actually driven by the existing disconnect handling once the render loop no longer crashes. If the disconnect path already requests the modal, no work is needed beyond the guard. If it does not, wiring the modal is called out as a follow-up (see Out of scope) rather than expanded here.

## Critical files

- `Engine/Source/GameBase.cpp`
  - `engine::GameBase::RenderFrame(GridCoord)` — `:280`; the `ASSERT(rFrames.iSnapshotCount > 0)` at `:289` is the crash site. No change to the function body required if callers are guarded; keep the assert as an internal precondition.
  - `engine::GameBase::Render()` — `:304`; camera-coord fallback `:311-319`, render-block gate `if (!rActiveCoords.empty())` `:323`, camera anchor `RenderFrame(cameraCoord)` `:343`, `mRenderInterpolates` prune `std::erase_if` `:434-437`, `interpolateFrame` lambda `:444-467`, `game::gpCamera->Update(mRenderInterpolates.at(cameraCoord))` `:478`.
- `Engine/Source/GameBase.h`
  - `CoordFrames::iSnapshotCount` (`:51`), `CoordFrames::ResetClientState()` (`:98`, zeroes it at `:106`) — read-only reference; no change.
- Reference-only (no change): `engine::Islands::…` empty-ring guard at `Engine/Source/Graphics/Islands.cpp:152` (the precedent to mirror); `ClientSessionBase::DisconnectFromServerBase()` at `Engine/Source/Network/Client/ClientSessionBase.cpp:55-66`; the never-empty active-set invariant note at `Engine/Source/Graphics/Render/MainUniforms.cpp:443`.

## Out of scope

- The game-layer active-set behavior (`game::Game::ComputeActiveSet`, `mClientGridCoord` never-empty invariant) — the fix stays entirely in the engine client render path; do not change what the active set contains.
- Any change to the server dual-buffer / `pCurrent`/`pNext` path or `FinalizeFrameTick` (server-only).
- Reconnect / subscription-lifecycle correctness itself (why the ring never populated). This plan only stops the render path from asserting on the resulting empty ring; subscription-lifecycle races are covered by the live `Network/SubscriptionLifecycleRaceHardening.md` / `Network/ClientFullStateEdgeFixes.md` plans.
- Building a new connection-rejection modal from scratch. If the existing disconnect handling already drives the `ModalScreen`, verify only; if it does not, file a follow-up rather than expand this plan.
- `RenderFrame`'s cold-start / render-behind fallback math (`:290-294`) — unchanged.

## Acceptance criteria

- Client at the main menu with no local server, clicking **LOCAL SERVER**, waiting through the ~15 s reconnect and the `ENET_EVENT_TYPE_DISCONNECT`, does **not** crash — the `rFrames.iSnapshotCount > 0` assert is no longer reachable.
- With `rActiveCoords` non-empty but all rings empty, `Render()` skips the interpolate/camera block and still presents (menu remains visible / responsive) — behaviourally identical to the `rActiveCoords.empty()` main-menu case.
- `RenderFrame` is never invoked on a coord with `iSnapshotCount == 0` from `Render()`.
- The connection-rejection modal (or a clean return to the main-menu state) is reachable after a failed reconnect — no longer pre-empted by the crash.

## Verification (agent-harness)

1. Launch server (`--agent-port 27100 --log-file …`) and client (`--agent-port 27101 --windowed 1280x720 --log-file …`); `ping` both.
2. Drive the client to connect (menu `click` **LOCAL SERVER**, or start already-connected), confirm active coords populate (`describe_scene` / `query_frame`).
3. `quit` the server process so it disappears.
4. Poll client `get_logs`; confirm the `ENET_EVENT_TYPE_DISCONNECT` arrives and the client stays alive (`ping` still responds; no `Assert failed: "rFrames.iSnapshotCount > 0"` in logs).
5. `screenshot` / `describe_ui` the client: confirm the menu (and the connection-rejection modal if wired) renders rather than a crashed/black window.
6. Negative control: a normal connected client with a live server continues to render islands/units unchanged (guard is inert in steady state).

## Notes

- **Invariant exposure**: client render path only. This is `Interpolate`/render territory, which is explicitly **outside** the deterministic PostRender CRC (root CLAUDE.md) — **no determinism/CRC exposure, no `kiVersion`/`.pack`/replay/wire change.** The edit lives inside the existing `#if defined(BT_CLIENT)` region of `GameBase.cpp` (`RenderFrame`/`Render` are already `BT_CLIENT`-only) — no client/server guard-scope change. No new allocations (the affected block already wraps its `std::erase_if` / `try_emplace` in `ScopedSuppressAllocationTracking`); skipping a coord only removes work.
- **`mRenderInterpolates` erase interaction**: the `std::erase_if` at `:434-437` currently keeps an interpolate for every coord in `rActiveCoords`. Skipped (empty-ring) coords will have no interpolate produced this frame; confirm nothing downstream (`RenderGlobal`, `game::gpCamera->Update` at `:478`) reads `mRenderInterpolates.at(rCoord)` for a coord that was skipped — `cameraCoord` is guaranteed renderable by construction, so `:478` is safe; a per-collection render consumer that iterates `rActiveCoords` and looks up `mRenderInterpolates` must tolerate a missing entry (the `Islands.cpp:152` precedent already `continue`s on empty rings, so this is the established contract). Verify during implementation.
- **Open decision for `/external-grill-plan`**: whether to also actively drive the connection-rejection `ModalScreen` from the disconnect path in this plan, or keep this plan crash-suppression-only and file the modal wiring separately. Recommendation: keep this plan to the render-path guard (the crash fix), verify modal reachability, and only expand if the modal is found un-wired.
- No architectural decision — mirrors an existing repo pattern (`Islands.cpp:152`); trivial-choice implementation details (exact predicate placement) picked simplest during implementation.
