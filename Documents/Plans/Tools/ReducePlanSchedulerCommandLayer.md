<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-27T13:00:56.750Z","dependsOn":[]} -->
# Split WorktreeCli's plan-scheduler command layer

## Context

`Tools/WorktreeCli/PlanScheduler.cpp` is 2,069 lines, 88,915 bytes, and 22,229 `bt-token-v1` before its prerequisite. `.agents/scripts/Measure-Tokens.ps1` and the source map confirm the prerequisite moves the metadata/parser and dependency-graph core into `PlanMetadata.cpp`/`.h`; its projected retained `PlanScheduler.cpp` is 19,481 `bt-token-v1`, still above the 10,000 `.cpp` threshold. This Plan owns the remaining command layer only and therefore depends directionally on `Documents/Plans/Tools/ReducePlanScheduler.md`. The dependency becomes a valid satisfied stale edge after that terminal Plan is deleted.

The current command-layer map is: claim/receipt storage and Git/context helpers `:514-1137` (with shared utility helpers in `:64-326` and `:876-1137`); `RunValidate` through `RunUnclaim` `:1138-1484`; `RenderDependencies`, `RunPrepare`, `RunReleaseAfterLanding`, and `RunReparentClaims` `:1486-2024`; and argument parsing plus dispatch `:143-226, :2027-2068`. The raw measured blocks are 7,626, 4,961, 6,544, and 801 `bt-token-v1` respectively. The first number includes metadata types/functions which the prerequisite removes, so the final command-store projection below is intentionally lower.

## Design

After `ReducePlanScheduler.md` lands, leave its established `PlanMetadata.h` / `PlanMetadata.cpp` unit untouched. `PlanScheduler.cpp` retains only `ParseArguments` and `RunPlanSchedulerCommand` dispatch; it includes the new internal header and dispatches to moved handlers. `PlanScheduler.h` remains unchanged.

Create exactly these tool-internal files, all in `namespace toolcli` and all included only by WorktreeCli sources:

- `PlanSchedulerCommandShared.h` includes `PlanMetadata.h`, `ToolCliCommon.h`, `<cstdint>`, and `<map>`. It defines the existing `Arguments` and `Claim` structs verbatim plus `inline constexpr uint64_t kClaimLifetimeTicks = 48ull * 60ull * 60ull * 10'000'000ull`, so every command unit uses one type identity and one claim-lifetime value. It declares these existing signatures verbatim, and no function definitions: result helpers `PrintResult`, `Conflict`, `Failure`; shared utility/claim helpers `TrimLineEnding`, `IsLowerHex`, `JsonText`, `IsPathBelow`, `RemovePlanAtomicTemporarySiblings`, `SchedulerRoot`, `ClaimPath`, `ValidateClaim`, `ReadClaim`, `ClaimReceiptDigest`, `HealClaims`, `ReadReceipt`, `WriteClaimReceipt`; Git/context helpers `ResolveGitCommonDirectory`, `ResolveGitBranch`, `ResolvePrimaryBranch`, `ResolvePrimaryReference`, `ResolveCommit`, `ResolveRepositoryCommit`, `CommitIsAncestorOfWorktreeHead`, `ClaimIsLive`, `ClaimBaselineIsAncestorOfWorktreeHead`, `ClaimMatchesSession`, `HasTerminalReceiptFor`, `ValidateBaselineMetadata`, `ResolveContext`, and `ResolveReceiptContext`; receipt bridges `ReceiptMatchesClaim`, `ReadReceiptIdentity`, and `LocateReceiptClaim`; and seven `Run*` functions (`RunPrepare` covers both preparation operations). `ReadRequiredString`, `IsCanonicalPositiveDecimal`, `ValidateManifest`, `IsSafeReceiptDestination`, and all other helpers have callers only in the store and stay private there. `RenderDependencies`, `ResolveRebaseHeadBranch`, and `ReplaceReceiptBytes` have callers only in terminal commands and stay private there. `PlanMetadata.h` remains the sole declaration owner for `Plan`, metadata parsing, marker constants, path normalization, graph construction, and graph ordering.
- `PlanSchedulerClaimStore.cpp` includes `PlanSchedulerCommandShared.h`, `CoordinationStore.h`, `<algorithm>`, `<iostream>`, `<map>`, and `<set>`. It owns the current generic JSON/receipt/path/Git helpers (`TrimLineEnding`, `IsLowerHex`, `ReadRequiredString`, `JsonText`, `PrintResult`, `IsPathBelow`, `IsCanonicalPositiveDecimal`, `RemovePlanAtomicTemporarySiblings`, `ValidateManifest`, `ClaimReceiptDigest`, `HealClaims`, `ReadReceipt`, `IsSafeReceiptDestination`, all Git resolvers, and receipt/session checks); and the cross-unit definitions listed above through `ValidateBaselineMetadata`, `ResolveContext`, and `ResolveReceiptContext`. Helpers with no external caller remain anonymous-namespace private. It calls `PlanMetadata` only; neither `PlanMetadata` nor the command units call back into it except through the shared declared functions.
- `PlanSchedulerClaimCommands.cpp` includes `PlanSchedulerCommandShared.h`, `CoordinationStore.h`, `<algorithm>`, and `<map>`: its `coordination::Guard`, `EnsureParentDirectory`, `WriteMetadataAtomic`, and `HashSha256` calls require the first include, while candidate ordering needs `<algorithm>`. It owns `RunValidate`, `RunClaimNext`, `ReadReceiptIdentity`, `LocateReceiptClaim`, `RunClaimStatus`, and `RunUnclaim` verbatim. Its one-way calls are to `PlanMetadata` for plan maps/graph functions and to the declared claim-store helpers; it owns no shared data.
- `PlanSchedulerTerminalCommands.cpp` includes `PlanSchedulerCommandShared.h`, `CoordinationStore.h`, `<algorithm>`, `<map>`, and `<set>`: its `coordination::Guard`, `EnsureParentDirectory`, `WriteMetadataAtomic`, `WriteBytesAtomic`, and `HashSha256` calls require the first include, while dependency scans and manifest/reparent collections require the standard headers. It owns `RenderDependencies`, `RunPrepare`, `RunReleaseAfterLanding`, `ResolveRebaseHeadBranch`, `ReplaceReceiptBytes`, and `RunReparentClaims` verbatim. Its one-way calls are to `PlanMetadata` and declared claim-store helpers; `ResolveRebaseHeadBranch` and `ReplaceReceiptBytes` stay anonymous-namespace private. `RunPrepare` and `RunReleaseAfterLanding` are the existing bodies that overlap the coordinated behavior-fix Plans.

No command unit includes another command `.cpp`; the only command-layer connection is `PlanScheduler.cpp` / command units -> `PlanSchedulerCommandShared.h` -> `PlanMetadata.h` and ToolCommon. Each command implementation directly includes `CoordinationStore.h` when it uses its `coordination::` API; each directly includes the standard headers named above for its own algorithms and containers. `PlanScheduler.cpp` drops the no-longer-needed `CoordinationStore.h`, `<algorithm>`, `<fstream>`, `<functional>`, `<iostream>`, `<map>`, and `<set>` includes after moved bodies prove them unused. Every new `.cpp` and the new header belongs only to the existing `WorktreeCli` filter, with one matching `ClCompile`/`ClInclude` entry per file in `WorktreeCli.vcxproj` and `.filters`; no ToolCommon or other project membership changes.

Implement in this buildable order: (1) add the shared header with the two exact structs and declarations, and include it from the existing file; (2) move claim-store definitions, leave forwarding declarations compiling, and add its project/filter entries; (3) move claim command definitions and entries; (4) move terminal definitions and entries; (5) reduce `PlanScheduler.cpp` to parser and dispatch, remove stale includes, then run `/update-vcxproj`. Move bodies without behavior changes.

## Expected sizes

| Unit | Expected `bt-token-v1` | Basis |
| --- | ---: | --- |
| `PlanScheduler.cpp` | ~900 | current parser/dispatch raw block is 801; shared-header include and retained file framing |
| `PlanMetadata.cpp` | ~2,300 | prerequisite projection 2,282; untouched by this Plan |
| `PlanSchedulerClaimStore.cpp` | ~7,400 | current candidate ranges 7,626; prerequisite-owned marker/`Plan` declarations leave this unit before the new header boundary |
| `PlanSchedulerClaimCommands.cpp` | ~5,000 | current `:1138-1484` block 4,961 plus boundary includes |
| `PlanSchedulerTerminalCommands.cpp` | ~6,600 | current `:1486-2024` block 6,544 plus boundary includes |
| `PlanSchedulerCommandShared.h` | ~1,100 | two existing structs and declarations only |

Every implementation file is below 10,000. The projected `.cpp` total is ~22,200 versus the pre-prerequisite 22,229 (within 2%); the separate ~1,100-token header is a required declaration boundary, not duplicated implementation.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp` — retain only argument parsing and public command dispatch after the prerequisite's metadata split.
- `Tools/WorktreeCli/PlanMetadata.h` and `PlanMetadata.cpp` — established prerequisite-owned metadata/graph boundary; read-only in this Plan.
- New `Tools/WorktreeCli/PlanSchedulerCommandShared.h`, `PlanSchedulerClaimStore.cpp`, `PlanSchedulerClaimCommands.cpp`, and `PlanSchedulerTerminalCommands.cpp` — exact command-layer ownership above.
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj` and `.vcxproj.filters` — only the four new-file membership/filter pairs.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — read-only scheduler contract fixture.

## Out of scope

- Changes to scheduler behavior, command schemas, exit codes, claims, receipts, plan metadata, selection order, dependency handling, healing, landing, or reparent semantics.
- Any change to `PlanMetadata.h`, `PlanMetadata.cpp`, or `PlanScheduler.h`; any additional public WorktreeCli API; a new ToolCommon abstraction; or a command-unit-to-command-unit include.
- Fixing defects in individual command handlers, including the overlapping terminal-preparation and terminal-release behavior fixes; preserve their behavior while moving bodies.
- New scheduler capability, unit tests, build/bootstrap policy, or changes outside the command-layer split.

## Risk tier and invariants

**Tier 3.** This shared scheduler coordination binary gates Plan claims and landings; the split adds translation units and project membership. Preserve byte-level stdout JSON, stderr placement, exit codes, metadata/dependency semantics, claim/receipt identity and healing, terminal-state proof, reparent behavior, atomic-write behavior, PCH-less ToolCommon include ownership, and one identity for every cross-unit type. Add no public WorktreeCli surface.

## Acceptance criteria

- `Measure-Tokens.ps1` records every listed implementation/header size; every `.cpp` is at or below 10,000 `bt-token-v1`, and the documented aggregate exception is confirmed against the same pre-split tree.
- The shared header has only the two data structs and declarations enumerated in Design; private helpers stay in their owning anonymous namespace; `PlanScheduler.h` and the established `PlanMetadata` unit are unchanged.
- `/update-vcxproj` confirms exactly one WorktreeCli compile/filter pair for each of the three new `.cpp` files and one include/filter pair for the new shared header, with no membership in another project.
- The PCH-less WorktreeCli project compiles. Existing scheduler fixtures and live checks prove byte-identical JSON and exit codes for `validate`, `claim-next`, `claim-status`, `unclaim`, `prepare-completion`, `prepare-rejection`, `release-after-landing`, and `reparent-claims`.

## Coordination

`Documents/Plans/Tools/TerminalPreparationChildSnapshotConsistency.md` changes the body moved as `RunPrepare`; `Documents/Plans/Tools/TerminalReleaseConflictAttribution.md` changes the body moved as `RunReleaseAfterLanding`. These are nondirectional same-body overlaps, not prerequisites: neither receives metadata dependency. Whichever Plan lands second reconciles its intended behavior into the moved body, re-cites symbols/fixture lines, and preserves the other Plan's behavior. Reciprocal entries in both Plans record the same obligation.

## Notes

- The dependency is directional only: the metadata/graph extraction must establish `PlanMetadata` before this command boundary can be applied. Its later absence is the scheduler's accepted stale-edge satisfaction, not a reason to omit the edge.
