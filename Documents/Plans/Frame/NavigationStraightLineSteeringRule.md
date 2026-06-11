# Decision: Collections "Never Straight-Line Steering" Rule vs Code

## Context

**Decision plan (present options).** The game Collections hub
(`Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md`, "Navigation") states an absolute rule:

> **Navigation**: Entities moving to a destination must use `NavQueryDirection` (per-cell `NavData` from
> `FrameStaticData`), never straight-line steering.

The code **contradicts this in two places**:

1. **`PlayersNavigation.cpp`** (the report cites lines `282-289`, `338-345`, `396-405`) falls back to
   **straight-line `XMVector3Normalize`** when `NavQueryDirection` returns a near-zero direction (one path logs
   `kWarning`). This is a *failure fallback* — when nav has no answer, steer straight rather than freeze.
   Almost certainly intentional.
2. **`SpaceshipsNavigation.cpp`** **never calls `NavQueryDirection` at all** — its `ComputeSteering` turns
   straight toward the nearest player / island center, with reactive `ApplyTerrainBounce` / `AvoidTerrain`.
   Spaceships are a fundamentally different (reactive, no nav-destination) steering model.

So the rule as written is false for the actual codebase. The fix is a **doc decision**: scope/soften the rule,
or (much less likely) change the code to obey the absolute rule. The line numbers are part of the deterministic
sim path — **do not change sim behavior to satisfy a doc rule** without explicit confirmation (project
directive: "Never remove working features as part of a 'fix'").

## Design

Resolve in the grill which framing is correct, then edit the hub doc (no code change in the recommended
options). Verify the cited code paths at execution (line numbers drift) before settling the wording.

- **Option A — scope the rule to nav-destination entities, with a documented failure fallback.** Reword to:
  "Entities navigating to a *destination* (Players) use `NavQueryDirection` (per-cell `NavData`); when nav
  returns a near-zero direction, straight-line normalize is the sanctioned fallback. Reactive enemies
  (Spaceships) use direct steering toward a target with terrain avoidance and do not use `NavData`." This
  matches the code exactly. **Recommended** — it tells the truth about both steering models and keeps the
  `NavData`-first guidance for the entities it actually applies to.
- **Option B — soften to "primary steering method".** "Destination navigation should *primarily* use
  `NavQueryDirection`; straight-line steering is permitted as a fallback or for reactive enemies." Looser; less
  precise than A about *which* collections do what.
- **Option C — change the code to obey the absolute rule.** Make Spaceships use `NavQueryDirection` and remove
  the Players straight-line fallback. **Not recommended** — this alters working, deterministic sim behavior
  (Spaceships' reactive model is by design; the Players fallback prevents a freeze when nav has no path), would
  require CRC/playtest validation, and is a *feature/behavior* change masquerading as a doc fix. Only pursue on
  explicit user request, and as a separate sim-behavior plan (not this doc decision).

**Recommendation: A.** It is the accurate description and requires only a hub-doc edit. Confirm the
Players-fallback and Spaceships-model intent with the user (they appear intentional from the code) before
finalizing.

## Out of scope

- **Any change to `PlayersNavigation.cpp` / `SpaceshipsNavigation.cpp` sim behavior** — the recommended options
  are doc-only. Code changes (Option C) are explicitly out of scope unless the user requests them, and would be
  a separate determinism-gated plan.
- `NavQueryDirection` / `NavData` / `NavBuild` themselves — unchanged.
- The `NavBuildSplit.md` refactor — unrelated (file organization, not the steering rule).
- Other navigation/steering helpers (`TerrainUtils` gradient-following) beyond noting they exist as the
  Players/Spaceships terrain helpers.

## Acceptance criteria

- The Collections hub "Navigation" rule accurately describes both steering models (Players: `NavData` +
  straight-line failure fallback; Spaceships: reactive direct steering, no `NavData`) — per the chosen option.
- No sim/behavior change (recommended options A/B are doc-only); if Option C is chosen it becomes a separate
  determinism-validated plan, not part of this doc resolution.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md` — the "Navigation" rule (the edit target).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the straight-line
  failure fallback (report cites `:282-289`, `:338-345`, `:396-405`; verify at execution) and the `kWarning`
  log path. Read-only confirmation that the fallback is intentional.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsNavigation.cpp` —
  `ComputeSteering` (reactive, no `NavQueryDirection`). Read-only confirmation of the second steering model.

## Notes

- **Decision plan (present options)** — recommended resolution is a hub-doc edit only (Risks = 0). The only
  code-touching option (C) is explicitly de-recommended and would split into its own sim-behavior plan.
- Touches the **determinism/sim path** only if Option C is (against recommendation) chosen — flag prominently
  for the grill, since changing steering changes the CRC stream.
