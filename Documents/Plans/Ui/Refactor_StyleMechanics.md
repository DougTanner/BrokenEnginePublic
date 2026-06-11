# Refactor: Style Mechanics (Engine/Source/Ui)

## Context

Source: /external-refactor-clean on `Engine/Source/Ui` (non-recursive). The directory is clean on the heavy
checklists — no hot-path allocation (all wrapper globals are static-init; `CurveData::AddPoint` can never
reallocate at runtime thanks to the ctor `reserve` + 16-point cap), no oversized files, no DirectXMath misuse,
no bool proliferation. What remains is one mechanical batch of dead noise and two one-sentence doc gaps.

## Design

### Engine/Source/Ui/WaterWrappersBase.cpp + Engine/Source/Ui/GraphicsSettingsWrappersBase.cpp
- Drop the five no-op `std::move(...)` wrappers on `Wrapper` ctor arguments: `gWaterLowCount`
  (`WaterWrappersBase.cpp:70`), `gWaterMediumCount` (`:88`), `gPresentMode`
  (`GraphicsSettingsWrappersBase.cpp:7`), `gSampleCount` (`:9`), `gWaterShapeDetail` (`:15`). The ctor takes
  `const std::vector<T>&` (`WrapperBase.h:38-39`) and converts elementwise into `mAllowed`
  (`WrapperBase.h:46-51`) — the prvalue temporaries bind directly, the `std::move` casts are pure dead noise
  (and by-value+move would be wrong: nothing stores the `vector<T>`). [~5m]
- Normalize the MSVC-only `31i64` (`WaterWrappersBase.cpp:70`) and `255i64` (`:88`) literals to the file's
  own dominant explicit form `int64_t {31}` / `int64_t {255}` (matching `:13/:24/:31`; the explicit type is
  required for `T=int64_t` template deduction). Same lines as the `std::move` drops — one edit. [~5m]

### Engine/Source/Ui/CurveData.h
- Delete the dead local `iCount` in `CurveData::ComputeTangents` — declaration `:151` and the
  `std::ignore = iCount;` laundering at `:171`; no ASSERT or other read exists. [~5m]

### Engine/Source/Ui/WaterWrappersBase.cpp
- Delete the three stale `// DT: TEMP` markers on shipping wrappers `gWaterFresnel` (`:116`),
  `gWaterNoiseFrequency` (`:117`), `gWaterNoiseAmount` (`:118`) — all three feed live per-frame uniforms
  (`GlobalUniforms.cpp:469-470/:497`); comment-only deletion. The fourth marker pair
  (`gWaterZOffsetTemp`, `.h:81`/`.cpp:103`) is NOT touched here — it needs a promote-or-remove decision,
  owned by `Ui/Refactor_WaterZOffsetTempDecision.md`. [~5m]

### Engine/Source/Ui/CLAUDE.md
- One sentence documenting `gMiscTestOne`/`gMiscTestTwo` (`MiscWrappersBase.cpp:7-8`, bound to the Tweaks
  Misc "Test One"/"Test Two" sliders, zero readers repo-wide) as intentional quick-iteration scratch knobs —
  or delete the pair if the user confirms they are vestigial (grill question). [~5m]
- One sentence documenting the `gCombineCurveOld`/`gCombineCurveNew` + `gbUseCombineCurveNew` A/B tuning
  scaffolding (`LightingWrappersBase.h:72-74`, `.cpp:71-73`; toggle consumed at
  `TweaksScreenLighting.cpp:222-227`, curve consumed at `LightingUniforms.cpp:47`) — deliberate
  compare-against-baseline apparatus, currently undocumented unlike the directory's other curve exceptions.
  Collapsing to one curve is deferred until tuning settles (out of scope). [~5m]

## Critical files
- `Engine/Source/Ui/WaterWrappersBase.cpp`
- `Engine/Source/Ui/GraphicsSettingsWrappersBase.cpp`
- `Engine/Source/Ui/CurveData.h`
- `Engine/Source/Ui/CLAUDE.md`

## Out of scope
- `gWaterZOffsetTemp` promote-or-remove — `Ui/Refactor_WaterZOffsetTempDecision.md`.
- The dead `CurveData::Changed()`/`ComputeHash` machinery (`CurveData.h:40-48/201-209/214-215`) —
  `Ui/Architecture_CurveDataDeadChangeDetection.md` (same file, different lines; co-schedule).
- The stale `kiWaterNormalSeaWavesIndex` comment and `.h`/`.cpp` ordering drift in the same Water pair —
  `Ui/Architecture_WaterWrapperInvariants.md` (same file; co-schedule).
- `CurveData.h`'s `int`-index/`operator[]` style vs guide rules 13/16/17, and `Wrapper::GetIndex`'s manual
  counter vs the rule-8 init-statement form — code-style-review territory; not worth a standalone pass and
  partially obsoleted by the CurveWidget ImPlot rebuild.
- `CurveWidget` decomposition — assessed and rejected (every block shares 5-7 locals; extraction trades a
  readable immediate-mode sequence for parameter plumbing) and superseded by
  `Ui/Architecture_LibraryReplacement.md`.
- `TextureManager.cpp:25-26/56-57`'s own `i64` literals — outside the target directory.

## Acceptance criteria
- Client and server build clean; all five `std::move` sites compile identically (value-identical literals);
  no `// DT: TEMP` remains in `WaterWrappersBase.{h,cpp}` except the `gWaterZOffsetTemp` pair.

## Notes
- All items are dead casts, dead locals, comment deletions, value-identical literal reforms, or doc
  sentences — zero behavior change, no determinism/CRC, `kiVersion`, replay, guard-scope, or
  allocation-tracking exposure. One grill question pre-staged: keep-and-document vs delete
  `gMiscTestOne`/`gMiscTestTwo`.

## Verification Notes (2026-06-10)
- All five `std::move` sites confirmed at the cited lines (`WaterWrappersBase.cpp:70/:88`,
  `GraphicsSettingsWrappersBase.cpp:7/:9/:15`); the discrete-enum ctor is
  `Wrapper(T value, const std::vector<T>& rAllowedValues)` (`WrapperBase.h:38-39`) converting elementwise
  into `mAllowed` (`:46-51`) — the moves are no-op casts on prvalue temporaries, removal is value-identical.
  `31i64`/`255i64` confirmed at `:70`/`:88`; `int64_t {31}` form matches the file's `:13/:24/:31` style and
  keeps `T = int64_t` deduction against `std::vector<int64_t>`.
- Dead `iCount` confirmed: declared `CurveData.h:151`, only other reference is `std::ignore = iCount;` at
  `:171` — no ASSERT or read.
- The three `// DT: TEMP` markers confirmed at `WaterWrappersBase.cpp:116-118` (`gWaterFresnel`,
  `gWaterNoiseFrequency`, `gWaterNoiseAmount`); all three feed live uniforms
  (`GlobalUniforms.cpp:469-470` noise frequency/amount, `:497` fresnel) — comment-only deletion is correct.
- `gMiscTestOne`/`gMiscTestTwo`: repo-wide grep confirms exactly three reference sites each —
  `MiscWrappersBase.{h:9-10,cpp:7-8}` and the Tweaks Misc bindings (`TweaksScreenMisc.cpp:17-18`,
  "Test One"/"Test Two"). Zero uniform/code readers; the grill question (document vs delete) stands.
- A/B scaffolding confirmed: `gCombineCurveOld`/`gCombineCurveNew`/`gbUseCombineCurveNew` at
  `LightingWrappersBase.h:72-74` (BT_CLIENT-guarded) / `.cpp:71-73`; toggle button at
  `TweaksScreenLighting.cpp:222-227`; curve baked at `LightingUniforms.cpp:47`. Currently undocumented in
  `Ui/CLAUDE.md` — the doc sentence is warranted.
- Cross-plan boundaries hold: `gWaterZOffsetTemp` (`.h:81`/`.cpp:103`) untouched here
  (`Refactor_WaterZOffsetTempDecision.md`); `Changed()`/`ComputeHash` deletion
  (`Architecture_CurveDataDeadChangeDetection.md`) is different lines in `CurveData.h`;
  `kiWaterNormalSeaWavesIndex`/ordering (`Architecture_WaterWrapperInvariants.md`) is different lines in
  `WaterWrappersBase.cpp`. All three same-file plans — co-schedule. `Frame/ScaleEngineToMeters.md` touches
  `WrapperBase.cpp:9` only — no overlap. `TextureManager.cpp` `128i64`/`64i64` confirmed at `:25-26/:56-57`
  (correctly out of scope).
