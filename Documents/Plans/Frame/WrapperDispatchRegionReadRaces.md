# Frame: Wrapper Reads Inside Dispatch Worker Regions (Unsynchronized-Read Audit)

## Context

`engine::Wrapper` (`Engine/Source/Ui/WrapperBase.h`) is a `float`-backed UI setting container with **no atomics**. Its
thread contract (now documented on the class as of `Ui/Architecture_WrapperContractDocs` landing): single writer on the
main thread during `ImGuiManager::Prepare`, readers sequenced later in the same frame, also main-thread. A wrapper read
inside a `common::gpMultithreading->Dispatch()` region must have **no runtime writer** — otherwise a main-thread Tweaks/menu
slider write races the worker-thread read of the unsynchronized `mfCurrent`.

The contract-doc plan cited only `gBaseHeight` (which has no runtime writer, so its NavQuery Dispatch-region reads are
benign). A Step-6 sweep during that plan surfaced **~15 Tweaks/menu-bound wrappers** — wrappers that DO have main-thread
runtime writers — read from worker-thread frame phases (`Spawn`/`PostCollision`/worker-side `Interpolate::Update`, fanned
out by `RunFrameTick` via `Dispatch()`). Each is a real unsynchronized-read candidate, not just doc material.

This plan is **investigate-then-decide** (per Diagnosis Discipline: verify the cited root cause before editing). The likely
outcome is "accept + document" for most sites — the raced wrappers carry cosmetic audio/visual values (volumes, pitch,
wind-deposit widths, trail intensities/durations, explosion-lighting), none of which feed the deterministic shared-CRC sim
state, and the Tweaks UI surface is debug-gated. But a torn `float` read of a wrapper mid-write is formal UB and can yield a
garbage intensity/volume for one frame, so the audit must classify each site and pick a resolution rather than assume.

## Design

### Phase 1 — Confirm the race surface (no edits)
For each candidate below, verify against current source:
- The read executes on a worker thread (trace the enclosing method back to a `RunFrameTick` Dispatch fan-out phase, not the
  main-thread `Render`/`Interpolate::Render` path). `Interpolate::Update` runs on BOTH worker and main paths — the worker side
  is the raced one.
- The wrapper has a main-thread runtime writer (Tweaks slider or menu toggle/radio). Confirm via the Tweaks/menu screen call
  sites; a wrapper with no writer is the benign `gBaseHeight` class and drops out of scope here.
- Whether the value participates in CRC'd sim state (it should not — confirm). Any wrapper found feeding CRC'd state is a
  determinism escalation, not a cosmetic race, and routes to its own plan.

### Phase 2 — Pick a resolution per cluster (grill decision)
Candidate resolutions, cheapest first:
- **(a) Accept + document** — add a one-line note at each site (or one hub note in `Frame/CLAUDE.md` + the Collections hub)
  that these cosmetic wrappers are intentionally read lock-free from workers; a torn read costs at most one frame of a
  cosmetic value. Recommended default if Phase 1 confirms zero CRC exposure and the values are cosmetic.
- **(b) Snapshot once per tick on the main thread** — capture the needed wrapper values into a plain struct before the
  Dispatch fan-out and have workers read the snapshot. Removes the race entirely; costs a per-tick copy and plumbing of the
  snapshot into the worker phases. Justified only if (a) is judged unacceptable.
- **(c) Make the specific raced wrappers atomic** — rejected unless Phase 1 finds a value that matters; changing `Wrapper`'s
  storage to atomic for all instances is out of proportion to a cosmetic hazard.

Choose one (likely (a)) at `/external-grill-plan`.

## Candidate sites (verify line numbers at execution — collections churn)

Real race candidates (Tweaks/menu writer + worker-thread read), all pre-existing:
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp:193,227,240` — shield/armor-hit volumes (`ApplyDamage` ← PostCollision).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp:366-372,448` — wind-deposit + blaster/missile audio (`SpawnBlasters`/`SpawnMissiles` ← Spawn).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp:396-398,568-570,580-581,616` — wind-deposit, hex-shield wrappers (Spawn + worker-side `Interpolate::Update`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp:282,376-378,568-570,593-595` — explosion-lighting, wind-deposit (Spawn + worker `Interpolate::Update`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsCombat.cpp:58,159,574` — death/hit/enemy-blaster audio (PostCollision/Spawn).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp:122,134,497` — trail intensity, loop/explosion volume (`SyncMissile`, `Explode`).
- `Engine/Source/Frame/Collections/Explosions/ExplosionsUpdate.cpp:99` and `ExplosionsSpawn.cpp:127,155` — trail-duration, wind-deposit.
- `Engine/Source/Frame/Collections/WindRadials/WindRadialsUpdate.cpp:13` — `gWindEnabled` (menu-bound) in worker `Interpolate::Update`.

Benign siblings (writer-less `gBaseHeight`, same class as the already-documented NavQuery reads — confirm no writer, then
exclude or fold into the (a) doc note): `Projects/.../Frame/Frame.cpp:216,249,339`, `Players/PlayersNavigation.cpp:322,439`,
`Spaceships/SpaceshipsNavigation.cpp:59,63`, `Players/Players.cpp:355,505,711`.

## Critical files
- The candidate `.cpp` sites above (reads only; resolution (a) adds comments, (b) adds a snapshot struct + plumbing).
- `Engine/Source/Frame/CLAUDE.md` and/or `Engine/Source/Frame/Collections/CLAUDE.md` (hub note if (a) chosen).

## Out of scope
- The `Wrapper` class storage itself — no atomics added (that's resolution (c), pre-rejected unless Phase 1 escalates).
- `gBaseHeight` determinism conversion — separate plan `Frame/Architecture_BaseHeightWrapperDeterminism.md`.
- Main-thread `Interpolate::Render` / `Register()` wrapper reads — not worker-reachable, not raced.
- Any wrapper found to feed CRC'd sim state — escalates to its own determinism plan, not handled here.
- The two `Set(bool)` / `ClientSettings.cpp:125` mention-clauses — already folded into the `WrapperBase.h` contract comments.

## Acceptance criteria
- Every candidate site classified: worker-vs-main confirmed, writer-presence confirmed, CRC-exposure confirmed absent.
- A single resolution chosen and applied uniformly (no per-site divergence without justification).
- If (a): the lock-free-cosmetic-read convention is documented once at a hub, not scattered.

## Notes
- No determinism/CRC/`kiVersion`/replay/network/`.pack` exposure expected — the whole point of Phase 1 is to confirm that.
  If confirmation fails for any site, stop and escalate that site.
- One grill decision pre-staged: resolution (a) accept+document vs (b) per-tick snapshot.
- Decision plan flavor: present the (a)/(b) options writeup at grill before editing.
