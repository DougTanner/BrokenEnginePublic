# Verification Ledger Review Rows

## Context

A next-plan-review postmortem of commit e9c9d31c (rehashed e1efba9b) could not tell from the acceptance ledger whether the required domain/style reviews ran in fresh subagent contexts or inline — the verification report lists compile/grep/queue checks but no review rows, and the landing artifact's transcript locator was `"not-collected"`, so establishing review provenance needed manual transcript archaeology. Two gaps, one per artifact: the `/verify-changes` ledger row schema (SKILL.md step 2: `criterion/behavior -> decisive check -> expected result -> independent signal -> status/evidence`) records checks but not the required-review roles that produced or adjudicated them, and `Write-AgentLandingArtifact.ps1` initializes `transcript` to `status = 'not-collected'` with empty candidates, attempting discovery only for the `codex` client — a Claude-client landing always records no locator.

Findings originate from a second-pass multi-agent review and are unverified claims; the executing agent must confirm each one before editing.

## Design

1. **Verify first** (per Diagnosis Discipline; a refuted claim is dropped and named as a residual). Confirm against the current tree that the `/verify-changes` ledger format has no per-review rows and that the landing artifact carries no transcript locator for the producing session (only the codex candidate-discovery branch in `Write-AgentLandingArtifact.ps1`).
2. **`/verify-changes` ledger — one row per required review.** Extend the ledger content requirement in `.agents/skills/verify-changes/SKILL.md` (and the ledger example in `.agents/references/subagent-reporting.md` if its format line needs the field) so a final-evidence-gate ledger carries one row per required review from the execution-control record: skill name (`/repo-code-review`, `/code-style-review`, `/adversarial-review`, …), delegated-or-inline marker, findings count, and resolution disposition. Exactly one row each — no new validation stages, no new receipts, no re-running reviews to populate the row.
3. **`/finalize-changes` landing artifact — transcript locator.** Record the producing session transcript path(s) — parent session plus material subagents where the host supplies them — in the artifact written by `Write-AgentLandingArtifact.ps1`, so future next-plan-review runs get deterministic provenance instead of transcript archaeology. Degrade gracefully: when the host cannot supply a path, record an explicit `"unavailable"` marker; never block a landing on transcript availability. `Find-AgentLandingArtifact.ps1` continues to validate schema/commit binding; adjust only if the new field needs surfacing in the lookup result.

## Critical files

- `.agents/skills/verify-changes/SKILL.md` — step 2 ledger row schema and the Output section's ledger-entry list
- `.agents/skills/finalize-changes/SKILL.md` — steps 2 and 5 landing-artifact invocations (only if the writer's new parameter needs plumbing text)
- `.agents/scripts/Write-AgentLandingArtifact.ps1` — `$transcript` initialization and artifact `transcript` field
- `.agents/scripts/Find-AgentLandingArtifact.ps1` — artifact lookup/validation
- `.agents/references/subagent-reporting.md` — final-evidence-gate ledger line format

## Out of scope

- Any new validation or receipt stage — the spine diet's single-validation-site rule stands; review rows and the transcript locator are recorded fields only.
- Permission readiness probes — owned by [Documents/Plans/Skills/WrapperSessionPermissionReadiness.md](WrapperSessionPermissionReadiness.md); warning-only overlap on next-plan/finalize skill text, coordinate wording but neither plan blocks the other.
- C++ changes.

## Acceptance criteria

- Every executed design item has a recorded verification result preceding its change (refuted claims named as residuals).
- A ledger produced after the change contains one row per required review, each carrying the delegated/inline marker, findings count, and resolution.
- A landing artifact produced after the change carries a transcript locator or an explicit `"unavailable"` marker, and a landing never blocks on transcript availability.
- Word-count discipline: net skill-text growth stays minimal — target under ~80 added words across both SKILL.md files.

## Notes

- Invariant exposure: skill text and agent scripts only — no determinism/CRC, wire, `.pack`/`kiVersion`, replay, client/server guard, or allocation-tracked exposure.
