<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:10.897Z","dependsOn":[]} -->
# Agent Harness Binary Precondition Check

## Context

`.agents/skills/agent-harness/SKILL.md:48` says "Stop if a required executable is absent", and `scripts/Invoke-HarnessClaim.ps1` requires only the resolved `AgentHarness.exe` — nothing verifies the target project's server and client executables before scenario work begins. In landing `000311fd` (Claude session `050f3482`), a harness dispatch ran 6m06s of setup before discovering `BrokenEngineSandboxServer.Debug.exe` had never been built in that worktree (19:17:35-19:23:41Z); recovery cost a 2m19s server build plus a full 13m47s re-dispatch. The trigger is common: any client-only change whose verification still needs a running server.

Root cause: the executable requirement is enforced lazily at launch time, after claim and scenario setup, instead of at skill entry.

## Design

Extend `Invoke-HarnessClaim.ps1` to resolve the active project's server and client executable paths (from the project's `Documents/AgentHarness.md` launch block, as the skill already reads for launching) and require both to exist for the requested configuration before claiming, reporting a typed blocked result naming each missing path so the manager routes `/compile` first and re-enters. When a scenario provably needs only one executable, the caller may name it; default requires both. Update the skill's launch section to state that the claim step already proved existence.

## Critical files

- `.agents/skills/agent-harness/scripts/Invoke-HarnessClaim.ps1` — the existence check and blocked result.
- `.agents/skills/agent-harness/SKILL.md` — the entry precondition sentence and launch-section reference.
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — read-only source of the executable names and output directory.

## In scope

- `Invoke-HarnessClaim.ps1`: the pre-claim existence check, an optional single-executable parameter, and the typed blocked code naming missing paths.
- `agent-harness/SKILL.md`: the precondition sentences.

## Out of scope

- Building anything from the harness path — `/compile` keeps sole ownership of builds; the check only reports.
- Launch, readiness, command, screenshot, and replay behavior.
- The claim-lock semantics and the `broken-engine-harness-claim/v1` schema beyond the new blocked code.

## Risk tier and invariants

Tier 2 — scoped tool behavior in one skill script; no engine runtime or coordination surface.

Invariants: the harness never builds or writes through shared Output links; a successful claim implies both required executables existed at claim time; existing claim outcomes are unchanged when the executables exist.

## Acceptance criteria

- A harness claim in a worktree missing the server executable blocks immediately with a typed result naming the missing path, before any launch or scenario work.
- A claim with both executables present behaves byte-identically to today.
- `/validate-skill` passes on the edited `SKILL.md`.
