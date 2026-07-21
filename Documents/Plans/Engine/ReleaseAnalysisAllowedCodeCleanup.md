# Retire the Release Analysis Allow List

## Context

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineAnalysis.ruleset` gates Release
Microsoft code analysis: `IncludeAll` minus an explicit per-code allow list, with
`CodeAnalysisTreatWarningsAsErrors=true` promoting everything else to a build error. The allow list
was seeded with the codes already present when the gate landed, so the gate could be introduced
without a cleanup pass. Each entry is a suppression: those codes are invisible now, not merely
non-fatal, including on newly written code.

Six codes are suppressed. Observed occurrences (client/server) at the time the gate landed:

| Code | Client | Server | Meaning |
|------|--------|--------|---------|
| C26497 | 7 | 2 | function could be `constexpr` |
| C26494 | 5 | 2 | variable is uninitialized |
| C26473 | 1 | 1 | cast between identical pointer types |
| C26445 | 1 | 0 | `string_view`/span bound to a reference |
| C26459 | 0 | 0 | (no site in tree) |
| C26498 | 0 | 0 | (no site in tree) |

Inspection of every site (2026-07-21, verified against current symbols) shows the severity is lower
than the rule names suggest. **No C26494 site is a genuine uninitialized read.** All five are
write-before-read: `rectHull[4]` in `IslandChainPlacement.cpp` is an output buffer handed to
`LocalHull` and read only through that function's return; `pfPassTicks`, `pfX` and `pfY` in
`CurveWidget.cpp` are each fully populated by the loop immediately following the declaration. The
one unambiguously real finding is C26473 at `IslandTerrain.cpp:172`, where
`reinterpret_cast<const std::byte*>(rLazyChunk.pData)` casts a `std::byte*`
(`FileManager.h:60`) to `const std::byte*` — the cast only adds `const` and can be dropped.
C26459 and C26498 have no sites at all, so their entries can be deleted outright.

## Design

Retire the allow list entry by entry, deleting each `<Rule>` from the ruleset as its sites are
resolved. The ruleset comment already states this intent; this plan executes it.

1. **Free deletions.** Remove the `C26459` and `C26498` entries. No code change — no site exists.
   Confirms the gate covers them going forward.
2. **C26473 — fix.** Drop the redundant `reinterpret_cast` at `IslandTerrain.cpp:172`, then remove
   the entry.
3. **C26497 — decide once, then apply.** Nine sites are small free functions the analyzer suggests
   marking `constexpr`. Adding `constexpr` is behavior-preserving and compile-checked, but it is
   analyzer-driven churn across five files. **Unresolved:** apply `constexpr` and retire the code,
   or keep the entry with a recorded justification that the codebase does not adopt this guideline.
   Resolve before implementing; do not split the decision per call site.
4. **C26494 and C26445 — decide once, then apply.** Given the false-positive finding above, the
   choice is between writing initializers the code does not need (`= {}` on buffers the next
   statement fills) or a restructure at the `TweaksScreenBase.cpp:448` structured binding, versus
   keeping both entries with the write-before-read justification recorded in the ruleset comment.
   **Unresolved**; the repository's minimum-sufficient-change directive argues for keeping the
   suppression and recording why, but that leaves the codes invisible on genuinely new code.

Any entry retained must carry its justification in the ruleset comment so the next reader does not
re-derive this analysis. Any entry removed must be proven by a passing Release PREfast run.

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineAnalysis.ruleset` — the allow list.
- `Engine/Source/Frame/IslandTerrain.cpp` — `LoadTemplate` heightmap offset math (C26473).
- `Engine/Source/Frame/IslandChainPlacement.cpp` — two `rectHull` output buffers (C26494).
- `Engine/Source/Ui/CurveWidget.cpp` — `pfPassTicks`, `pfX`, `pfY` (C26494). Client-only.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` — audit-loop structured binding (C26445). Client-only.
- `Engine/Source/Agent/AgentUiRegistry.cpp` (`LowerAscii`), `Engine/Source/Graphics/Graphics.cpp`
  (`SnapToDetailBlock`), `Engine/Source/Graphics/Screenshot.cpp` (`IsFourByteColor`, `IsBgra`,
  `IsSingleChannelNormalizable`), `Common/DataFile.h` (`IsCompressed`),
  `Engine/Source/Ui/HeightLerpWrapperQuartet.cpp` (`LerpAtHeight`) — C26497 candidates.

## Out of scope

- Changing the gate mechanism, `CodeAnalysisTreatWarningsAsErrors`, or the `IncludeAll` rule surface.
- Enabling Clang-Tidy, or altering ordinary Debug/Profile/Release build behavior.
- Cleaning analysis codes that are not on the allow list.
- Any behavior, simulation, or asset change.

## Acceptance criteria

- Every allow-list entry is either deleted, or retained with its justification recorded in the ruleset comment.
- Client and server Release PREfast builds return zero, with positive evidence analysis executed
  this run (fresh per-TU `*.nativecodeanalysis.xml` and `.lastcodeanalysissucceeded` written after link)
  rather than being skipped as up-to-date.
- A retired code demonstrably fails the build again if reintroduced; verify at least one retired code.
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
