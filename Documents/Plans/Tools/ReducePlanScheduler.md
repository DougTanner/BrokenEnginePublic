<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-24T02:37:56.000Z","dependsOn":[]} -->
# Extract the plan-metadata parse and dependency-graph core from PlanScheduler.cpp

## Context

`Tools/WorktreeCli/PlanScheduler.cpp` is a single 2,045-line translation unit measured at **21,878 `bt-token-v1`** (deterministic measurement via `.agents/scripts/Measure-Tokens.ps1`) — roughly twice the 10,000-token `.cpp` threshold. The size observation was deferred per the `/reduce-file` convention (report the observation, defer planning); this Plan is the first of two split increments. The unit is behavior-preserving structural debt, not an acceptance failure.

Every scheduler symbol lives in `namespace toolcli`'s anonymous namespace. This increment extracts the one sub-family that depends on nothing else in the file: plan-metadata parsing and the dependency graph — `NormalizePlanPath`, `ParsePlanBytes`, `ParsePlan`, `BuildPlans`, `BuildPlansAtCommit`, `IsBlockedByDependencies`, `MarkCycles` — plus the marker constants and the three leaf helpers it shares with the retained code (`Utf8PathLess`, `ParseCanonicalUtcTimestamp`, `ReadBytes`). Its outbound calls all land in ToolCommon (`RunGit`, `Utf8ToWide`, `WideToUtf8`, `ExtendedLengthPath`, `coordination::HashSha256`), so the new unit is acyclic: `PlanScheduler.cpp` calls into it and it never calls back.

`ValidateBaselineMetadata` stays put. It is metadata validation in name only — it calls `ResolvePrimaryReference`, `ResolveRepositoryCommit`, `CommitIsAncestorOfWorktreeHead`, and `HasTerminalReceiptFor`, all members of the claim/receipt and Git-resolution block. Moving it would make the extracted header advertise four functions it does not own and make the two units mutually dependent. It moves in the follow-up increment, with its dependencies.

**Threshold status — this increment does not bring the file under threshold.** Measured projection of the split:

| Unit | `bt-token-v1` |
| --- | --- |
| `PlanScheduler.cpp` (after) | 19,481 |
| `PlanMetadata.cpp` (new) | 2,282 |
| `PlanMetadata.h` (new) | 304 |
| Sum | 22,067 (pre-split file: 21,878) |

Reaching 10,000 requires moving ~11,900 `bt-token-v1`; the entire extractable parse/graph family is worth 2,397. The rest of the file is the claim/receipt coordination block, the Git resolvers, and the `Run*` command handlers (measured in Notes) — 11,538 of it in the command layer alone, which has no cohesive boundary under the threshold and would have to be chopped by size across two units. That is a far larger Tier-3 surface on the binary that gates every session's plan claims and landings, and it collides directionally with three in-flight Plans that own regions inside it. So this Plan claims a measured reduction, leaves `PlanScheduler.cpp` deliberately oversized, and hands the threshold to a follow-up Plan authored as part of this work.

## Risk tier and invariants

**Tier 3.** Triggers: this is the shared plan-scheduler coordination binary that gates every session's plan claims and landings (build/bootstrap coordination that can block other sessions), and the change adds a translation unit (project membership). Invariants to hold:

- Every scheduler operation's stdout JSON schema, stderr placement, and exit code are byte-identical across the split; moved declarations and definitions are verbatim.
- Plan selection order, metadata/marker parsing rules, dependency-cycle handling, and claim/receipt validation and healing are unchanged.
- One type identity across both translation units for any type that crosses the new boundary; no new exported or public WorktreeCli surface, and single-TU functions keep anonymous-namespace internal linkage.
- PCH-less include ownership (ToolCommon shared-consumption rules) is preserved.

The split preserves behavior but carries repo-wide blast radius on regression, so `/update-vcxproj` reconciliation and a fixture-backed scheduler run are required.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete split that meets the acceptance criteria, and add no abstractions, configuration, refactors, renames, behavior changes, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named regions plus the mechanical necessities (includes, declarations, the anonymous-namespace-to-header move of the one shared type) the split requires — nothing else.

**In scope** — only these regions:

- `Tools/WorktreeCli/PlanScheduler.cpp`: delete the moved regions listed in Design, add `#include "PlanMetadata.h"`, and drop the now-unused `<fstream>` and `<functional>` includes. Everything else — `Arguments`, `Claim`, the forward declarations, the retained helpers, the claim/receipt coordination block, the Git resolvers, `ValidateBaselineMetadata`, `ParseArguments`, the `Run*` handlers, and `RunPlanSchedulerCommand` — stays byte-identical.
- New `Tools/WorktreeCli/PlanMetadata.h` and `Tools/WorktreeCli/PlanMetadata.cpp`: exactly two new files, contents pinned in Design.
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj` and `.vcxproj.filters`: one new `ClCompile` and one new `ClInclude`, reconciled via `/update-vcxproj`.
- New `Documents/Plans/Tools/ReducePlanSchedulerCommandLayer.md` via `/create-follow-up-plans`, carrying the residual split of the claim/receipt, resolver, and command-handler blocks with the measured budgets from Notes. Metadata `dependsOn` is `[]`: this Plan is deleted at completion, so a dependency edge on it would land as a stale-edge notice, and the extracted boundary is already in the tree before the follow-up becomes claimable.

**Out of scope:**

- Bringing `PlanScheduler.cpp` to or below the 10,000 `bt-token-v1` threshold. That needs the command layer split and belongs to the follow-up Plan.
- Moving `ValidateBaselineMetadata`, the claim/receipt coordination helpers, the Git/commit resolvers, `ResolveContext`/`ResolveReceiptContext`, `ParseArguments`, the `Run*` handlers, or `RunPlanSchedulerCommand`.
- Any change to plan selection, metadata/marker parsing rules, dependency-cycle handling, claim/receipt validation, healing, landing, reparent behavior, storage layout, command stdout/stderr schemas, or exit codes. Move declarations and definitions verbatim.
- Merging with, or ordering against, the CLI-parser decision (`Architecture_LibraryReplacement.md`, an investigation that edits no source; a resulting adoption Plan would rewrite `ParseArguments`) or the include cleanup (`Architecture_IncludeDependencies.md`).
- Any per-function token budget; this Plan targets only the file-level split.
- Editing `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — it is a read-only acceptance fixture, not an edit target.

## Design

One header and one implementation file, both `namespace toolcli`, both tool-internal. Pre-split line numbers below.

**`PlanMetadata.h`** — `#pragma once`, `#include "ToolCliCommon.h"`, `#include <map>`; everything else resolves through `ToolCliCommon.h` exactly as `PlanScheduler.cpp` already relies on it. Declares, at `toolcli` scope, exactly:

- `kMarkerPrefix`, `kMarkerSuffix` (17-18) — `constexpr std::string_view`, still consumed by the retained `RunPrepare` (1472) and `RunReleaseAfterLanding` (1749).
- `struct Plan` (41-51) — the only type crossing the boundary, verbatim. `Arguments` and `Claim` do **not** move: no moved symbol names them, so both keep their anonymous-namespace home in `PlanScheduler.cpp`.
- Eight function declarations, defined in `PlanMetadata.cpp` and called from `PlanScheduler.cpp`: `Utf8PathLess`, `ParseCanonicalUtcTimestamp`, `ReadBytes`, `NormalizePlanPath`, `BuildPlans`, `BuildPlansAtCommit`, `IsBlockedByDependencies`, `MarkCycles`. These lose internal linkage because they are genuinely two-TU; they gain `toolcli`-scope linkage only, are absent from `PlanScheduler.h`, and add no exported surface.

**`PlanMetadata.cpp`** — `#include "PlanMetadata.h"`, `#include "CoordinationStore.h"`, `#include <algorithm>`, `<fstream>`, `<functional>`. Holds, verbatim:

- Anonymous namespace: `ParsePlanBytes` (327-393) and `ParsePlan` (395-404). Callers are only `ParsePlan`, `BuildPlans`, and `BuildPlansAtCommit`, all in this unit, so both stay internal-linkage.
- `toolcli` scope: `Utf8PathLess` (69-72), `ParseCanonicalUtcTimestamp` (82-85), `ReadBytes` (124-141), `NormalizePlanPath` (251-271), `BuildPlans` (406-436), `BuildPlansAtCommit` (438-482), `IsBlockedByDependencies` (777-788), `MarkCycles` (790-844). Bodies unchanged; indentation drops one tab level with the anonymous namespace.

**Cross-boundary edges, all one-directional.** `PlanScheduler.cpp` -> `PlanMetadata`: `NormalizePlanPath`, `BuildPlans`, `BuildPlansAtCommit`, `IsBlockedByDependencies`, `MarkCycles`, `Utf8PathLess`, `ParseCanonicalUtcTimestamp`, `ReadBytes`, `Plan`, and the marker constants. `PlanMetadata` -> `PlanScheduler.cpp`: none. `PlanMetadata` -> ToolCommon only otherwise.

**Retained leaf helpers.** `TrimLineEnding` (64-68) stays: its callers are the Git resolvers (853-920) and `ResolveRebaseHeadBranch` (1789, 1795), none of which move. `IsLowerHex`, `ReadRequiredString`, `JsonText`, `PrintResult`, `Conflict`, `Failure`, `IsPathBelow`, `IsCanonicalPositiveDecimal`, and `RemovePlanAtomicTemporarySiblings` have no caller in the moved set and stay anonymous in `PlanScheduler.cpp`.

Reconcile Visual Studio membership and filters with `/update-vcxproj`, then run the PCH-less WorktreeCli tool compile and the fixture-backed scheduler verification.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp` — retains `Arguments`, `Claim`, claim/receipt coordination, Git resolvers, `ValidateBaselineMetadata`, `ParseArguments`, and the `Run*` command dispatch.
- New `Tools/WorktreeCli/PlanMetadata.h` / `PlanMetadata.cpp` — the plan-metadata parse and dependency-graph core plus the three shared leaf helpers.
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj` and `.vcxproj.filters` — new-file tool-project membership, reconciled via `/update-vcxproj`.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — read-only scheduler behavior fixtures used for acceptance verification.
- New `Documents/Plans/Tools/ReducePlanSchedulerCommandLayer.md` — follow-up increment carrying the threshold.

## Acceptance criteria

- `Measure-Tokens.ps1` reports `PlanScheduler.cpp` at least 2,300 `bt-token-v1` below its pre-split measurement in the same tree (projection 21,878 -> 19,481), `PlanMetadata.cpp` and `PlanMetadata.h` each at or below the 10,000 `.cpp` threshold (projection 2,282 and 304), and the three units summing no more than 2% above the pre-split total (projection 22,067) — proving relocation, not rewrite. `PlanScheduler.cpp` stays above the threshold; that is this Plan's stated non-goal, not a failure.
- `PlanMetadata.h` declares exactly the symbols listed in Design and nothing else; `ParsePlanBytes` and `ParsePlan` are anonymous-namespace-internal to `PlanMetadata.cpp`; `Arguments` and `Claim` remain anonymous in `PlanScheduler.cpp`; `PlanScheduler.h` is unchanged; no new exported or public WorktreeCli surface.
- WorktreeCli (tool project) compiles PCH-less after membership reconciliation, with exactly one new `ClCompile` and one new `ClInclude` in `WorktreeCli.vcxproj`, matching mirrored `.vcxproj.filters` entries, and no entries for any other project.
- Scheduler behavior is byte-identical: `validate`, `claim-next`, `claim-status`, `unclaim`, `prepare-completion`, `prepare-rejection`, `release-after-landing`, and `reparent-claims` produce unchanged JSON and exit codes, verified against the existing `Test-WorktreeCliPlanScheduler.ps1` fixtures and equivalent live command checks.
- `Documents/Plans/Tools/ReducePlanSchedulerCommandLayer.md` exists with valid byte-zero metadata and `plan validate` reports no diagnostics for it.

## Notes

- Measured residual blocks for the follow-up Plan (pre-split line numbers, `bt-token-v1`): claim/receipt coordination, Git resolvers, `ValidateBaselineMetadata`, and the context resolvers (484-1107) 5,787; claim-lifecycle handlers `RunValidate` through `RenderDependencies` (1108-1475) 5,194; terminal handlers `RunPrepare` through `RunReparentClaims` (1476-2002) 6,344. With the ~2,156 of includes, shared structs, small helpers, `ParseArguments`, and dispatch left over, that is 19,481 — so the follow-up needs at least a two-unit split of the command layer to reach 10,000.
- Same-file, non-directional overlaps (line-drift only, whoever lands second re-cites); no directional ordering required, so metadata `dependsOn` stays empty: `Architecture_LibraryReplacement.md` (investigation only — edits no source; a resulting adoption Plan would rewrite `ParseArguments`, which stays here), `Architecture_IncludeDependencies.md` (removes an include — this Plan also edits the include block, still line-drift only). No existing Plan owns this file-level split.
- Structural precedent: `ReducePipelineManager.md`, `ReduceWaterFragmentShader.md`.
