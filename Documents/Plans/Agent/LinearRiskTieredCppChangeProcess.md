# Make C++ change execution linear and risk-tiered

## Context

The previous root C++ Code Change Process applied twelve numbered stages to every C++ change whose triggers matched. It required paired correctness/adversarial review and two fresh session audits per logical file group. The three audited sessions landed correct changes, but produced 21, 51, and 48 Markdown reports for commits `d730205a`, `98b5272c`, and `90f7ed34`. The captured PREfast corpus contains 51 reports totaling 328170 bytes, bound by source-manifest SHA-256 `543e943a887335cafaaf75dfc573d04f544483ad49f7b254c7aa1a69a9508597`. DataPacker alone ran six final session audits; the save session accumulated roughly fifteen session-audit reports, many rechecking queue prose rather than changed runtime behavior.

That process lacked a convergence rule. Reviewer disagreement could cause another review round even when both reports contained usable evidence, and an adversarial prompt could optimize for finding a rejection instead of reaching an evidence-backed decision. Verification similarly grew from acceptance coverage into exploratory edge-case work: the PREfast session ran game runtime checks unrelated to its build-policy contract, while the save session expanded a small requested scenario set after the relevant behavior already passed.

The process should remain rigorous for determinism, serialization, threading, trust boundaries, and cross-system changes, while small local changes should stop as soon as their approved acceptance criteria and declared invariants are proven.

## Design

1. Replace the twelve-stage root process with exactly seven named stages: **Approve and classify**; **Implement and propagate**; **Run targeted pre-review checks**; **Review and resolve correctness**; **Apply conditional hygiene**; **Verify the acceptance matrix**; and **Reconcile, audit when triggered, and finalize**. Preserve existing skills as callable roles whose objective triggers are recorded before implementation.
2. During **Approve and classify**, have the manager create an execution-control record containing the fixed process baseline, selected risk tier and concrete triggers, required and conditional roles, and the initial acceptance-criterion-to-check matrix. Classify the whole approved change at the highest applicable risk tier:
   - **Tier 1 — mechanical:** documentation, style, project membership, or local behavior-preserving edits with no public signature or invariant exposure.
   - **Tier 2 — scoped behavior:** one subsystem's runtime/tool behavior with no determinism/CRC, wire, serialization layout, save/replay compatibility, threading, trust-boundary, or shared coordination exposure.
   - **Tier 3 — invariant/integration:** any excluded Tier-2 surface, build/bootstrap coordination that can block all sessions, or a change spanning independently owned subsystems.
   A reviewer may escalate with evidence; lowering the approved tier requires user approval.
3. During **Implement and propagate**, invoke `/update-affected-code` only when an implementation report emits a sweep handoff or the change affects a signature, identity, semantics, layout, client/server guard scope, or required mirrored pattern. Do not run a similarity-only repository sweep.
4. During **Run targeted pre-review checks**, compile affected targets and run the smallest applicable static checks before correctness review so reviewers inspect buildable bytes. Full builds and runtime checks remain acceptance-matrix decisions, not default pre-review work.
5. During **Review and resolve correctness**, run one fresh domain correctness review. C++ uses `/repo-code-review`; Tier-1 non-C++ work uses a fresh domain-coherence review instead of `/repo-code-review`. Add bounded `/adversarial-review` only for Tier 3 or when the primary review names one concrete unresolved failure hypothesis needing independent falsification. Shader changes retain `/glsl-review` for shader-specific evidence. Do not send identical prompts to multiple reviewers merely to seek consensus.
6. Rewrite the adversarial-review contract to attempt bounded falsification, not rejection: a finding blocks only when it names a concrete, reachable, in-scope, material failure path. Hypothetical polish, unrelated baseline defects, and trust-boundary inputs that cannot reach the change are non-findings. When no such failure is proven, return PASS and stop.
7. Make the main session adjudicate the union of reviewer evidence once. Different findings are complementary, not disagreement. Conflicting conclusions are resolved from cited code/test evidence; never discard a round or launch fresh reviewers to manufacture consensus. Default to one focused fix and affected re-review/retest; a second pass requires a still-reproducible blocker and examines only invalidated regions and checks.
8. During **Apply conditional hygiene**, invoke `/code-style-review` only for changed C++, `/update-claude-docs` only for durable instruction or invariant drift, and `/update-vcxproj` only for added/removed files or whole-file client/server affinity changes.
9. During **Verify the acceptance matrix**, map every approved criterion and declared invariant to at least one decisive check, justify skipped checks, and name the independent signal for any intentional duplicate. Tier 1 uses static checks and affected-target compilation when C++ changed; Tier 2 adds the smallest observable scenario for each behavior; Tier 3 adds only exposed invariant-specific client/server, replay/determinism, trust-boundary, coordination, or integration checks. A passing matrix is a hard stop: do not expand into exploratory scenarios.
10. During **Reconcile, audit when triggered, and finalize**, reconcile with primary first. Run one fresh whole-change `/session-audit` only for late semantic fixes, reconciliation-changed bytes, Tier-3 cross-file integration, or changed regions no correctness reviewer saw. Finalize only after focused fixes and invalidated checks pass; retain the existing explicit final landing approval, claim release, and completion-reporting semantics.
11. Add the dedicated [regression reference](../../../.agents/references/cpp-change-process-regression.md) for commits `d730205a`, `98b5272c`, and `90f7ed34`. It records tier, triggered roles, the acceptance evidence matrix, skipped work, and the exact evidence stop. PREfast must not invoke the game harness; Save must stop after its approved save scenarios pass rather than add obstruction variants; DataPacker must not create paired audits per file group.
12. Activate the new process only for sessions whose fixed session-start baseline contains its landed commit. The implementation session remains governed by the prior process captured at its baseline.

## Critical files

- `AGENTS.md` — risk tiers, seven-stage process, convergence/loopback rules, and conditional role triggers.
- `.agents/references/cpp-change-process-regression.md` — frozen-corpus identities and exact evidence-stop dry runs.
- `.agents/skills/repo-code-review/SKILL.md` — single-pass default and evidence threshold.
- `.agents/skills/adversarial-review/SKILL.md` — bounded falsification and PASS/stop contract.
- `.agents/skills/session-audit/SKILL.md` — conditional single whole-change audit trigger.
- `.agents/skills/verify-changes/SKILL.md` — risk-tiered acceptance matrix and bounded retest loop.
- `.agents/skills/resolve-findings/SKILL.md` — one focused fix/re-review round default.
- `Documents/Plans/AGENTS.md` and `Documents/Features/AGENTS.md` — active plans refer to named process stages rather than root stage numbers.
- Live plan/feature files that explicitly require the obsolete paired process — replace only that execution reference; retain their internal design-step numbering.

## Out of scope

- Weakening an approved acceptance criterion, allowing a failed/skipped item to pass, or removing final landing approval.
- Changing C++ style, engine runtime behavior, AgentCli coordination schemas, or build analyzer policy.
- Setting a universal numeric cap on tests or agents regardless of risk.
- Running duplicate reviewers to simulate unavailable model diversity.
- Adding unit tests.

## Acceptance criteria

- Root instructions describe one linear seven-stage workflow with explicit Tier 1-3 triggers and no unconditional correctness/adversarial pair plus paired-per-group final audits.
- Reviewer disagreement is adjudicated once from evidence and never causes a consensus rerun; accepted fixes trigger only affected re-review/retest.
- Adversarial review explicitly stops with PASS when it cannot prove a concrete reachable material failure, and forbids rejection-seeking or unrelated edge-case expansion.
- Style, documentation, project membership, harness, full client/server build, and final session audit each have objective triggers and are skipped when those triggers do not apply.
- Verification maps every approved criterion to at least one decisive check, names duplicate checks' independent signal, and stops when the matrix passes.
- The three-session dry run preserves every material check that found a real issue while eliminating duplicate file-group audits, queue-prose re-reviews, unrelated runtime checks, and consensus rounds.
- The new process applies only when the session's fixed baseline contains its landed commit; this implementation finishes under the earlier baseline's process.
- Every changed skill passes `/validate-skill`; documentation links resolve and active instructions contain no obsolete numbered-root-stage or unconditional paired-review references. Frozen historical fixtures and skill-local procedure numbering are exempt.

## Notes

- Developer workflow only; no simulation/CRC, wire, save format, `.pack`, shader, guard-scope, or runtime allocation exposure.
- Score: Effort 4, Impact 5, Risks 3, total 2; Tier Large.
- This plan overlaps `Agent/BoundAgentContextAndCapabilityReporting.md` in root/reviewer instructions. Reconcile wording once; neither plan is a prerequisite.
