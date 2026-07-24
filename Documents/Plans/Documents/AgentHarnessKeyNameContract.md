<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T01:49:10.091Z","dependsOn":[]} -->
# Align Agent-Harness Key Names with the Implemented Contract

## Context

The agent-harness command reference (`Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, `### Client commands`, the `key` bullet at line 111) advertises title-case named keys: "Supports one letter/digit, Escape, Space, Tab, Enter, arrows, and F1-F24." The implementation, `ParseKeyVk` in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:973-1028`, accepts named keys only as exact uppercase tokens: `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`, plus a case-insensitive single letter, a single digit, and `F1`-`F24` (leading `F` or `f`). A direct `{"cmd":"key","params":{"key":"Escape"}}` harness request therefore returns `unknown key`, while the identical request with `ESC` succeeds.

This is a pre-existing documentation/implementation disagreement found while verifying `DisabledPassGatingPerfAudit`; that work owns graphics disabled-pass gating, not the agent command contract. The implementation is already coherent, so this plan corrects the documentation rather than broadening parser behavior. That decision is final; the implementer does not revisit it.

## Design

1. Reconfirm the current `ParseKeyVk` body (`AgentCommandsClient.cpp:973-1028`) before editing, since line numbers drift. Its accepted set is: one ASCII letter (case-insensitive) or digit; exact uppercase `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`; and `F<n>` for n in 1..24 where only the leading `F`/`f` is case-insensitive. Anything else throws `unknown key`. If the function has changed materially from this table, stop and report the contradiction instead of editing.
2. In `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, edit only the `key` bullet under `### Client commands` (currently line 111). Replace its supported-keys sentence so it advertises exactly the accepted canonical spellings, e.g.: ``Supports one letter/digit (case-insensitive), `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`, and `F1`-`F24`.`` Trivial wording latitude is allowed; listing a title-case named key (such as `Escape`) as a supported token is not.
3. Keep the rest of the `key` bullet — the `{"key":name,"holdFrames"?:1}` schema and the `{"ok":true}` return — byte-identical except where the Coordination section below applies.
4. Make no change to `ParseKeyVk`, JSON schemas, aliases, command dispatch, or any other command-reference entry.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent content encountered along the way. Naming a file grants no permission to touch anything in it beyond the named region.

In scope:

- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — only the `key` bullet in `### Client commands`; within that bullet, only the supported-keys wording.

Out of scope:

- Any edit to `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp`, including `ParseKeyVk` — the file is implementation evidence only.
- Making named-key parsing case-insensitive, adding aliases, or changing accepted virtual-key values.
- Any input-script timing or `holdFrames` limit change; `Documents/Plans/Documents/AgentHarnessDeferredInputLimit.md` owns that separate root cause.
- Agent transport single-in-flight behavior, deferred-response policy, graphics behavior, and `DisabledPassGatingPerfAudit`.
- Every other bullet, section, or caveat in `AgentHarness.md`; new agent commands, capabilities, or unit tests.

## Critical files

- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — `key` command contract; the only planned edit.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `ParseKeyVk` at `:973-1028`; read-only evidence.

## Risk tier and invariants

Change Workflow Tier 1: documentation-only, behavior-preserving correction. Invariants: every advertised canonical token must be accepted by the current parser; existing accepted aliases and one-letter/digit behavior remain unchanged; no determinism/CRC, wire, serialization, replay, threading, allocation, shader, build, or client/server-affinity surface is touched.

## Acceptance criteria

- The `key` entry in the command reference contains no title-case named-key spelling presented as a supported token.
- A direct source comparison confirms every documented canonical named-key token is accepted by `ParseKeyVk`, including `ESC`/`ESCAPE`.
- `git diff` shows changes only in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, confined to the `key` bullet; no source file changes.
- `git diff --check` passes.

## Coordination

`Documents/Plans/Documents/AgentHarnessDeferredInputLimit.md` independently adds the `key.holdFrames` range and drain-rate caveat to the same `key` bullet. Neither plan directionally depends on the other. Whichever lands second must retain both the canonical key-name wording and the supported `holdFrames` contract in one coherent entry; do not overwrite the already-landed addition.

## Notes

Prior direct harness evidence (the `Escape` failure / `ESC` success pair in Context) is sufficient to select documentation correction over a behavior change. No runtime verification or compilation is required for this documentation-only edit; the static contract comparison in the acceptance criteria is decisive.
