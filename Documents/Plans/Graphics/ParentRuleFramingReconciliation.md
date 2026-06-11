# Decision: Parent-Rule Framing Reconciliation (Deposit Shaders / Layer Violations / Singletons)

## Context

**Decision plan (present options).** Three parent CLAUDE.md rules are in mild tension with documented leaf
realities, plus a related Graphics-manager file-docs consistency question. Each is a **framing/wording decision**
(pick which doc is authoritative, or add a qualifier) with **no code change**. Grouped because they are all
"parent convention vs leaf reality" reconciliations resolved by the same kind of judgment call. Each is
independent.

### Decision 1 — Deposit-shader EWNS convention (conflict 7)

`Engine/Data/Shaders/CLAUDE.md` states: "deposit shaders compute EWNS weights from world-space offset with
epsilon fallback to omnidirectional." But **`HexShieldLighting.frag`** instead projects the surface
center-normal's XY, and its epsilon fallback **zeroes the deposit** (not omnidirectional). The leaf documents
the divergence; the parent wording over-generalizes.

- **Options:** (A) narrow the parent to "**light-source** deposit shaders compute EWNS from world-space offset
  …; surface-normal deposit shaders (e.g. `HexShieldLighting.frag`) project the center-normal XY with a
  zero-deposit fallback"; (B) leave parent absolute + keep the leaf divergence note; (C) leave as-is.
- **Recommendation: (A)** — there are genuinely two deposit families (light-source vs surface-normal); naming
  them in the parent is accurate and removes the contradiction. (The report's Objects shader note already
  flagged this "deposit-pass divergence: center-normal projection, zero epsilon fallback".)

### Decision 2 — Engine layer-violation framing vs ImGuiManager (conflict 8)

`Engine/Source/CLAUDE.md` says engine types **naming game concepts** are "real layer violations worth fixing."
But `ImGuiManager.h` **forward-declares `game::` screen classes** and holds `std::unique_ptr<game::*Screen>`
members, documented as **deliberate** in `ImGuiManager.CLAUDE.md`. The two engine docs are in mild tension —
one calls it a violation worth fixing, the other sanctions it.

- **Options:** (A) reconcile by stating the principle precisely: "engine code reading `game::gp*` globals / a
  game-base member is by design; an engine type *owning* game-screen members (ImGuiManager) is a sanctioned
  exception documented at its leaf; what *is* a violation is [the specific class the rule means]." Pin down what
  the "real layer violation worth fixing" actually refers to so it stops sweeping in the sanctioned cases.
  (B) Soften the `Engine/Source` rule to "...worth scrutinizing" and let leaves mark sanctioned exceptions.
  (C) leave as-is.
- **Recommendation: (A)** — the project already has a strong stated position ("Engine reading `game::gp*`
  globals is by design — not a layer violation; do not file plans to 'inject' or 'decouple'"). The
  `Engine/Source` rule should be reworded to align with that position and explicitly carve out the
  ImGuiManager-owns-screens case, so the two engine docs stop contradicting. Confirm with the user what the
  rule's *intended* target is (the genuinely-bad case it wants to catch).

### Decision 3 — "Managers: singletons via `gp*` globals" vs non-conforming managers (conflict 9)

Root CLAUDE.md: "**Managers**: Singletons via `gp*` globals". But `MemoryManager.h`/`.cpp` is **free functions +
operator overloads, no class** (and no `gp*`), and `DebugDraw` is **entirely static** (file-scope state, no
`gp*`). The leaves note these as naming caveats; harmless, but the root convention reads as universal.

- **Options:** (A) add to the root rule "…most managers; a few are free-function/static modules without a
  `gp*` singleton (`MemoryManager`, `DebugDraw`) — see their leaves"; (B) rename those modules so the "Manager"
  suffix doesn't imply the singleton pattern (code change — heavier, probably not worth it); (C) leave as-is
  (the caveat lives at the leaves).
- **Recommendation: (A)** — a one-clause qualifier on the root rule is accurate and cheap; the leaves already
  carry the detail. Option B is a code/rename churn for a cosmetic naming concern — out of scope.

### Decision 4 — Graphics manager file-level docs consistency pass

The Graphics-managers refresh produced several per-file doc corrections (BufferManager occupancy buffer,
PipelineManager descriptor-verify location, CommandBufferManager re-record framing, ParticleManager wind claim —
these are the `StaleDocClaimsSweep.md` Group A items). Beyond those specific fixes, the refresh flagged that the
`Graphics/Managers/` file-level docs would benefit from a **consistency pass** to ensure the manager-by-manager
descriptions use one framing (record-once vs re-record, CB lifetimes, fence/semaphore ownership) after the
several corrections. This is a **scope decision**: is a dedicated managers-docs consistency pass warranted, or
do the targeted `StaleDocClaimsSweep` fixes suffice?

- **Options:** (A) do only the targeted stale-claim fixes (in `StaleDocClaimsSweep.md`) and close this — no
  separate pass; (B) run a focused consistency pass over all `Graphics/Managers/` CLAUDE.md after the targeted
  fixes land, normalizing terminology. **Recommendation: (A)** unless the user wants the broader normalization —
  the targeted fixes resolve the known inaccuracies; a full pass is polish.

## Design

This plan produces **doc framing decisions**, not code. For each: confirm the option with the user, then apply
the chosen wording via `update-claude-docs` (decisions 1-3) or scope the managers pass (decision 4). Verify each
cited leaf reality against current code/docs at execution.

## Out of scope

- **Any code change** — all four are doc framing/scope decisions. (Option B in decisions 2/3 would be a code
  rename/refactor; explicitly de-recommended and not part of this plan.)
- The specific stale Graphics-manager doc *claims* (BufferManager/PipelineManager/etc.) — those are the
  `StaleDocClaimsSweep.md` Group A fixes; decision 4 here only rules on whether a *broader* consistency pass is
  also wanted.
- The aggregation/scope-rule qualifiers (ExternalHeaders, BT_CLIENT, Engine.h, engine→game constant) — separate
  `AggregationAndScopeRuleQualifiers.md` decision plan.
- The `## Out of scope` backfill / Plans-Features framing governance — separate plan.

## Critical files

- **Decision 1:** `Engine/Data/Shaders/CLAUDE.md` (EWNS deposit-shader rule); `HexShieldLighting.frag`
  (the divergent surface-normal deposit) and its leaf note.
- **Decision 2:** `Engine/Source/CLAUDE.md` (layer-violation rule), `Engine/Source/Graphics/Managers/
  ImGuiManager.CLAUDE.md` (the sanctioned game-screen-ownership note), root `CLAUDE.md` ("Engine reading
  `game::gp*` is by design" position to align with).
- **Decision 3:** root `CLAUDE.md` ("Managers: singletons via `gp*`"), `Engine/Source/Memory/CLAUDE.md`
  (free-function/static caveat), the DebugDraw leaf (entirely-static caveat).
- **Decision 4:** `Engine/Source/Graphics/Managers/CLAUDE.md` and the per-manager leaf docs.

## Notes

- **Decision plan (present options)** — deliverable is resolved CLAUDE.md wording / a scope ruling; no
  build/runtime/CRC impact (Risks = 0). Resolve in `/external-grill-plan`.
- Decisions 2 and 3 touch **root / `Engine/Source` CLAUDE.md** — those edits land under the normal doc process
  (gated), not silently from this plan.
- All four lean toward a light qualifier or "targeted fixes suffice"; the heavier code-rename options are
  explicitly de-recommended.
