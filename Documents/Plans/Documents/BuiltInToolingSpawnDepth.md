<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T22:55:00.309Z","dependsOn":[]} -->
# Built-In Tooling Spawn Depth

## Context

`.claude/settings.json` sets `"CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH": "1"`, and the root `AGENTS.md` "Subagents" bullet cites that value plus `disallowedTools: Agent` in each of the seven role definitions under `.claude/agents/` as the no-nested-subagent enforcement. The role-level block is independent of the cap and holds at any depth, so every role the Change Workflow itself dispatches is already covered. Built-in Claude Code tooling is not: changelog 2.1.218 made `/code-review` run as a background subagent, so a built-in that fans out internally sits at depth 2 and could be refused under the depth-1 cap.

The landing that introduced the setting could not close this criterion. `/code-review` is user-triggered and separately billed, and `Workflow` requires explicit user opt-in to multi-agent orchestration; no agent can launch either. The check was deferred to a user-run session, with the fallback recorded below.

## Design

In one interactive session at the current primary tip, with `.claude/settings.json` unmodified so `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH=1` is in effect:

1. Run `/code-review` against a non-empty working diff and let it run to completion.
2. Run one `Workflow`, opting in to multi-agent orchestration, and let it run to completion.

Record for each: completed normally, or failed — with the refusal text verbatim.

If both complete, the criterion closes with that observation and no repository bytes change.

If either aborts with a spawn-depth or nested-agent refusal, apply the recorded fallback, which is the entire remediation:

- set `"CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH": "2"` in `.claude/settings.json`;
- amend the root `AGENTS.md` "Subagents" bullet to state the value is 2 because the named built-in requires one nested level, and that the repository's own no-spawn guarantee rests on `disallowedTools: Agent` in the seven role definitions, which still blocks every dispatched role at any depth;
- re-run the failing built-in and confirm it completes at 2.

Both the runs and any fallback edit require explicit user direction in that session: agents may not launch these built-ins or change harness configuration on their own.

## Critical files

- `.claude/settings.json` — `env.CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH`; edited only on an observed failure.
- `AGENTS.md` — "IMPORTANT: Context management and agent selection" > "Subagents", the bullet naming the depth value; edited only on an observed failure.
- `.claude/agents/{planner,reviewer,implementer,researcher,locator,builder,mechanic}.md` — `disallowedTools: Agent`; read-only evidence that role-level enforcement does not depend on the cap.

## In scope

- No tracked file changes when both observed runs complete: the criterion closes on the recorded observation alone.
- `.claude/settings.json` — `env.CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH`, set to `"2"`; edited only on an observed spawn-depth or nested-agent refusal.
- `AGENTS.md` — the "IMPORTANT: Context management and agent selection" > "Subagents" bullet naming the depth value, amended to state that the value is 2 because the named built-in requires one nested level and that the no-spawn guarantee rests on `disallowedTools: Agent` in the seven role definitions; edited only on that same observed failure.

## Out of scope

- Any change to the seven role definitions, their `disallowedTools` lines, or the Change Workflow delegation contract.
- Raising the cap, or editing settings at all, absent an observed spawn-depth failure.
- Auditing other built-in commands, MCP servers, or Codex-side depth behavior; only `/code-review` and one `Workflow` are in scope.
- Any `.codex/` configuration mirror.

## Risk tier and invariants

Tier 2 — scoped tool behavior. The verification alone changes nothing; the fallback edits repository-wide agent configuration and its governing documentation, and exposes no engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface. Invariant: subagents must not spawn subagents. At a raised cap that invariant rests solely on `disallowedTools: Agent` in all seven role definitions; the fallback must not weaken or remove those lines.

## Acceptance criteria

- With `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH=1` in effect, `/code-review` returns its findings and one opted-in `Workflow` reaches its normal end state, neither aborting with a spawn-depth or nested-agent refusal — observed in a single session, both outcomes recorded verbatim.
- If the fallback fired: `.claude/settings.json` reads `"2"`, the root `AGENTS.md` "Subagents" bullet names the value and the built-in that forced it, all seven `.claude/agents/*.md` still carry `disallowedTools: Agent`, and the previously failing built-in completes at 2.
