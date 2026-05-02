# Unify `AcquireTarget` In-Range and Fallback Filters

Source: surfaced by code-review subagent during the session that added projectile-lead math to `PlayersCombat.cpp`. Intentionally deferred from that change to keep the lead-targeting diff scoped.

## Context

`PlayersCombat.cpp::AcquireTarget` performs two separate scans of the spaceship list to pick a target for a player:

1. **Primary scan** — find the closest enemy spaceship that is alive **and** past its arrival-grace window **and** visible (not hidden by fog / smoke / occlusion as defined by the existing visibility predicate). Used to pick something to shoot at.
2. **Fallback scan** (lines ~109-127) — when the primary finds nothing in range, find the nearest alive enemy purely so the player can rotate to face it ("look direction"). This scan filters by alive only — it does not check arrival-grace and does not check visibility.

The asymmetry is the bug. When the primary loop deliberately excludes a spaceship (e.g., a freshly-arrived enemy still inside its grace window, or an enemy hidden by smoke), the fallback can still pick that same spaceship as the look target. The player then visibly rotates toward an entity it is not allowed to engage, leaking information the primary filter is meant to suppress.

This is pre-existing — the lead-targeting change did not introduce it. The lead change only added math to the firing path; the look-direction path was untouched. The review flagged it as a separate bug worth fixing in its own diff.

## Design

Replace the two-pass structure with a single pass that tracks both candidates with consistent filtering:

```cpp
struct AcquireResult { iTargetInRange = -1; iTargetForLook = -1; };

for (each enemy spaceship i)
{
    if (!bAlive[i])                       continue;
    if (iTickArrived[i] + kiGrace > iTick) continue;   // Now applied to BOTH paths
    if (!bVisible[i])                     continue;   // Now applied to BOTH paths

    fDist = ...;

    // Track nearest in-range (existing primary)
    if (fDist <= fRange && fDist < fBestInRange) { iTargetInRange = i; fBestInRange = fDist; }

    // Track nearest overall (formerly the fallback) — same filters now
    if (fDist < fBestOverall) { iTargetForLook = i; fBestOverall = fDist; }
}
```

After the loop:
- If `iTargetInRange >= 0`, fire at it (with lead) and use it for look direction.
- Else if `iTargetForLook >= 0`, just face it (no firing).
- Else, no target — keep current facing.

Net result: the look direction respects the same exclusion rules the firing logic does. Enemies inside arrival-grace or behind visibility-occlusion never become look targets either.

## Out of scope

- Changing the visibility predicate itself or what counts as "in range".
- Changing arrival-grace duration or eligibility rules.
- Per-player priority weighting (closest-by-distance is the existing rule; this plan preserves it).
- Re-running `/external-grill-plan` on the firing logic — only the look-direction selection changes.
- Lead-targeting math (already landed in the prior session).

## Acceptance criteria

- `AcquireTarget` walks the spaceship list exactly once.
- The same alive + grace + visibility filter gates both the in-range and fallback candidate writes.
- An arrival-grace enemy with no other targets present causes the player to keep current facing (not rotate toward the grace-period enemy).
- A visibility-occluded enemy with no other targets present causes the player to keep current facing.
- `BrokenEngineSandbox` client and server build clean. No new heap allocations in the main loop (all locals are stack scalars).
- Manual playtest: spawn a fresh enemy, observe that follower players do not snap-rotate toward it during the grace window.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` — `AcquireTarget` member function (current dual-loop body around lines ~80-130; line numbers will drift, anchor on the symbol name).

## Notes

- This is a single-function change. Risk is low; the only behavioral question is whether existing players relied on the over-permissive look behavior. Likely not — it leaks the bug ("why did my ship turn toward nothing?") more than it provides utility.
- If the playtest reveals that having no look target at all feels worse than the current leak, fall back to a third tier: nearest alive ignoring grace and visibility, used **only** if no filtered candidate exists. That would still be a single pass — just track three candidates. Decide after the playtest, not up front.
- Keep the function under the existing size budget; if the single-pass version pushes it over, factor the per-spaceship test into a small `IsAcquireCandidate(i)` lambda local to the function — do not extract to a free function unless the predicate is reused elsewhere.
