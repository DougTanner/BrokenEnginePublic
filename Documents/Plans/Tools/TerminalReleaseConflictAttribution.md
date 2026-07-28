<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T21:35:15.878Z","dependsOn":[]} -->
# Terminal release must name what actually blocked it

## Context

In `Tools/WorktreeCli/PlanScheduler.cpp`, `RunReleaseAfterLanding`'s terminal-state proof (`:1758-1779`) clears one flag from two structurally different findings and then reports only one of them.

- `:1764` seeds `bTerminalStateVerified` from the target's absence at the actual primary tip.
- `:1767-1770` clears it when a **valid** plan still lists the target in `dependsOn` — a surviving direct dependency edge.
- `:1771-1774` clears it when a plan is **invalid** while its bytes still start with the marker prefix — a document that claims to be a plan but whose `dependsOn` cannot be read, so an edge on the target cannot be excluded.
- `:1776-1779` returns a single conflict: code `terminal-state-not-proven`, message `"actual primary Plans tree still contains target or a direct dependency edge"`.

That message describes the first two findings only. When the third fires, the operator is told the target still exists or still has a dependency edge and is sent to inspect the wrong file, while the blocking document is some unrelated malformed Plan elsewhere in the tree. The refusal itself is correct and fails closed — an unreadable marker cannot prove the absence of an edge — and one such document blocks **every** receipt-bound terminal release at that tip until it is fixed, so the misattribution is paid by every landing session, not just the one that introduced it.

Commit `bec58eb0` widened the class of documents that can trip that branch. `ClassifyDirectoryGuidance` (`:416-421`) now forces `bValid = false` for every `AGENTS.md`/`CLAUDE.md` under `Documents/Plans` regardless of its bytes, so such a file carrying a marker prefix — which `Documents/Plans/AGENTS.md` forbids but nothing mechanically prevents — satisfies `:1771` and blocks release even though it can never be a plan or carry a dependency edge. `plan validate` deliberately stays silent about guidance documents (`ReportInvalidMetadata:425-430`), so it offers the operator no corroborating diagnostic either.

The evidence needed for correct attribution is already computed and thrown away: `BuildPlansAtCommit` at `:1759-1760` fills a `diagnostics` array with exactly the `{ plan, code: "invalid-metadata", message }` entries for these documents, and nothing reads it afterwards.

Originating gap: outside the approved boundary of the change that introduced the guidance classification (the marker-less-plan-document rule, its two reporting sites, the guidance exemption, the six document dispositions, and the planning documentation). It is not an acceptance failure of that change — the refusal behaves correctly; only the diagnosis is wrong.

## Design

Keep the refusal condition and the `terminal-state-not-proven` code exactly as they are. This Plan changes what the conflict *says* and *carries*, never which inputs refuse.

- In the `:1765-1775` loop, record the blocking path and its cause instead of only setting the flag, and record the target-present finding from `:1764` the same way. Causes: `target-present`, `dependency-edge`, `invalid-metadata`. Do not stop at the first finding — the operator gets the complete set, and the loop already visits every plan.
- Return them through the existing `extra` parameter of `Conflict` (`:108-116`). Precedent for a path-bearing conflict payload is the `recovery-conflict` `plan` field, asserted by `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1:287`.
- Reuse the established vocabulary rather than inventing one: emit a `blockers` array of `{ plan, code, message }` objects, the same shape `claim-next` already emits at `:1385-1387`. The `invalid-metadata` entries are the `diagnostics` entries already produced at `:1760` — surface those verbatim instead of recomputing or reformatting them.
- Rewrite the message so it is true for every cause and asserts none of them individually — it points at `blockers` rather than naming the target.
- No new conflict code, no change to exit codes, no change to which conditions refuse, and no new file or Git read.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp` — `RunReleaseAfterLanding` `:1758-1779` only. `Conflict:108-116`, `BuildPlansAtCommit:465`, and `ClassifyDirectoryGuidance:416-421` are read-only references.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` — read-only; `Complete-LandedState:214-230` is the sole consumer of this command's JSON. It stores the whole object at `$result.planClaim.release` and reads only `released`, `alreadyReleased`, `terminalStateVerified`, `code`, and the exit code, so additive conflict fields reach the landing result without a consumer change. Confirm this still holds before implementing.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` — the release-after-landing block at `:299-306`; keep the existing case and add the attribution coverage named in Acceptance criteria.
- `Tools/WorktreeCli/AGENTS.md` — only if the "Coordination State" sentence describing receipt-bound release needs the attribution property stated.

## Out of scope

- The refusal condition itself. The target-absence seed, the dependency test, and the invalid-plus-marker test keep identical semantics; a release that refuses today must still refuse, and a release that succeeds today must still succeed.
- The `terminal-state-not-proven` code string, exit codes (`0` ok, `2` conflict, `1` failure), and the `released` / `already-released` payloads.
- Whether a marker-carrying `AGENTS.md`/`CLAUDE.md` under `Documents/Plans` should block release at all, and whether `plan validate` should report guidance documents. Both are separate questions; until one is decided, release keeps failing closed on any unreadable marker.
- Everything before the terminal-state proof (`:1708-1757`): receipt identity, landed-commit resolution, ancestry checks, and the guard.
- `prepare-completion`, `claim-next`, `unclaim`, `reparent-claims`, selection order, claim lifecycle, the `broken-engine-plan/v1` marker format, and the claim record schema.
- Adding a unit test or a new test project.

## Risk tier

Tier 3. Trigger: modifies `plan release-after-landing`, the coordination command every wrapper session's `/finalize-changes` invokes to release a terminal claim — build/bootstrap coordination that can block other sessions.

Invariants that must hold:

- Exactly the same inputs refuse and exactly the same inputs release. The added attribution is observation of state the loop already computes.
- Release still fails closed when any marker-carrying document at the primary tip cannot be parsed.
- Conflict code, exit codes, and every other command's stdout JSON are unchanged; the only schema movement is additive fields on this one conflict.
- The primary tip is still read exactly once, through the existing `BuildPlansAtCommit` call.

No determinism/CRC, wire protocol, serialization, save/replay, threading, allocation-tracked runtime, or shader exposure.

## Acceptance criteria

- With the terminal target reintroduced at the primary tip, `release-after-landing` still exits `2` with `terminal-state-not-proven` — the existing fixture case at `:300-301` passes unmodified — and the payload names that path with cause `target-present`.
- With a document at the primary tip whose bytes start with the marker prefix but whose metadata is malformed, the same code is returned and the payload names **that** path with cause `invalid-metadata`; the message asserts no single cause and makes no dependency-edge claim.
- With a surviving direct dependency edge on the target, the payload names the citing plan with cause `dependency-edge`.
- When several findings hold at once, every blocking path appears.
- A clean terminal state still releases: `code: released` with `released` and `terminalStateVerified` true, and the retry still returns `already-released` with the same fields.
- All other `Test-WorktreeCliPlanScheduler.ps1` cases and `plan validate` behaviour are unchanged.

## Coordination

`Documents/Plans/Tools/ReducePlanSchedulerCommandLayer.md` moves the body this Plan changes, `RunReleaseAfterLanding`, into `PlanSchedulerTerminalCommands.cpp`. This is a nondirectional same-body overlap, not a prerequisite: neither Plan declares a metadata dependency. Whichever lands second reconciles this Plan's blocker-attribution behavior into the moved body, re-cites symbols and fixture lines, and preserves the command-layer split.

`Documents/Plans/Tools/ReducePlanScheduler.md` splits the metadata parse and dependency-graph core out of `PlanScheduler.cpp` while keeping every `Run*` handler — including `RunReleaseAfterLanding` — in that file. Different boundary (file size versus conflict attribution), no ordering requirement, no metadata edge; whichever lands second re-cites line numbers.

`Documents/Plans/Tools/TerminalPreparationChildSnapshotConsistency.md` and `Documents/Plans/Tools/AtomicWritePlanFileVisibility.md` both work in `RunPrepare` and in the terminal-preparation block of `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`. This Plan touches neither `RunPrepare` nor that fixture block — its coverage goes in the release block at `:299-306` — so there is no ordering requirement; only fixture line numbers move.

## Verification

1. Build WorktreeCli through `/compile`.
2. Run `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`; all pre-existing cases plus the new attribution cases pass.
3. Run `.agents/skills/finalize-changes/scripts/Test-FinalizePreflight.ps1`, which asserts the documented `plan release-after-landing` invocation and capability contract (`:317-318`) — proof the command surface did not move.
