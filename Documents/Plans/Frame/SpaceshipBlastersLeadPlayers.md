# Spaceship Blasters Lead Player Targets

Source: surfaced during the session that added `common::ComputeLeadPosition` and applied it to player blasters in `PlayersCombat.cpp`. Flagged as a future game-design consideration rather than a forced follow-up.

## Context

Player blasters now solve for projectile lead against their target spaceship (closed-form intercept via `common::ComputeLeadPosition` in `Common/Source/MathUtils`). Enemy spaceship blasters do not — `Spaceships.cpp` enemy firing (the angle-to-player gated block around lines ~531-557) currently fires straight along the spaceship's facing with no lead solve against the targeted player.

The math infrastructure (`ComputeLeadPosition`, projectile speed, target velocity) is already in place and correct for this use. Wiring it into the spaceship firing path is a single call-site change once it has been authorized.

The reason this is **not** an automatic follow-up is fairness/difficulty. Today, enemy spaceships miss moving players in proportion to player speed — that miss rate is part of the difficulty curve. Switching to lead-targeted enemy fire would tighten the curve sharply and is a player-felt design choice, not a correctness fix.

## Design

Gated on a game-design decision. Two reasonable options once authorized:

1. **Full lead** — enemy blasters use `ComputeLeadPosition` against the targeted player's velocity. Maximum difficulty. Single call-site change.
2. **Partial lead with jitter** — solve for lead position, then perturb the firing direction by a small random angle (RNG-from-frame, deterministic) so accuracy improves but doesn't become surgical. Two extra lines plus an RNG seed sourced from the frame counter and per-spaceship index.

Option 2 is the more likely shape because (a) it preserves the design intent that enemy fire is partly miss-able and (b) it gives a single dial (the jitter angle) the designer can tune.

Either option lives in the same code site — the angle-to-player gate inside the spaceship firing loop. The gate's existing semantics (only fire when roughly facing the player) stay; only the firing direction changes from "along facing" to "lead vector (optionally jittered)".

## Out of scope

- Changing the angle-to-player gate, the fire-rate cooldown, projectile speed, or per-spaceship-class behavior.
- Per-player difficulty scaling (easy/medium/hard) — if added, that is a separate plan.
- Predictive aim against player **acceleration** (currently `ComputeLeadPosition` assumes constant target velocity; this is fine).
- Touching player blasters — they already lead.

## Acceptance criteria

- Game-design decision recorded (option 1 vs option 2 vs do-nothing) before any code change.
- If option 1: enemy spaceship blasters call `common::ComputeLeadPosition` and shoot toward the lead point.
- If option 2: same, plus deterministic per-spaceship-per-frame jitter applied to the firing direction with a documented angle range.
- Determinism preserved (no `rand()`, no wall-clock seeding) — RNG source is frame counter + spaceship index, matching existing patterns in the spaceship update path.
- Playtest at the chosen difficulty target before merging — the felt difference is the whole point of this change, so it must be subjectively validated, not just code-reviewed.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — enemy blaster firing block inside the angle-to-player gate (currently around lines ~531-557; anchor on the gate predicate, not line numbers).
- `Common/Source/MathUtils.h` (or wherever `ComputeLeadPosition` lives — confirm at execution time) — call site dependency, no edits expected.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` — reference implementation for the existing player-side lead solve; mirror its argument shape.

## Notes

- Defer until a designer asks for it. There is no correctness motivation to land this; it is purely a difficulty/feel change.
- If accepted, the diff is small enough (under ~10 lines) that `/external-grill-plan` is overkill — invoke `code-review` and `code-style-review` only.
- Deterministic RNG sourcing matters because spaceships run on both client (predicted) and server (authoritative). A non-deterministic jitter would desync. Use the existing per-frame deterministic-RNG helper, not a per-call `std::random_device`.
