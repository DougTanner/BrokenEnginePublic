<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-29T18:00:59.825Z","dependsOn":["Documents/Plans/Documents/TierOneNextPlanFastPath.md"]} -->
# Reduce agent workflow context after removing obsolete ceremony

## Context

The final verifier for `4f74a207` used about 1.04M exposed tokens across approximately 19 model calls. Roughly 954K were cached input and only about 12K were generated output. Context grew from approximately 23K to 87K tokens through repeated loading of workflow documents and broad tool output. Sol's review behavior magnified that input, but the repeated stages and oversized mandatory read set are the primary repository-controlled causes.

The current `bt-token-v1` baseline is 17,716 tokens across the normal workflow cores: next-plan 2,623; verify-changes 2,866; finalize-changes 6,872; execution gates 2,697; subagent reporting 1,176; update-vcxproj 1,482.

The prerequisite Plans remove final manifests, agent-visible receipt identity, duplicate Tier-1 gates, and ad hoc project validation. This Plan reduces the remaining context only after those semantics are stable, so obsolete prose is deleted instead of merely moved.

## Design

First delete every clause made obsolete by the prerequisite Plans. Keep each `SKILL.md` focused on its normal route. Move only still-required conditional material to directly linked, trigger-specific references for primary commits, AgentTools promotion/bootstrap, conflict and recovery, Tier-3 grill/audit, and optional SmartGit review. Agents load a reference only when its named trigger is present.

Delegation prompts do not paste inherited `AGENTS.md`, skill text, the full executable Plan, manager transcript, or generic process explanation. Supply only the objective, exact ownership/scope, baseline or candidate commit, acceptance checks, and task-specific prohibitions. Remove mandatory `Delegation basis` and generic `Context` blocks where those fields already prove the relationship.

Reviewers inspect the candidate diff and the cited Plan clauses from Git. They do not ingest complete command logs, unrelated workflow references, or manager transcripts.

Sidecars project compact success JSON containing status, code, message, next-stage identities, and bounded counts. Do not nest raw WorktreeCli responses. Failures return the decisive diagnostic and a bounded selector for retained evidence rather than dumping complete files, XML, or logs.

Use `rg`, structured validators, and bounded line windows for repository evidence. Remove workflow instructions that encourage whole-file dumps when a deterministic selector exists.

Eliminate manager progress pings and polling loops while a child remains host-reported running. Use host completion notifications. A coordination sidecar may perform one bounded wait and return typed retry timing; the manager resumes from that result instead of running repeated status probes.

Keep Sol as the reviewer. Reduce the number of review stages and their mandatory context before considering a model substitution.

## Critical files

- `.agents/skills/{next-plan,verify-changes,finalize-changes,update-vcxproj}/` — compact cores and trigger-specific references.
- `.agents/skills/next-plan/references/execution-gates.md` and `.agents/references/subagent-reporting.md` — remove repeated contracts and prompt boilerplate.
- `.agents/scripts/Measure-Tokens.ps1` — acceptance measurement; no change expected.

## Out of scope

- Replacing Sol or changing role/model mappings.
- Weakening Tier-2/Tier-3 correctness, adversarial, build, runtime, conflict, recovery, or AgentTools requirements.
- Moving required policy solely to meet a token target.
- Introducing another report, manifest, receipt, approval artifact, or prompt schema.

## Risk tier and invariants

**Tier 3.** Trigger: reorganizes contracts across Plan selection, review, finalization, recovery, and reporting.

Every surviving trigger must remain discoverable from the core skill; missing/malformed evidence must keep failing closed where it does today; conditional safety instructions must load whenever their trigger fires; smaller prompts must retain exact objective, ownership, candidate identity, and acceptance checks. No engine runtime invariant is exposed.

## Acceptance criteria

- The six measured workflow-core files total no more than 8,858 `bt-token-v1` tokens, half the recorded 17,716 baseline.
- The mandatory skill/reference read set for a normal Tier-1 next-plan route is no more than 6,000 tokens, excluding the executable Plan and automatically supplied root instructions.
- A normal Tier-1 reviewer prompt contains no receipt identity, final manifest, full Plan copy, root-AGENTS copy, transcript, raw XML, or unrelated conditional workflow.
- Primary-commit, AgentTools, conflict/recovery, Tier-3, and SmartGit references are loaded only when their explicit trigger is present.
- Success-sidecar output contains no nested raw tool result or unbounded file/log body; negative fixtures retain a decisive bounded diagnostic.
- A host-reported running worker causes no progress ping or repeated manager status command.
- All changed skills pass `/validate-skill`, and their existing integration fixtures pass.

## Notes

The numeric target measures the repository-controlled core, not automatically injected root policy or the task's executable Plan. Removal takes precedence over moving text to a reference.
