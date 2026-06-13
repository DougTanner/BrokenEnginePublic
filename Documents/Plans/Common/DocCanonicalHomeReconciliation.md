# Decision: Doc Canonical-Home & Cross-Doc Conflict Reconciliation

## Context

**Decision plan (present options).** Six cross-doc inconsistencies surfaced by the repo-wide CLAUDE.md audit
where two docs cover the *same* topic and either (a) point at each other in a loop, (b) duplicate the same fact,
or (c) give *conflicting* rationales/rules for the same code. Each is resolved by a **canonical-home or
pick-one-wording decision** — none is a planning-tree-rule question (that is `PlanDocGovernanceDecisions.md`) and
none is a parent-rule-vs-leaf-exception qualifier (that was `ParentRuleFramingReconciliation.md`, since **landed and removed**). Grouped because
they are the same kind of judgment call ("which doc owns this, or which wording is right"). Each is independent;
the plan can land any subset.

Five of the six are **documentation-only**. One (Decision 5 — Water.frag in-shader lerp) carries a possible
**shader refactor** branch and is the only one with code exposure; it is presented as exception-vs-refactor.

### Decision 1 — DataPacker bake/split contract: circular deferral, pick canonical home (finding R)

`DataPacker/Source/CLAUDE.md` (Architecture, para 1) says the full bake/split contract — "route table, two-stage
version sentinels, auto-crop, mesh subdivision, leaf rejection" — "lives in `ExportJobs/CLAUDE.md`". But
`ExportJobs/CLAUDE.md` ("Island Chunk Ingest") points back: "The Gaea bake, archetype patching, crop/downsample,
route split, and two-stage `BakeVersion.txt` / `SplitVersion.txt` dirty sentinels all live in the `Island/` bake
TUs — see [../CLAUDE.md]." So the two docs form a **deferral loop**, and the actual two-stage sentinel mechanism
(when `BakeVersion.txt` vs `SplitVersion.txt` is written/checked, the leaf-rejection rule) is documented in
*neither* CLAUDE.md — it lives only in `BakeIslandIntermediates.cpp` / `BakeRoute.cpp` comments.

- **OVERLAP:** `StaleDocClaimsSweep.md` Group A item 9 already flags this exact circular delegation and prescribes
  a concrete fix ("Make the parent claim its own bake paragraphs (break the loop)"). **This decision should
  defer the mechanical loop-break to that item and only resolve the open *governance* question it leaves**:
  whether the parent's bake paragraphs should additionally *absorb a short statement of the sentinel contract*
  (so it lives in a CLAUDE.md, not just `.cpp` comments), or whether deferring sentinel detail to the source
  comments is acceptable.
- **Options:** (A) parent (`DataPacker/Source/CLAUDE.md`) owns the bake/split paragraphs (per item 9) **and**
  adds a one-line statement of the two-stage sentinel contract pointing at the authoritative `.cpp`; ExportJobs
  keeps only its chunk-ingest paragraph and a single up-pointer. (B) parent owns it (per item 9) but leaves the
  sentinel mechanism in `.cpp` comments (no CLAUDE.md sentence) — minimal. (C) ExportJobs owns the whole
  contract and the parent up-points (inverts the current loop). **Recommendation: (A)** — breaks the loop the
  way item 9 already chose (parent-canonical) and pins the sentinel contract somewhere discoverable. Coordinate
  with item 9 so the two don't double-edit the same paragraphs.

### Decision 2 — Family-specific skip-normalize call sites: claimed in Water/CLAUDE.md but absent (finding S)

`Engine/Data/Shaders/CLAUDE.md` ("Unit-preserving identities (skip the redundant `normalize()`)") states:
"Family-specific call sites are documented in the relevant child CLAUDE.md (Water, Model, Objects, Particles)."
But `Water/CLAUDE.md` has **no skip-normalize / unit-preserving-identity (reflect/cross) coverage** — its only
`normalize()` mentions are the scale-invariant weighted-sum-then-normalize (Multi-sample normal composition) and
the Gerstner `normalize(cross(T,B))`, neither of which is the "skip the redundant normalize because the result is
already unit" identity the parent points to. So the parent over-promises Water coverage.

- **Options:** (A) add the missing skip-normalize call-site note to `Water/CLAUDE.md` (if Water genuinely has
  such a site — confirm against `Water.frag` / `WaterSkyboxOne.frag` at execution). (B) if Water has *no* such
  site, drop "Water" from the parent's "(Water, Model, Objects, Particles)" list so the parent stops claiming
  coverage that doesn't exist. **Recommendation: confirm first, then (A) or (B)** — verify whether Water actually
  relies on a unit-preserving identity (a `reflect`/`cross` result consumed without re-normalize). If yes → (A);
  if no → (B). The parent and the one chosen side must end consistent.

### Decision 3 — "Single `ThirdParty.<Config>.lib`" stated in two docs, pick canonical (finding T)

The fact "everything compiles into a single `ThirdParty.<Config>.lib` linked by client, server, and DataPacker"
appears verbatim in **both** `ThirdParty/CLAUDE.md` ("Build Organization") and
`ThirdParty/Prebuilts/Platforms/VisualStudio2026/CLAUDE.md` ("Overview"). Duplicate fact across a parent/child
pair — the repo's no-sibling-duplication posture wants one canonical statement.

- **Options:** (A) keep the full statement in the **Prebuilts VisualStudio2026 CLAUDE.md** (the build-config doc
  that owns output naming / configs) and reduce the parent `ThirdParty/CLAUDE.md` to a one-line pointer. (B) keep
  it in the parent (library-inventory/policy owner) and trim the child. **Recommendation: (A)** — the
  single-lib-output fact is build-configuration, which is the VisualStudio2026 doc's domain (it already owns
  `TargetName`, configs, consumer auto-build); the parent's "Build Organization" should up/down-point to it.

### Decision 4 — "All headers aggregated through Common.h" stated in Common/CLAUDE.md + root, pick canonical (finding U)

`Common/CLAUDE.md` Overview says "All headers are aggregated through `Common.h` (pulled in by `Pch.h`); new
headers are added only there." Root `CLAUDE.md`'s directory index already says "`Common.h` is the single
aggregation header (included by `Pch.h`)". Duplicate of the root directory-index fact.

- **Options:** (A) the **root directory index** stays canonical for the one-line "Common.h is the aggregation
  header" fact; `Common/CLAUDE.md` keeps only the *non-duplicated* delta ("new headers are added only there" —
  an authoring rule the root doesn't state) and drops the re-statement of the aggregation fact. (B) leave both
  (accept the small overlap). **Recommendation: (A)** — keep the authoring-rule delta in `Common/CLAUDE.md`,
  drop the duplicated aggregation-fact sentence. **NOTE:** the root `CLAUDE.md` is *out of edit scope* for the
  doc-edit batch (root-CLAUDE.md change-ban); this decision only trims the `Common/CLAUDE.md` side and is
  recorded so the canonical choice is on record — no root edit.

### Decision 5 — Water.frag in-shader camera-height lerp vs the CPU-resolve rule (finding V) — code exposure

`Engine/Source/Graphics/Render/CLAUDE.md` ("Camera-Height-Conditional Uniforms") states the rule: "When a uniform
varies with camera eye height, lerp CPU-side and upload the single resolved float — **never pass endpoint heights
and low/high targets to the shader**." But `Water.frag:157-159` does exactly that for the three normal-weight
samples: it uploads both `fWaterNormalWeight{One,Two,Three}{Min,Max}` *and* `fCameraHeightZoomFactor`, and runs
the `mix(min, max, factor)` lerp **in-shader**. `Water/CLAUDE.md` documents the in-shader form as deliberate
("Per-sample weights interpolate between min/max endpoints by a CPU-resolved camera-zoom factor … without a
separate pipeline"). So the Water leaf and the Render rule are in tension.

- **Options:** (A) **document an intentional exception** — Water deliberately keeps the per-sample min/max
  endpoints shader-side so all three samples share one `fCameraHeightZoomFactor` upload (vs. three resolved
  floats), and the close-vs-far weighting is tuned per sample; add a carve-out to the Render rule naming Water as
  the sanctioned exception, and note it in `Water/CLAUDE.md`. Doc-only. (B) **refactor to the rule** — resolve
  the three weights CPU-side (`mix` on the host) and upload three single floats, dropping the six min/max
  uniforms and the factor; matches the rule, removes shader-side branching. Shader + uniform-population change
  (`Water.frag` + the Render water population + the layout struct) — a real refactor, DataPacker recompile +
  visual smoke-test, not compile-checked. **Recommendation: present both; lean (A)** unless the user wants the
  uniform surface trimmed — the in-shader form is a deliberate, working perf/feel tradeoff and (B) is genuine
  code churn for a consistency win. If (B) is chosen it becomes a **separate shader/uniform refactor plan**, not
  part of this decision doc.

### Decision 6 — SmokeTrails vs WindTrails `uiFrameId` rationale conflict, pick one (finding W)

Both `SmokeTrails/CLAUDE.md` and `WindTrails/CLAUDE.md` agree the `Render()` `uiFrameId` parameter is unused, but
give **different reasons** for why it exists:
- `SmokeTrails/CLAUDE.md`: "its frame-id parameter exists only for **signature parity** with `WindTrails`".
- `WindTrails/CLAUDE.md`: "carries an extra `uiFrameId` parameter solely so it is **excluded from the
  auto-generated `InterpolateRenderTypes` walk**".

The code (`FrameBase.h:245-246`) confirms the *functional* reason is the exclusion mechanism: "SmokeTrails and
WindTrails are excluded from `ForEachInterpolateRender` because their `Render()` takes `uiFrameId`. They are
called separately in `RenderFrameMain()` with the per-frame ID." So "signature parity" is a downstream
consequence, not the cause — the SmokeTrails framing is the misleading one.

- **Options:** (A) make `SmokeTrails/CLAUDE.md` use the **exclusion-from-`InterpolateRenderTypes`** rationale
  (matching WindTrails and `FrameBase.h:245`), since that is the real reason both signatures carry the param.
  (B) leave both (accept divergent framings). **Recommendation: (A)** — both should cite the
  `InterpolateRenderTypes`-exclusion mechanism; "signature parity" alone hides *why* the parity exists.

## Design

This plan produces **canonical-home rulings + pick-one wordings** (Decisions 1-4, 6 are doc-only; Decision 5 is
exception-vs-refactor). For each: confirm the option with the user in `/external-grill-plan`, verify the cited
leaf/parent text against current docs and the cited code at execution (line numbers drift), then apply via the
standard `update-claude-docs` step. Decision 5's option (B) spins out into a separate shader refactor plan if
chosen.

## Out of scope

- **Engine/game code changes** — five of six are doc-only. Decision 5 option (B) is a shader/uniform refactor and
  is explicitly **not** part of this decision doc; if chosen it becomes its own plan.
- **The planning-tree rule set** (`## Out of scope` backfill, Plans/Features framing) — that is
  `PlanDocGovernanceDecisions.md`.
- **Parent-rule-vs-leaf-exception qualifiers** (EWNS deposit, layer-violation, singleton, ExternalHeaders,
  BT_CLIENT, Engine.h) — those are `ParentRuleFramingReconciliation.md` (**landed and removed**) and `AggregationAndScopeRuleQualifiers.md`.
- **The mechanical loop-break of the DataPacker bake/split delegation** — owned by `StaleDocClaimsSweep.md`
  item 9; Decision 1 here only rules on whether the sentinel contract additionally gets a CLAUDE.md home.
- **Root `CLAUDE.md` edits** — Decision 4 trims only the `Common/CLAUDE.md` side; the root directory-index line
  is not touched (root change-ban).
- **The substance of the bake/skip-normalize/water behavior the docs describe** — unchanged; only which doc owns
  the description (and Decision 5's optional uniform-resolve location).

## Acceptance criteria

- Decision 1: the DataPacker bake/split delegation loop is broken (via item 9) and the two-stage sentinel
  contract has a chosen home (CLAUDE.md sentence + `.cpp` pointer, or `.cpp`-only by ruling) — the two docs no
  longer point at each other for it.
- Decision 2: `Engine/Data/Shaders/CLAUDE.md`'s "(Water, Model, Objects, Particles)" claim matches reality —
  either Water gains the skip-normalize note or Water is dropped from the list.
- Decision 3: the single-`ThirdParty.lib` fact lives in one canonical doc; the other carries a pointer.
- Decision 4: `Common/CLAUDE.md` no longer re-states the root's aggregation-header fact (keeps only the
  add-headers-here delta); root untouched.
- Decision 5: resolved as either a documented Water exception in the Render rule + `Water/CLAUDE.md`, or a spun-out
  refactor plan — the Render rule and `Water/CLAUDE.md` end mutually consistent.
- Decision 6: `SmokeTrails/CLAUDE.md` and `WindTrails/CLAUDE.md` give the same (`InterpolateRenderTypes`-exclusion)
  rationale for the unused `uiFrameId`.
- No code/behavior change (unless Decision 5 (B) is chosen and spun out).

## Critical files

- **Decision 1:** `DataPacker/Source/CLAUDE.md` (bake/split deferral para), `DataPacker/Source/ExportJobs/
  CLAUDE.md` (Island Chunk Ingest back-pointer); read-only `BakeIslandIntermediates.cpp` / `BakeRoute.cpp`
  (sentinel-contract source of truth). Coordinate with `StaleDocClaimsSweep.md` item 9.
- **Decision 2:** `Engine/Data/Shaders/CLAUDE.md` (unit-preserving-identities note), `Engine/Data/Shaders/Water/
  CLAUDE.md`; read-only `Water.frag` / `WaterSkyboxOne.frag` to confirm whether a skip-normalize site exists.
- **Decision 3:** `ThirdParty/CLAUDE.md` (Build Organization), `ThirdParty/Prebuilts/Platforms/VisualStudio2026/
  CLAUDE.md` (Overview).
- **Decision 4:** `Common/CLAUDE.md` (Overview aggregation sentence); root `CLAUDE.md` directory index
  (read-only reference — not edited).
- **Decision 5:** `Engine/Source/Graphics/Render/CLAUDE.md` (Camera-Height-Conditional Uniforms rule),
  `Engine/Data/Shaders/Water/Water.frag` (`:157-159`), `Engine/Data/Shaders/Water/CLAUDE.md`; if option (B):
  the Render water uniform population + the `MainLayout` weight uniforms.
- **Decision 6:** `Engine/Source/Frame/Collections/SmokeTrails/CLAUDE.md`, `Engine/Source/Frame/Collections/
  WindTrails/CLAUDE.md`; read-only `Engine/Source/Frame/FrameBase.h` (`:245-246`, the `InterpolateRenderTypes`
  exclusion comment).

## Notes

- **Decision plan (present options)** — deliverable is canonical-home rulings + small doc edits; no
  build/runtime/CRC impact for Decisions 1-4/6 (Risks = 0). Decision 5 has a code branch (option B) that, if
  chosen, becomes a separate shader/uniform refactor plan with its own DataPacker-recompile + playtest risk.
- All six lean toward the low-touch option (pick-canonical / pick-one-wording / document-the-exception); the only
  heavier path is Decision 5 (B).
- Resolve in `/external-grill-plan`. Decision 4 records a canonical choice but edits only the `Common/CLAUDE.md`
  side (root change-ban).
