# Architecture: Water Wrapper Stale Invariant Comment & Ordering Drift

## Context

Source: /external-architecture-review on `Engine/Source/Ui` (non-recursive). The Ui directory's shader-safety
bounds are comment-enforced with exact citations (documented convention, `Engine/Source/Ui/CLAUDE.md`), which
makes a *stale* invariant comment actively misleading: `WaterWrappersBase.cpp:7-8` claims the water-normal
texture-index defaults are "One=0, Two=1, Three=11 (SeaWaves) — must match
`TextureManager::kiWaterNormalSeaWavesIndex`", but the actual defaults are One=**12** (`:13`), Two=**15**
(`:24`), Three=**2** (`:31`), and `TextureManager::kiWaterNormalSeaWavesIndex`
(`Graphics/Managers/TextureManager.h:72`) has no consumer other than this comment. Either the constant and
comment are both dead, or a real index-pairing invariant was silently broken when the defaults were retuned.
Separately, the same pair has declaration-order drift against the slider-order convention that governs it.

## Design

### Engine/Source/Ui/WaterWrappersBase.cpp + Engine/Source/Graphics/Managers/TextureManager.h
- Reconcile the stale pairing: determine (git history / texture-pack manifest) whether
  `kiWaterNormalSeaWavesIndex` ever gated behavior or only ever documented a default. Expected outcome —
  both the `WaterWrappersBase.cpp:7-8` comment and the orphaned `TextureManager.h:72` constant are deleted;
  if a real pairing invariant turns out to be live, instead rewrite the comment against the current defaults
  and cite the consumer. [~15m]

### Engine/Source/Ui/WaterWrappersBase.h / WaterWrappersBase.cpp
- Fix the intra-pair ordering drift: the header groups `gWaterLowCount`/`gWaterMediumCount`/`gWaterHigh*` in
  a trailing block (`WaterWrappersBase.h:104-109`) while the `.cpp` defines the High block at `:57-60`
  (between Skybox and Height Darken) and the counts inline at `:70` and `:88`. Per
  `Engine/Source/Ui/CLAUDE.md` / `Screens/TweaksScreen/CLAUDE.md`, declaration order within a pair must
  match the Tweaks slider order; these are annotated as slider-map exceptions in the header comment, but the
  `.h` and `.cpp` orders diverge from *each other*. Align the `.cpp` definition order to the header (or
  extend the exception annotation to state the `.cpp` placement rationale). [~10m]

## Critical files
- `Engine/Source/Ui/WaterWrappersBase.h`
- `Engine/Source/Ui/WaterWrappersBase.cpp`
- `Engine/Source/Graphics/Managers/TextureManager.h` (constant deletion, pending the reconcile outcome)

## Out of scope
- Changing any wrapper default/min/max *value* — this plan touches comments, declaration order, and a dead
  constant only; water rendering output must be bit-identical.
- The `// DT: TEMP` markers on `WaterWrappersBase.cpp:103,116-118` / `WaterWrappersBase.h:81` — owned by the
  refactor-clean pass (`Ui/Refactor_*` plans), not this invariant reconcile.
- The snap-step `fract()` pact comments at `WaterWrappersBase.cpp:122-123` — verified-live convention,
  untouched.

## Acceptance criteria
- The reconcile question is answered with evidence (history or a live consumer) and recorded in the edit;
  no "must match" comment remains whose claimed values differ from the adjacent defaults.
- Client and server build clean (wrapper pairs compile in both builds; values unchanged).

## Notes
- No determinism/CRC exposure: water wrappers are client-presentation tuning read at defaults on the server;
  the plan changes no stored value. Single grill decision pre-staged: delete vs. rewrite the
  `kiWaterNormalSeaWavesIndex` pairing once history is checked.

## Verification Notes (2026-06-10)
- Stale-comment claim re-derived from source: `WaterWrappersBase.cpp:8` asserts defaults "One=0, Two=1,
  Three=11 (SeaWaves) — must match TextureManager::kiWaterNormalSeaWavesIndex"; actual defaults are
  `gWaterNormalIndexOne` = 12 (`:13`), `gWaterNormalIndexTwo` = 15 (`:24`), `gWaterNormalIndexThree` = 2
  (`:31`). `kiWaterNormalSeaWavesIndex = 11` confirmed at `TextureManager.h:72`; repo-wide grep finds zero
  code consumers — only its own definition and the stale comment. Index 11 = "SeaWaves" in
  `kpWaterNormalNames` (`TextureManager.h:66-71`), so the constant was consistent with the *old* comment;
  the defaults were retuned (12 = SeaWavesB, 15 = StonesAndRipples, 2 = Foam per the names table) without
  updating either. The "constant + comment both dead" expected outcome looks right; git-history check stays
  execution work.
- Ordering-drift claim confirmed: header trailing block `WaterWrappersBase.h:104-109`
  (`gWaterLowCount`/`gWaterMediumCount`/`gWaterHigh*`, annotated "radio-button-bound but not via slider
  map; pure-internal High*") vs `.cpp` High block at `:57-60` (between Skybox `:41-55` and Height Darken
  `:62-67`) and counts inline at `:70`/`:88`. The `.h`/`.cpp` intra-pair divergence is real; the
  TweaksScreen CLAUDE.md ordering convention ("Wrapper global order in the matching `<Tab>WrappersBase.{h,cpp}`
  must match") covers both files. Wrapper definitions have no init-order dependency on each other, so
  reordering `.cpp` definitions is safe.
- Out-of-scope cross-references confirmed: `// DT: TEMP` at `.cpp:103/116-118` + `.h:81` owned by the two
  `Ui/Refactor_*` plans; `fract()` pact comment at `.cpp:122-123` is live convention (`gWaterColorNoiseMultiplierOne`
  step 0.1). Same-file plans (`Refactor_StyleMechanics`, `Refactor_WaterZOffsetTempDecision`) — co-schedule.
