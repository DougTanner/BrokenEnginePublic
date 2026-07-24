<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: Harness Lock Integrity

## Context

Source: /external-architecture-review on `Tools/` recursively. Three gaps in AgentHarness harness-lock integrity:

1. `toolcli::RunSocketCommand` (`Tools/AgentHarness/AgentHarness.cpp`) treats `--owner` as optional — when `owner` is empty it skips `RefreshHarnessHeartbeat` entirely, so a socket command can execute against the game without proving ownership of the default harness claim.
2. The heartbeat refresh happens once, before connect. A valid long command (connect retries until the `--timeout-ms` deadline, up to 10 minutes) can outlive the five-minute staleness window the calling agent uses (`.agents/skills/agent-harness/SKILL.md`), letting another session judge the claim stale and steal it mid-command.
3. `toolcli::RunHarnessLockCommand` and `toolcli::RefreshHarnessHeartbeat` (`Tools/AgentHarness/HarnessLockCommands.cpp`) act on any record `coordination::ReadMetadata` can parse. `coordination::ValidateMetadataEnvelope` (`Tools/ToolCommon/CoordinationStore.h:61`, implemented in `CoordinationStore.cpp`) already checks `schemaVersion`, `domain`, `logicalKey` against the locator, owner/session/worktree/timestamp fields, and `claimantPid` — but nothing in AgentHarness calls it, so a parseable record with the wrong domain, key, or schema is status-reported, heartbeated, released, or stolen as if valid.

## Design

Reuse the existing shared validator; no new metadata fields, no changes to `Tools/ToolCommon`.

### `Tools/AgentHarness/HarnessLockCommands.cpp` — validate the envelope after every read [~30m]

- In `RunHarnessLockCommand`: after the successful `ReadMetadata(locator->path, metadata)` call (the `bExists && !ReadMetadata(...)` block near line 164), when the record exists, validate it with `coordination::ValidateMetadataEnvelope(metadata, *locator)` before any use of `metadata` — that covers the `status` print, the `claim` conflict print, the combined `HasOwner` owner-comparison gate for `release`/`heartbeat`/`steal`, and the `release`/`heartbeat`/`steal` actions themselves (lines 170-235). A parseable but envelope-invalid record is treated as a failure (`Fail` + `kiExitFailure`), not as a held-or-free lock state, so it cannot be reported valid, heartbeated, released, or stolen through these normal verbs.
- In `RefreshHarnessHeartbeat`: after its `ReadMetadata` succeeds (line 255), require `ValidateMetadataEnvelope` to pass in addition to the existing `HasOwner` check before `StampHarnessHeartbeat`.
- Retain corrupt-record recovery only as an explicit, separately authorized path. (Original plan wording preserved; the concrete recovery mechanism is not specified by this plan — see Notes.)

### `Tools/AgentHarness/AgentHarness.cpp` — mandatory owner, live claim [~1h]

All changes inside `RunSocketCommand`:

- Make `--owner` required: replace the `if (!owner.empty() && !RefreshHarnessHeartbeat(owner))` pre-connect check (lines 193-197) with a hard requirement — empty `owner` fails with usage (`kiExitFailure`), and a pre-connect `RefreshHarnessHeartbeat(owner)` returning `false` still fails as today. Note `RefreshHarnessHeartbeat` currently returns `true` for an empty owner; with `--owner` mandatory that early-out becomes unreachable from this call site — do not repurpose it.
- Keep the claim live for the duration of the command: during the blocking connect-retry loop and the send/receive phase (the `do { ... } while (false)` block, lines 212-314), periodically re-run `RefreshHarnessHeartbeat(owner)` (which takes the `.guard` transition guard internally) at an interval safely under the five-minute staleness window, bounded by the existing `--timeout-ms` deadline — no refresh extends the command past its deadline. The natural insertion points are the connect-retry loop iterations and, for the post-connect phase, a refresh before entering send/receive; a refresh mechanism that covers a full `--timeout-ms` (max 600000 ms) command without a >5-minute heartbeat gap is required, the exact cadence is implementer's choice.
- Abort on ownership loss: if any mid-command `RefreshHarnessHeartbeat` returns `false` (owner mismatch, invalid envelope, or unreadable record), stop the command and exit `kiExitFailure` instead of continuing to drive the game.
- Update the `PrintUsage` text so `--owner TOKEN` is no longer shown as optional (`[--owner TOKEN]` → `--owner TOKEN`).

### Documentation propagation

- `Tools/AgentHarness/AGENTS.md`: the bullet describing `--owner` heartbeat behavior and exit codes must reflect mandatory `--owner` on socket commands and in-command heartbeat refresh; the staleness-model bullet (agent judges staleness, executable never evaluates it) stays true and stays.
- `.agents/skills/agent-harness/SKILL.md`: socket-command examples already pass `--owner $Owner`; update only prose that implies `--owner` is optional on socket commands, and note that a long-running command keeps its own heartbeat fresh. Do not change the five-minute staleness/steal procedure.

## Critical files

- `Tools/AgentHarness/AgentHarness.cpp`
- `Tools/AgentHarness/HarnessLockCommands.h`
- `Tools/AgentHarness/HarnessLockCommands.cpp`
- `Tools/AgentHarness/AGENTS.md`
- `.agents/skills/agent-harness/SKILL.md`

## Scope contract

This listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope (named regions only):**

- `Tools/AgentHarness/HarnessLockCommands.cpp`: `RunHarnessLockCommand` (post-`ReadMetadata` validation only) and `RefreshHarnessHeartbeat`; anonymous-namespace helpers only if a small shared validation helper is extracted for these two call sites.
- `Tools/AgentHarness/AgentHarness.cpp`: `RunSocketCommand` (argument requirement, mid-command heartbeat, abort path) and `PrintUsage` text.
- `Tools/AgentHarness/HarnessLockCommands.h`: only if a signature addition is needed for the changes above.
- `Tools/AgentHarness/AGENTS.md` and `.agents/skills/agent-harness/SKILL.md`: only the statements the behavior change makes incorrect.

**Out of scope:**

- Changing length-prefixed JSON framing, request/response size limits, endpoint commands, or game-side transport (`SendAll`, `ReceiveAll`, `ConnectWithTimeout`, framing constants stay as-is).
- Making AgentHarness itself judge five-minute staleness or perform automatic steals — the calling agent keeps that role.
- Adding landing, build, queue, or repository behavior to AgentHarness.
- Any change to `Tools/ToolCommon` (including `ValidateMetadataEnvelope` itself), lock verbs, `--key`/`--expect` semantics, or the `lock token` command.

## Risk tier

Tier 3 — cross-process ownership and long-running command coordination.

## Acceptance criteria

- No socket command can execute without proving ownership of the default harness claim: `AgentHarness.exe --port N "<json>"` without `--owner`, or with an owner that does not match the current claim, exits `1` without connecting.
- A valid command lasting five to ten minutes keeps its claim's `heartbeatAt` fresher than five minutes throughout, and ownership loss mid-command stops the command with exit `1`.
- Parseable but schema/domain/key-invalid lock metadata cannot be status-reported as valid, heartbeated, released, or stolen through the normal `lock` verbs or `RefreshHarnessHeartbeat`.
- AgentHarness lock and socket fixtures preserve exit codes `0` (success), `1` (usage/transport/OS failure), and `2` (state conflict / negative JSON response) and the documented JSON framing.

## Notes

- Invariant exposure: exclusive harness command ordering and reliability of runtime/replay acceptance evidence; no direct simulation CRC, `.pack`, replay format, client/server layout, or allocation-tracked runtime exposure.
- Unresolved (do not invent): the original plan directs retaining corrupt-record recovery "only as an explicit, separately authorized path" without specifying that path's mechanism (flag, verb, or manual deletion). Implement the validation-as-failure behavior above; surface the recovery-path decision to the user before adding any new recovery command.
