# Validate ownership-partitioned shared primary work

## Context

`plan order validate` is the deterministic authority for the Plans and Features queues. Its default primary-checkout path correctly fails closed when `git status --porcelain=v1` is non-empty (`Tools/AgentCli/PlanOrderCommands.cpp`, `RunValidate`) and its plan discovery reports every unindexed `.md` or `.txt` file other than `AGENTS.md` as an orphan (`ValidateFiles`).

That contract has no explicit safe path for a user-authorized workflow operating in a deliberately dirty shared primary checkout. Verification of `Agent/LinearRiskTieredCppChangeProcess.md` partitioned 27 session-owned paths from 133 concurrently preserved paths and bound the queue files by exact SHA-256/blob identities, but the authoritative command still exited 2 with `ok: false` solely for `dirty-primary` and two `orphan-plan` diagnostics. The orphan paths are the tracked canonical directory-memory stubs `Documents/Plans/CLAUDE.md` and `Documents/Features/CLAUDE.md`, each containing only `@AGENTS.md`; they are not executable plans or reference documents.

The result is a structural verification gap: a user-authorized shared-parent session cannot obtain deterministic queue PASS evidence even when its ownership partition and queue identities are explicit, while treating canonical memory stubs as plan-like documents creates false orphan failures. The default clean-primary requirement and AgentCli's sole queue authority must remain fail-closed.

## Design

1. Add an explicit opt-in AgentCli validation contract, or an equivalent wrapper-coordinated path, for a user-authorized shared primary checkout. The contract must bind the exact checkout, repository, caller/session identity, owned and concurrently preserved path sets, and queue-file identities strongly enough that AgentCli can prove every dirty path is accounted for without trusting free-form agent prose.
2. Keep ordinary `plan order validate` behavior unchanged: without the explicit authorization/ownership input, any dirty registered primary still reports `dirty-primary`; an in-progress or unverifiable Git operation remains a blocker in every mode.
3. Pre-stage the exact ownership representation and producer for **Approve and classify** and `/external-grill-plan`. Viable shapes include a wrapper-issued immutable ownership manifest or an AgentCli request bound to existing coordination state. Do not introduce a second lock/claim domain, accept an agent-authored waiver, or infer ownership from the working tree.
4. Make shared-primary validation reject incomplete, overlapping, stale, escaping, duplicate, or repository-mismatched ownership data. Queue and plan bytes that participate in validation must be bound to the inspected state; a mixed or changed queue snapshot cannot pass.
5. Refine plan discovery so the canonical `Documents/Plans/CLAUDE.md` and `Documents/Features/CLAUDE.md` directory-memory stubs are not executable orphans. The exclusion must recognize only the canonical stub identity/shape; a real plan-like document, including a `CLAUDE.md` with substantive plan content, must still be diagnosed unless it is a valid executable or reference document.
6. Update the workflow instructions that invoke validation so the opt-in path is used only with explicit user authority and the required ownership evidence. Do not relax `/next-plan` clean-primary selection, queue mutation locking, row claims, or final landing authority.
7. Extend the deterministic AgentCli PowerShell fixtures with default-clean, authorized-shared, hostile-ownership, changed-queue, canonical-stub, and substantive-`CLAUDE.md` cases. These are command/coordination acceptance scenarios, not unit tests.

## Critical files

- `Tools/AgentCli/PlanOrderCommands.cpp` — `RunValidate`, `ValidateFiles`, argument parsing, deterministic diagnostics, and validation receipts.
- `Tools/AgentCli/PlanOrderCommands.h`, `Tools/AgentCli/AgentCli.cpp`, and `Tools/AgentCli/AGENTS.md` — public command surface and fail-closed authority contract if the selected design changes them.
- `.agents/scripts/Test-AgentCliPlanOrder.ps1` — disposable repository/worktree acceptance scenarios for clean, shared, stale, hostile, and stub-discovery behavior.
- `.agents/skills/verify-changes/SKILL.md`, `.agents/skills/create-follow-up-plans/SKILL.md`, and other direct `plan order validate` consumers proven affected by the selected contract — explicit authorization/evidence handoff only.
- Wrapper/session coordination files proven necessary by the approved ownership design — authoritative manifest production and existing claim integration, without a parallel coordination domain.

## Out of scope

- Weakening or removing the default clean-primary, Git-operation, queue-lock, row-claim, dependency-graph, or landing gates.
- Allowing free-form waivers, hand validation, direct `Order.md` parsing/editing, or implicit trust in untracked files.
- Making shared-primary mode the default for wrapper-created isolated worktrees or `/next-plan` selection.
- General Markdown classification, arbitrary ignored-file patterns, engine/runtime changes, or unit tests.

## Acceptance criteria

- The existing unqualified primary validation scenario remains deterministic and reports `dirty-primary` for any tracked or untracked change.
- An explicitly authorized shared-primary fixture with disjoint complete ownership sets and exact queue identities returns exit 0, `ok: true`, and no diagnostics when all queue content is valid.
- Missing, overlapping, stale, escaping, repository-mismatched, or subsequently changed ownership/queue evidence returns exit 2 with stable diagnostics and no repository or coordination mutation.
- In-progress Git operations and unverifiable Git authority remain non-passing in both default and opt-in modes.
- Exact canonical `Documents/Plans/CLAUDE.md` and `Documents/Features/CLAUDE.md` `@AGENTS.md` stubs do not produce `orphan-plan`; ordinary unindexed `.md`/`.txt` documents and substantive plan-like `CLAUDE.md` content still do.
- `plan order validate` remains the sole executable-row parser and deterministic queue authority; neither skill prose nor a side script substitutes a PASS.
- AgentCli's Release/x64 build, command help/capability checks, deterministic plan-order fixtures, changed-skill validation, and `git diff --check` pass through the existing supported workflows.

## Notes

- Developer-workflow coordination only; no engine runtime, determinism/CRC simulation, wire, save/replay, `.pack`, shader, allocation-tracked, or client/server guard exposure.
- The ownership-contract shape is an architectural decision to resolve during **Approve and classify**. Preserve the existing global coordination domains and default clean-primary behavior.
- Score: Effort 4, Impact 4, Risks 4, total 4; Tier Architectural. This spans AgentCli validation, workflow coordination, and trust-boundary fixtures, with failure modes capable of weakening global queue authority.

