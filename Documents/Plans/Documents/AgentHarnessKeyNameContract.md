<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T01:49:10.091Z","dependsOn":[]} -->
# Align Agent-Harness Key Names with the Implemented Contract

## Context

The agent-harness command reference advertises the `key` name `Escape` (`Projects/BrokenEngineSandbox/Documents/AgentHarness.md`). `ParseKeyVk` in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:809-866` accepts the named escape key only as uppercase `ESC` or `ESCAPE`; a direct `{"cmd":"key","params":{"key":"Escape"}}` harness request therefore returns `unknown key`, while the identical request with `ESC` succeeds.

This is a pre-existing documentation/implementation disagreement found while verifying `DisabledPassGatingPerfAudit`; that work owns graphics disabled-pass gating and does not own the agent command contract. The implementation is already coherent, so correct the documentation rather than broaden parser behavior.

## Design

1. Reconfirm the current `ParseKeyVk` table before editing. Preserve its existing behavior: one ASCII letter (case-insensitive) or digit; `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`; and `F1` through `F24` (with the existing `F`/`f` prefix behavior).
2. Update only the `key` entry in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` to advertise accepted canonical spellings: uppercase named keys, including `ESC` or `ESCAPE`, and `F1`-`F24`. Do not imply title-case names such as `Escape` are valid.
3. Do not change `ParseKeyVk`, JSON schemas, aliases, command dispatch, or the existing single-letter/digit and named-key behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — `key` command contract; the only planned edit.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `ParseKeyVk` at `:809-866`; implementation evidence only, not modified.

## Out of scope

- Making named-key parsing case-insensitive, adding aliases, or changing accepted virtual-key values.
- Any input-script timing or `holdFrames` limit; `Documents/Plans/Documents/AgentHarnessDeferredInputLimit.md` owns that separate root cause.
- Agent transport single-in-flight behavior, deferred-response policy, graphics behavior, and `DisabledPassGatingPerfAudit`.
- New agent commands, capabilities, or unit tests.

## Risk and invariants

Future implementation is Change Workflow Tier 1: a documentation-only, behavior-preserving correction. Every advertised canonical token must be accepted by the current parser; existing accepted aliases and one-letter/digit behavior remain unchanged. There is no determinism/CRC, wire, serialization, replay, threading, allocation, shader, build, or client/server-affinity change.

## Acceptance criteria

- The command reference contains no title-case named-key spelling that it presents as a supported `key` token.
- A direct source comparison confirms every documented canonical named-key token is accepted by `ParseKeyVk`, including `ESC`/`ESCAPE`.
- The existing parser still accepts a case-insensitive single letter, a digit, the documented uppercase named keys, and `F1` through `F24`; no source file changes outside the command reference.
- `git diff --check` passes.

## Coordination

`Documents/Plans/Documents/AgentHarnessDeferredInputLimit.md` independently adds the `key.holdFrames` range and drain-rate caveat to the same command-reference entry. Neither plan directionally depends on the other. When either lands after the other, retain both the canonical key-name wording and the supported `holdFrames` contract in one coherent entry; do not overwrite the already-landed addition.

## Notes

The prior direct harness evidence is sufficient to select documentation correction over a behavior change. No runtime verification or compilation is required for a documentation-only edit; the static contract comparison is decisive.
