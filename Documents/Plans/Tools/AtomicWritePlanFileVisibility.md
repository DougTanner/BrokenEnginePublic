<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T19:42:10.311Z","dependsOn":[]} -->
# Atomic Plan rewrites must leave tracked files visible

## Context

`plan prepare-completion` leaves every Plan file it rewrites Hidden+Temporary in the user's Git worktree.

`coordination::WriteBytesAtomic` (`Tools/ToolCommon/CoordinationStore.cpp:287-310`) creates its temporary sibling with `CreateFileW(..., FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, ...)` (`:293`), then `MoveFileExW`s it over the destination (`:303`). The rename carries the source's attributes onto the destination, so the destination inherits both bits.

The only caller that writes into the tracked worktree is `PlanScheduler.cpp:1634`, the direct-child dependency rewrite in `RunPrepare`. Every other caller reaches this helper through `WriteMetadataAtomic` (`CoordinationStore.cpp:282-285`) and writes a machine-local coordination record under `%LOCALAPPDATA%\BrokenEngineLocks` (`Tools/WorktreeCli/AGENTS.md`, "Coordination State"): `PlanScheduler.cpp:1344`, `:1541`, `:1600`, `:1670`, `:1902`, `:1914`, `:1986`; `LandingLockCommands.cpp:145`, `:170`, `:199`; `HarnessLockCommands.cpp:92`, `:194`, `:235`.

Two consequences, both on a tracked, user-facing Markdown document:

- **Hidden.** The Plan disappears from Explorer and from any listing that does not ask for hidden entries, after any `/next-plan` completion that rewrites a dependent Plan. It also breaks ordinary tooling that rewrites the file by truncate-open: `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` threw `Access to the path '...\Documents\Plans\Test\Dependent.md' is denied.` the first time a fixture attempted `[IO.File]::WriteAllText` on a scheduler-rewritten child, and now works around it with `[IO.File]::SetAttributes($childDiskPath, [IO.FileAttributes]::Normal)` at `:235` and `:244` (see the comment at `:234`). That workaround is the standing evidence for this Plan, and removing the need for it is the acceptance signal.
- **Temporary.** `FILE_ATTRIBUTE_TEMPORARY` asks the cache manager to avoid committing the contents to disk. That is wrong for a tracked source file the session is about to commit.

Git itself is unaffected; this is a user-visibility and durability defect, not a correctness one.

## Design

**Constraint that shapes the fix.** `WriteBytesAtomic` is shared, and hidden is deliberate for the machine-local records: `Test-WorktreeCliPlanScheduler.ps1:514-515` enumerates them with `-Force` under the comment "Claim records are written hidden." A blanket attribute change is therefore wrong. The hidden temporary sibling is also load-bearing while it exists: `RemovePlanAtomicTemporarySiblings` (`PlanScheduler.cpp:281-325`) only deletes orphans that are regular files, match the exact `<plan>.tmp.<pid>.<seq>` shape, **and** carry both `FILE_ATTRIBUTE_HIDDEN` and `FILE_ATTRIBUTE_TEMPORARY` (`:315`). Nothing may weaken that recognizability.

The split is clean along the existing helper boundary: every hidden write is a metadata-record write, and the single raw-bytes write is the one that must be visible.

**Fix.** In `Tools/ToolCommon/CoordinationStore.cpp`:

- `WriteBytesAtomic`: after a successful `MoveFileExW` (`:303`), `::SetFileAttributesW(targetPath.c_str(), FILE_ATTRIBUTE_NORMAL)`. Temp creation attributes at `:293` are unchanged, so the sibling stays recognizable for its whole crash-exposed lifetime, and the rename stays the atomic publish point. Clearing to `FILE_ATTRIBUTE_NORMAL` discards nothing a caller owned: the renamed file's attribute set is fully determined by this function's own `CreateFileW`.
- `WriteMetadataAtomic`: after `WriteBytesAtomic` succeeds, `::SetFileAttributesW` the destination back to `FILE_ATTRIBUTE_HIDDEN`, so every `%LOCALAPPDATA%` record keeps today's appearance. Hiding is cosmetic — enumeration of those records already uses `-Force` / `directory_iterator`, both of which see hidden entries — so a failed or crash-interrupted re-hide leaves a visible record and self-corrects on the next write. Do not fail the write on a `SetFileAttributesW` error in either function; the bytes are already published.

Then delete the two child-Plan workarounds in `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` (`:234-235` comment plus call, and `:244`) so the fixture proves the rewritten child is directly writable. The claim-record `SetAttributes` calls at `:189`, `:262`, and `:316` stay: those target `%LOCALAPPDATA%` records, which remain hidden by design.

**Rejected alternatives**, recorded because each is defensible until its failure mode is named:

- *Clear the temp file's attributes just before `MoveFileExW`.* Removes the post-rename window, but a crash in the sliver between the attribute clear and the rename leaves a `.tmp.<pid>.<seq>` orphan that `RemovePlanAtomicTemporarySiblings:315` refuses to delete, stranding an untracked file in the worktree permanently.
- *Clear attributes at the `PlanScheduler.cpp:1634` call site.* Smallest diff, but a crash or failure between the rename and the clear leaves the Plan hidden forever: on retry `RenderDependencies` returns `false` because the edge is already removed, so the child is skipped and never rewritten. No self-healing.
- *Add a visibility parameter or overload to `WriteBytesAtomic`.* Widens a shared ToolCommon signature to express a distinction the existing `WriteBytesAtomic`/`WriteMetadataAtomic` split already carries.

## Critical files

- `Tools/ToolCommon/CoordinationStore.cpp` — `WriteBytesAtomic:287-310` and `WriteMetadataAtomic:282-285`.
- `Tools/WorktreeCli/PlanScheduler.cpp` — `RemovePlanAtomicTemporarySiblings:281-325` (the recognizability contract to preserve) and the tracked-worktree call site at `:1634`; read-only reference, no edit expected.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — terminal-preparation block; remove the child-Plan `SetAttributes` workarounds, keep the claim-record ones.
- `Tools/WorktreeCli/AGENTS.md` — only if the "Coordination State" wording needs the visibility property stated.

## Out of scope

- The temp file's creation attributes at `CoordinationStore.cpp:293`, the `.tmp.<pid>.<seq>` filename shape, the rename-as-publish structure, and `RemovePlanAtomicTemporarySiblings` in any form.
- Making `%LOCALAPPDATA%` coordination records visible, or changing lock/claim storage layout, schema, lifetime, healing, or enumeration.
- The build lock's separate `OPEN_ALWAYS`/hidden-attribute path, completed by the prior build-command reliability change.
- Any change to `plan prepare-completion` classification, manifest contents, `changedPaths`, conflict codes, exit codes, or stdout JSON.
- Retrofitting attribute repair onto Plan files already left hidden by earlier runs. One `attrib -h -t` by the user clears those; a migration path is not warranted.
- Adding a unit test or a new test project. The existing integration fixture is the verification surface.

## Risk tier

**Tier 3.** Trigger: `Tools/ToolCommon/CoordinationStore.cpp` is compiled into both tool executables and backs every landing lock, harness lock, and scheduler claim write — build/bootstrap coordination that can block other sessions.

Invariants that must hold:

- The atomic-publish contract is unchanged: a reader sees either the previous complete bytes or the new complete bytes, never a partial file, and the rename remains the only publish step.
- A `.tmp.<pid>.<seq>` sibling is Hidden+Temporary for its entire existence, so `RemovePlanAtomicTemporarySiblings` still recognizes and deletes every orphan it recognizes today.
- Every `%LOCALAPPDATA%\BrokenEngineLocks` record written by `WriteMetadataAtomic` is hidden on success, exactly as today.
- Write success and failure return values, and every caller's error branch, are unchanged.

No determinism/CRC, wire protocol, serialization, save/replay, threading, or allocation-tracked runtime exposure.

## Acceptance criteria

- After `plan prepare-completion` rewrites a direct-dependency child, `GetFileAttributesW` on that child reports neither `FILE_ATTRIBUTE_HIDDEN` nor `FILE_ATTRIBUTE_TEMPORARY`, and the file is listed by `Get-ChildItem` without `-Force`.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` rewrites a scheduler-rewritten child with `[IO.File]::WriteAllText` and no preceding `SetAttributes`, and passes.
- Records under `%LOCALAPPDATA%\BrokenEngineLocks` written by any `WriteMetadataAtomic` caller are still hidden: the fixture's `-Force` claim enumeration at `:514-516` is still required, and the claim-record `SetAttributes` calls at `:189`, `:262`, `:316` remain necessary.
- A pre-seeded Hidden+Temporary `<plan>.tmp.123.1` orphan is still deleted by terminal preparation, and an unrelated untracked file in the same directory is still preserved — the existing assertions at `:222-228` pass unmodified.
- All other `Test-WorktreeCliPlanScheduler.ps1` cases, `plan validate` behaviour, exit codes (`0` ok, `2` conflict, `1` failure), and every command's stdout JSON are unchanged.

## Coordination

`Documents/Plans/Tools/TerminalPreparationChildSnapshotConsistency.md` edits the same terminal-preparation block of `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` and the `RunPrepare` child loop that calls `WriteBytesAtomic`. Root cause and boundary differ (file attributes in ToolCommon versus which bytes `RunPrepare` classifies from), and neither needs the other's change to compile or pass, so no metadata dependency is declared in either direction. Whichever lands second re-cites fixture line numbers only.

`Documents/Plans/Tools/ReducePlanScheduler.md` moves parse and graph helpers out of `PlanScheduler.cpp` and does not touch `CoordinationStore.cpp` or the `:1634` call site. Independent.

## Verification

1. Build WorktreeCli and AgentHarness through `/compile`; both consume the changed ToolCommon unit.
2. Run `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` with the two child-Plan workarounds removed.
3. Run `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1` and `.agents/skills/next-plan/scripts/Test-NextPlanWorkflowSidecars.ps1` — both exercise claim-record writes through `WriteMetadataAtomic`.
4. Inspect a rewritten child's attributes directly, and a live claim record's attributes, to prove the two paths diverged as designed.
