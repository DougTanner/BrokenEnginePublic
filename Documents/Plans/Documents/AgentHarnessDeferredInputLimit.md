<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T01:49:10.092Z","dependsOn":[]} -->
# Bound Agent-Harness Key Hold Duration by Deferred Liveness

## Context

The `key` command currently reads `holdFrames` as `int64_t`, narrows it to `int32_t`, and only then lower-clamps it (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:995-1005`). That does not normalize every signed negative before narrowing, and reading only `int64_t` does not decisively bound an unsigned JSON integer above `INT64_MAX`. The command reference documents the default but no maximum or negative-value behavior (`Projects/BrokenEngineSandbox/Documents/AgentHarness.md`).

Every deferred command is instead subject to the `AgentCommandServer` liveness budget of `kiDeferredTimeoutDrains = 1800` (`Engine/Source/Agent/AgentCommandServer.h:62-64`), with the `Drain()` timeout evaluated before its deferred poll (`AgentCommandServer.cpp:222-230`). On the client, `Drain()` starts the script before `AgentInput::AdvanceFrame()` (`Engine/Source/Main.cpp:372-383`); a key script requires one press frame, `holdFrames` hold frames, and a final completion frame (`AgentInput.cpp:329-355`). The poll completes two drains after a positive hold's final hold frame, so the current safe maximum is `1800 - 2 = 1798` frames. A `holdFrames:4800` request was observed to fail after roughly 15.6 seconds with the deferred timeout even while the key script continued; three sequential `holdFrames:1500` requests succeeded.

This is a pre-existing, out-of-scope residual from `DisabledPassGatingPerfAudit`. It is not the single-in-flight/concurrency root owned by `Documents/Plans/Network/AgentTransportConcurrentCommands.md`: this plan does not change transport sequencing or allow concurrent commands.

## Design

1. Reconfirm the current client ordering and phase count before editing. If `Drain()` no longer precedes `AdvanceFrame()`, or the key phases no longer yield a safe maximum of `kiDeferredTimeoutDrains - 2`, stop and recalculate the bound from the live code rather than retaining `1798` by assumption.
2. Expose the server deferred-drain limit to the client command handler through one canonical engine declaration; do not duplicate `1800`. At the `CommandKey` JSON trust boundary, require a JSON integer using the established `DesyncProbeCountParameter` pattern (`AgentCommandsClient.cpp:264-286`), then inspect representations separately: read unsigned integers as `uint64_t` and reject every value above the derived safe key-hold maximum before narrowing; read signed integers as `int64_t`, normalize every negative to `0` before narrowing, and reject every positive value above that maximum. Return a clear type error for non-integers and a clear range error for over-limit integers. Leave the default at `1`.
3. Keep the existing key script's press, hold, release, and completion timing unchanged. The fix only rejects requests that cannot complete before the established liveness timeout; it must not extend or disable that timeout.
4. Update the `key` command reference with the inclusive current range `0..1798`, default `1`, and signed-negative normalization to `0`. State that the range is a client-drain/frame count rather than a wall-clock guarantee, so actual duration varies with the client drain rate; do not promise a fixed number of seconds.

## Critical files

- `Engine/Source/Agent/AgentCommandServer.h` — owns the canonical deferred-drain liveness limit; expose only the value needed to derive the key-command bound.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `CommandKey` at `:995-1007`; validate at the JSON trust boundary before narrowing and starting the deferred script.
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — `key` command contract; document the inclusive range and drain-rate caveat.
- `Engine/Source/Main.cpp` (`:372-383`) and `Engine/Source/Agent/AgentInput.cpp` (`:329-355`) — ordering and phase-count evidence; modify neither unless current-source verification disproves the stated derivation.

## Out of scope

- Changing `kiDeferredTimeoutDrains`, its liveness purpose, error policy, or generic deferred capture behavior.
- `hover.holdFrames` or any other script's duration; each has distinct phases and no proven mismatch here.
- Agent transport concurrency, pipelining, listener/response ordering, or the separate `AgentTransportConcurrentCommands` plan.
- Extending supported input duration, adding commands, changing key press/release behavior, or adding unit tests.

## Risk and invariants

Future implementation is Change Workflow Tier 3: it changes a JSON trust-boundary contract and the Engine/game agent-command interface, both excluded from Tier 2. The public command must reject non-integers and over-limit integers immediately and descriptively, never later as the server's misleading deferred timeout; signed negatives normalize to `0` before narrowing. All accepted normalized values must complete before the existing `kiDeferredTimeoutDrains` liveness failure, with no change to its frame-rate-dependent nature. Preserve the one-in-flight transport, the server timeout, key-script phase semantics, default `holdFrames:1`, and behavior for `0..1798`. No determinism/CRC, wire, serialization, replay, data layout, or build/bootstrap contract changes are authorized.

Future execution must follow Tier 3 gates: `/plan-audit`, then `/external-grill-plan`; affected-code propagation; targeted client and server builds plus the focused client harness scenario; fresh C++ and documentation reviews; `/adversarial-review`; and the Tier-3 integration final-evidence route through `/verify-changes` and `/finalize-changes`. Do not execute those gates while authoring this follow-up Plan.

## Acceptance criteria

- The maximum accepted `key.holdFrames` is derived from the server's canonical deferred-drain limit and the verified key-script completion overhead, with no independent `1800` literal in the client handler.
- `holdFrames:1799`, `INT32_MAX`, and unsigned `UINT64_MAX` fail synchronously with the documented range error before narrowing, rather than starting a script or returning `capture timed out`.
- A signed `-1` and signed `INT64_MIN` both normalize to `0` before narrowing and follow the same successful behavior as `holdFrames:0`; a floating-point value, string, boolean, null, or other non-integer JSON value fails with the documented type error.
- `holdFrames:0`, the default, and a representative existing long hold (`1500`) remain accepted; `1798` completes successfully with an increased harness response timeout appropriate for the live drain rate.
- The command reference documents `0..1798`, default `1`, signed-negative normalization to `0`, and the drain-count/frame-rate caveat; its contract agrees with the enforced type and range behavior.
- Build the affected client and server targets, then run the focused client harness sequence above. `git diff --check` passes.

## Coordination

`Documents/Plans/Documents/AgentHarnessKeyNameContract.md` independently corrects canonical key-name spellings in the same command-reference entry. Neither plan directionally depends on the other. When either lands after the other, retain both the canonical key-name wording and the `holdFrames` range/rate caveat in one coherent entry; do not overwrite the already-landed addition.

## Notes

The `1798` value is intentionally an implementation-derived current limit, not a time guarantee: the observed 15.6-second timeout for a much longer request differs from the source comment's approximately 30 seconds at 60 fps because drains follow the live client frame rate. Do not use the observed elapsed time as the contract.
