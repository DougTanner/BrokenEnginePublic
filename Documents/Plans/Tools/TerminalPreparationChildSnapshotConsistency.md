<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T19:42:10.312Z","dependsOn":[]} -->
# Terminal preparation must classify and render from the bytes it just read

## Context

In `Tools/WorktreeCli/PlanScheduler.cpp`, `RunPrepare`'s child reconciliation loop (`:1606-1639`) reads a child Plan's current bytes but decides from an older snapshot.

`BuildPlans` (`:406-436`, via `ParsePlan:395-404` and `ReadBytes:124-141`) populates each `Plan`'s `bytes`, `digest`, and `dependencies` by reading the working tree once, at `:1503`. The loop then probes the child's existence with a **fresh** `ReadBytes` at `:1621` — and discards the bytes it just read. Classification and rendering both use the earlier snapshot: `RenderDependencies(found->second, target, after)` at `:1629` tests `found->second.dependencies` for the target and copies `found->second.bytes` as the body.

If a Plan file changes on disk between the scan and the loop:

- A dependency edge **restored** in that window is not rewritten. The snapshot says the edge is already gone, `RenderDependencies` returns `false`, the child is skipped, and it keeps a `dependsOn` entry naming a target that is about to be deleted.
- A concurrent **body** edit is overwritten, because the rewrite emits the snapshot body verbatim.

**This is a latent inconsistency, not a reachable landed-history bug. State it that way; do not inflate it.**

- The window is the millisecond-scale interior of a guarded, no-wait transaction and requires an out-of-contract external write to a Plan file. The scheduler guard (`:1487-1496`) excludes concurrent *scheduler* operations, not an external filesystem writer.
- A compensating control defends the restored-edge half in depth. `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1:278-298` re-runs `prepare-completion` before any primary mutation; a skipped child is rewritten on that rerun, which makes `changedPaths` non-empty and trips `approval.refresh-required` at `:296-297`, blocking the landing. An adversarial review explicitly refuted this as an actionable safety violation for that reason, and no landed history shows the invariant violated.
- The overwritten-body half is not covered by that control — the rerun sees the marker already in its after-state and reports nothing — but the file is tracked, so the loss is visible in `git status`/`git diff` and in the pre-landing approval review before it can be committed.

The shape predates the recent change. The prior implementation also rendered from the snapshot; it merely had a whole-file digest comparison in front of it, which happened to conflict on any divergence. Removing that comparison (correctly — it refused legitimately reconciled bodies) left the snapshot read as the only classifier input.

**Scope interaction that deferred this.** The immediately-prior change's approved design (`Documents/Plans/Tools/TerminalManifestReconciliationTolerance.md`, Design section 1) explicitly required "no new parsing, no new file I/O, no new helper" for this loop, and listed `ParsePlanBytes`, `BuildPlans`, and the `Plan` struct as out of scope. Re-reading and re-parsing per child was therefore out of bounds there, and is exactly what this Plan authorizes.

## Design

In the child loop only, replace the snapshot record with a freshly parsed one.

Keep the tracked-membership check on the snapshot. `plans` comes from `git ls-files` (`BuildPlans:408`), so `plans.find(childPath) == plans.end()` at `:1614` is the only available test for "tracked executable Plan", and a fresh parse cannot answer it. Retain that lookup and its `child-invalid` conflict; drop the `!found->second.bValid` half of that condition, which the fresh parse now answers.

Replace the bare `ReadBytes` probe at `:1619-1624` with a fresh parse of the same path, and classify and render from it:

- Build a local `Plan` with `path = childPath` and `diskPath = worktree / childPath`, then call `ParsePlan`. `ParsePlan` performs the same `ReadBytes` the probe already performed, so this adds no extra file read.
- `ParsePlan` returns `false` → the child is absent or unreadable. Return the existing `recovery-conflict` / `"child plan is absent during terminal recovery"` with the same `plan` detail, exactly as `:1621-1624` does today.
- Parsed but `!bValid` → return the existing `child-invalid` conflict. This is the one intended behaviour change: a child whose on-disk marker is malformed or manual now conflicts instead of being silently overwritten from a stale snapshot. That is correct — the transaction owns only the marker, so it must not guess a `dependsOn` set the file no longer states.
- Otherwise pass the fresh record to `RenderDependencies` and keep both existing branches unchanged: `false` means the on-disk marker no longer lists the target (after-state; `continue`, pushing to `changed` only when `bRecoveringPreparation`), `true` means the rewrite is still pending (write with `coordination::WriteBytesAtomic`, push to `changed`).

Preserve the comment at `:1625-1627` explaining why a reconciled body must not conflict, updating it only if the fresh parse makes its wording inaccurate.

Everything outside the loop keeps using `plans`: the target lookups at `:1512`, `:1645`, the manifest construction at `:1524-1540`, and the recovery manifest expansion at `:1577-1594`. Those decide manifest membership, which is deliberately a scan-time property — the manifest records the set of children observed at preparation, and expanding it from a later snapshot would change which children the transaction owns, not merely how it renders them.

Rejected: refreshing the whole `plans` map before the loop. It moves the window rather than closing it, and a full `git ls-files` plus N reads is a wider window than a single per-child read.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp` — `RunPrepare` child reconciliation loop `:1606-1639` only; `ParsePlan:395-404` and `struct Plan:41-51` are read-only references (see Coordination for the one linkage consequence).
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — terminal-preparation block; add the coverage named in Acceptance criteria.
- `Tools/WorktreeCli/AGENTS.md` — only if the "Coordination State" sentence describing terminal-preparation classification needs the fresh-read property stated.

## Out of scope

- The manifest construction blocks at `:1510-1545` and `:1566-1605`, `ValidateManifest`, `RenderDependencies` itself, `ParsePlanBytes`, `BuildPlans`, and the `Plan` struct's fields.
- The terminal target's digest gate at `:1640-1666`. Deletion destroys the whole file, so a whole-file check there is deliberate and stays.
- Any locking, retry, or exclusion mechanism against external filesystem writers. This Plan closes a read-to-decide gap; it does not attempt to serialize the worktree against non-scheduler processes.
- Reordering terminal preparation relative to reconciliation, and any stale-baseline or primary-tip precondition. `RunPrepare` must not begin consulting the primary tip.
- `Invoke-FinalizeLanding.ps1`, the `approval.refresh-required` check, and the `recovery-conflict` landing route. The compensating control is unchanged and this Plan does not rely on removing it.
- Scheduler selection order, claim lifecycle, `claim-next`, `release-after-landing`, `dependsOn` semantics, the `broken-engine-plan/v1` marker format, and the claim record schema.
- Adding a unit test or a new test project.

## Risk tier

**Tier 3.** Trigger: changes `plan prepare-completion`, which every session's `/next-plan` completion and every `/finalize-changes` landing invokes, and which mutates Plan claim state — build/bootstrap coordination that can block other sessions.

Invariants that must hold:

- A completion never lands while any direct child's on-disk marker still lists the terminal target in `dependsOn`. The fresh parse tests that property against the bytes present at write time rather than at scan time, strictly strengthening it.
- A legitimately reconciled child body still never conflicts, and survives the rewrite verbatim.
- `changedPaths`, `manifestDigest`, `claimState`, conflict codes, exit codes (`0` ok, `2` conflict, `1` failure), and every command's stdout JSON schema are unchanged.
- Preparation still performs exactly one read per manifest child inside the loop; the fresh parse replaces the existing probe rather than adding to it.

No determinism/CRC, wire protocol, serialization, save/replay, threading, or allocation-tracked runtime exposure.

## Acceptance criteria

- With an `awaiting-landing` claim, restoring a manifest child's `dependsOn` edge to the terminal target on disk **after** the scan cannot be distinguished from restoring it before: re-running `prepare-completion` reapplies the marker rewrite and reports that child in `changedPaths`. Verified by the existing restored-edge case, which must pass unmodified.
- A body edit applied to a manifest child is preserved byte-for-byte by a subsequent `prepare-completion` rewrite that removes a restored edge — the rewritten file ends with the edited body, not the scan-time body. This is the fixture assertion that fails against the snapshot render and passes after the fix.
- A manifest child whose on-disk marker is malformed returns `child-invalid` rather than being overwritten.
- A manifest child deleted from disk still returns `recovery-conflict` with `"child plan is absent during terminal recovery"` and the `plan` detail naming it.
- `prepare-completion` on an unchanged `awaiting-landing` claim still returns `code: recovered` with an empty `changedPaths`; on a `preparing` claim it still reports the already-applied paths; the terminal target's third-party-bytes conflict is unchanged.
- All other `Test-WorktreeCliPlanScheduler.ps1` cases and `plan validate` behaviour are unchanged.

## Coordination

`Documents/Plans/Tools/ReducePlanScheduler.md` extracts `ParsePlanBytes`, `ParsePlan`, `BuildPlans`, `NormalizePlanPath`, `ReadBytes`, and `struct Plan` into a new `PlanMetadata.cpp`/`.h` pair while keeping every `Run*` handler, including `RunPrepare`, in `PlanScheduler.cpp`. Root cause and boundary differ (file size versus which bytes a decision reads), and this Plan is implementable in either order, so **no metadata dependency is declared**: an edge would block this Plan behind a large Tier-3 split it does not need.

One consequence must be recorded rather than discovered. That Plan's design keeps `ParsePlan` and `ParsePlanBytes` anonymous-namespace-internal to `PlanMetadata.cpp`, justified by "callers are only `ParsePlan`, `BuildPlans`, and `BuildPlansAtCommit`, all in this unit" — a premise this Plan falsifies by calling `ParsePlan` from `RunPrepare`. `ReadBytes`, `NormalizePlanPath`, `BuildPlans`, and `Plan` are already in its declared header set and need nothing. So whichever lands second promotes `ParsePlan` to a `PlanMetadata.h` declaration with `toolcli`-scope linkage — one declaration line and one linkage change, forced immediately by an undeclared-identifier compile error, adding no exported or public WorktreeCli surface. If this Plan lands first, `ReducePlanScheduler.md`'s acceptance criterion pinning `ParsePlan` internal must be read against the caller set present in the tree at implementation time.

`Documents/Plans/Tools/AtomicWritePlanFileVisibility.md` fixes the file attributes that `coordination::WriteBytesAtomic` leaves on the file this loop rewrites. Different boundary (ToolCommon attributes versus scheduler classification inputs), no ordering requirement; both edit the same terminal-preparation block of `Test-WorktreeCliPlanScheduler.ps1`, so whichever lands second re-cites fixture line numbers.

## Verification

1. Build WorktreeCli through `/compile`.
2. Run `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`; all pre-existing cases plus the new body-preservation and malformed-marker cases pass.
3. Run `.agents/skills/next-plan/scripts/Test-NextPlanWorkflowSidecars.ps1` and `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1`, which assert the `prepared`/`awaiting-landing` transition and the pre-landing preparation branch.
