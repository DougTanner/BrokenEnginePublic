<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Retire the Release Analysis Allow List

## Context

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineAnalysis.ruleset` gates Release
Microsoft code analysis: `IncludeAll` minus an explicit per-code allow list of `<Rule Id="..."
Action="None" />` entries, with `CodeAnalysisTreatWarningsAsErrors=true` promoting everything else
to a build error. The allow list was seeded with the codes already present when the gate landed, so
the gate could be introduced without a cleanup pass. Each entry is a suppression: those codes are
invisible now, not merely non-fatal, including on newly written code.

Six codes are suppressed. Observed occurrences (client/server) at the time the gate landed:

| Code | Client | Server | Meaning |
|------|--------|--------|---------|
| C26497 | 7 | 2 | function could be `constexpr` |
| C26494 | 5 | 2 | variable is uninitialized |
| C26473 | 1 | 1 | cast between identical pointer types |
| C26445 | 1 | 0 | `string_view`/span bound to a reference |
| C26459 | 0 | 0 | (no site in tree) |
| C26498 | 0 | 0 | (no site in tree) |

Inspection of every site (2026-07-21, re-verified against current symbols 2026-07-24) shows the
severity is lower than the rule names suggest. **No C26494 site is a genuine uninitialized read.**
All five are write-before-read: the two `XMFLOAT2 rectHull[4]` buffers in
`IslandChainPlacement.cpp` (in `PlaceAnchor` and `TryTouchPlace`) are output storage handed to
`LocalHull` and read only through that function's return; `pfPassTicks`, `pfX` and `pfY` in
`CurveWidget.cpp` (all inside the `CurveWidget` free function) are each fully populated by the loop
immediately following the declaration. The one unambiguously real finding is C26473 in
`IslandTerrain::WaitForElevationMaps` (`IslandTerrain.cpp:171`), where
`reinterpret_cast<const std::byte*>(rLazyChunk.pData)` casts a `std::byte*` (`LazyChunk::pData`,
`Engine/Source/File/FileManager.h:60`) to `const std::byte*` — the cast only adds `const` and can
be dropped. C26459 and C26498 have no sites at all, so their entries can be deleted outright.

## Design

Retire the allow list entry by entry, deleting each `<Rule>` element from the ruleset as its sites
are resolved. The ruleset comment already states this intent; this plan executes it.

1. **Free deletions.** Remove the `C26459` and `C26498` `<Rule>` entries. No code change — no site
   exists. Confirms the gate covers them going forward.
2. **C26473 — fix.** In `IslandTerrain::WaitForElevationMaps`, replace
   `reinterpret_cast<const std::byte*>(rLazyChunk.pData)` at `IslandTerrain.cpp:171` with the plain
   pointer (the implicit `std::byte*` → `const std::byte*` conversion), then remove the `C26473`
   entry from the ruleset.
3. **C26497 — decide once, then apply.** Nine sites are small free functions the analyzer suggests
   marking `constexpr` (functions enumerated under Critical files). Adding `constexpr` is
   behavior-preserving and compile-checked, but it is analyzer-driven churn across five files.
   **Unresolved:** apply `constexpr` and retire the code, or keep the entry with a recorded
   justification that the codebase does not adopt this guideline. Resolve before implementing; do
   not split the decision per call site.
4. **C26494 and C26445 — decide once, then apply.** Given the false-positive finding above, the
   choice is between writing initializers the code does not need (`= {}` on buffers the next
   statement fills) or a restructure at the `TweaksScreenBase::RunSliderAuditFrame` structured
   binding (`for (const auto& [rKey, pWrapper] : TweaksSliderMap::Get())`,
   `TweaksScreenBase.cpp:499`), versus keeping both entries with the write-before-read
   justification recorded in the ruleset comment. **Unresolved**; the repository's
   minimum-sufficient-change directive argues for keeping the suppression and recording why, but
   that leaves the codes invisible on genuinely new code.

Any entry retained must carry its justification in the ruleset comment so the next reader does not
re-derive this analysis. Any entry removed must be proven by a passing Release PREfast run.

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineAnalysis.ruleset` — the allow
  list (`<Rule>` entries and the explanatory comment block).
- `Engine/Source/Frame/IslandTerrain.cpp` — `IslandTerrain::WaitForElevationMaps`, chunk payload
  offset math (C26473 at line 171).
- `Engine/Source/Frame/IslandChainPlacement.cpp` — `PlaceAnchor` and `TryTouchPlace`, one
  `XMFLOAT2 rectHull[4]` output buffer each (C26494).
- `Engine/Source/Ui/CurveWidget.cpp` — `CurveWidget` free function: `pfPassTicks`, `pfX`, `pfY`
  local arrays (C26494). Client-only.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` —
  `TweaksScreenBase::RunSliderAuditFrame`, audit-loop structured binding (C26445). Client-only.
- C26497 candidate functions (the nine client/server occurrence sites across five files):
  `Engine/Source/Agent/AgentUiRegistry.cpp` (`LowerAscii`), `Engine/Source/Graphics/Graphics.cpp`
  (`SnapToDetailBlock`), `Engine/Source/Graphics/Screenshot.cpp` (`IsFourByteColor`, `IsBgra`,
  `IsSingleChannelNormalizable`), `Common/DataFile.h` (`IsCompressed`),
  `Engine/Source/Ui/HeightLerpWrapperQuartet.cpp` (`LerpAtHeight`).

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the
acceptance criteria and add no abstractions, configuration, refactors, or fixes to adjacent code
encountered along the way. Naming a file grants no permission to touch anything in it beyond the
named regions plus the mechanical necessities (declarations matching a `constexpr` definition) the
named change requires.

### In scope

- `BrokenEngineAnalysis.ruleset`: deleting `<Rule>` entries whose sites are resolved, and editing
  the comment block to record the justification for any retained entry.
- `IslandTerrain::WaitForElevationMaps`: only the single `reinterpret_cast` expression at
  `IslandTerrain.cpp:171`.
- Only if the step-3 decision is "apply `constexpr`": adding `constexpr` to the nine listed
  functions (`LowerAscii`, `SnapToDetailBlock`, `IsFourByteColor`, `IsBgra`,
  `IsSingleChannelNormalizable`, `IsCompressed`, `LerpAtHeight`) and their matching declarations
  (e.g. `HeightLerpWrapperQuartet.h:21`, the `IsBgra` forward declaration at
  `Screenshot.cpp:45`). No other edits to those functions' bodies.
- Only if the step-4 decision is "fix the sites": initializers on the five listed C26494 locals
  (`rectHull` in `PlaceAnchor` and `TryTouchPlace`; `pfPassTicks`, `pfX`, `pfY` in `CurveWidget`)
  and the minimal restructure of the `RunSliderAuditFrame` structured binding at
  `TweaksScreenBase.cpp:499`. No other edits to those functions.

### Out of scope

- Changing the gate mechanism, `CodeAnalysisTreatWarningsAsErrors`, or the `IncludeAll` rule surface.
- Enabling Clang-Tidy, or altering ordinary Debug/Profile/Release build behavior.
- Cleaning analysis codes that are not on the allow list, or any other diagnostics PREfast reports.
- Any behavior, simulation, or asset change.
- Any edit in the named files outside the named functions/regions.

## Risk tier

Tier 2 — scoped build/analysis-gate behavior with provably behavior-preserving code edits. Trigger
for escalation: `IslandChainPlacement.cpp` and `IslandTerrain.cpp` are on the deterministic
island-generation path (see Notes); any edit there that could alter evaluation rather than merely
add `const`ness, an initializer, or `constexpr` escalates the change to Tier 3 and must not land
without checking generated output.

## Acceptance criteria

- Every allow-list entry is either deleted, or retained with its justification recorded in the
  ruleset comment.
- Client and server Release PREfast builds return zero, with positive evidence analysis executed
  this run (fresh per-TU `*.nativecodeanalysis.xml` and `.lastcodeanalysissucceeded` written after
  link) rather than being skipped as up-to-date.
- A retired code demonstrably fails the build again if reintroduced; verify at least one retired
  code.
- Ordinary agent Release builds still return zero with `RunCodeAnalysis=false`.
- No unit tests added.

## Notes

- **Determinism / CRC:** `IslandChainPlacement.cpp` and `IslandTerrain.cpp` are on the deterministic
  island-generation path under `/fp:strict`. Every change here must be provably behavior-preserving —
  dropping a redundant cast and initializing an already-fully-written buffer both are, but a
  `constexpr` or restructure that alters evaluation must not land without checking generated output.
- **Client / server:** `CurveWidget.cpp` and `TweaksScreenBase.cpp` are client-only; the C26497 and
  C26473 sites compile into both targets, so both builds must be re-verified.
- **Wire / save / replay / `.pack` / shader:** none.
- **Build:** ruleset changes affect Visual Studio Release builds as well as the `/compile` skill's
  explicit PREfast mode, because both Release configurations set `RunCodeAnalysis=true` themselves.
- **Validation:** `/compile` for the Release PREfast verification runs.
