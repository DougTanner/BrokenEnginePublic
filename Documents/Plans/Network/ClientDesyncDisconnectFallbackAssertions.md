# Keep Client Desync Disconnect Fallbacks Reachable

## Context

`ClientDesyncManager::PollDebugFrameResponse` and `ClientDesyncManager::PollDesyncTimeout` in `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` each enter an intentional modal-message + `Client::Disconnect` fallback when `kbDesyncRecovery` is false. Both branches first execute `ASSERT(false)`. `common::Assert` logs, optionally breaks, and always throws, so neither fallback can run.

The fixed session baseline `29376fe49c2287e12b1f6ec6d84b54bd3126b4a4` contains both assertions (`ClientDesyncManager.cpp:46` and `:69` at that revision), proving the defect predates the desync-debug infrastructure work that surfaced it. That active work owns compile-time debug gating, server request throttling, deadline handling, and the diagnostic probe; changing the client fallback policy is outside its accepted scope. The remaining acceptance gap is the repository rule that recovery/fallback code must not be blocked by an assertion immediately before it.

## Design

- Remove only the unconditional `ASSERT(false)` from the `!kbDesyncRecovery` branch in `ClientDesyncManager::PollDebugFrameResponse`.
- Remove only the unconditional `ASSERT(false)` from the `!kbDesyncRecovery` branch in `ClientDesyncManager::PollDesyncTimeout`.
- Preserve each existing error log where present, modal text, disconnect call, compile-time gates, timeout deadline, and recovery-enabled behavior. Do not replace the assertions with another break or throw.

The two removals belong together because they are the same root cause in the same client manager, guard the two terminal outcomes of one debug-frame wait, and use the same build and runtime verification strategy.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` — `ClientDesyncManager::PollDebugFrameResponse` and `ClientDesyncManager::PollDesyncTimeout`; make both disabled-recovery disconnect fallbacks reachable.
- `Common/ErrorUtils.cpp` — read-only evidence for the `ASSERT(false)` throw behavior; no edit intended.

## Out of scope

- Changing `kbDesyncRecovery`, `kbDesyncDebugFrames`, or `kbDebugBreak` defaults or their placement.
- Changing desync-debug request/response gating, server throttling, timeout duration or clock, diagnostic commands, or wire behavior.
- Changing soft recovery, repeated-desync escalation, modal wording, disconnect semantics, or any other assertion.
- Adding a dedicated timeout-injection command or unit tests.

## Acceptance criteria

- Neither disabled-recovery branch executes an assertion, debug break, or throw before its existing modal-message + disconnect fallback.
- `PollDebugFrameResponse` still logs frame differences and clears debug state before choosing recovery or disconnect; `PollDesyncTimeout` still clears debug state, logs the timeout outcome, returns `true`, and chooses recovery or disconnect.
- A client target build passes after the removals.
- In a diagnostic build with `kbDesyncDebugFrames=true` and `kbDesyncRecovery=false`, the existing `desync_probe` recovery scenario reaches the debug-frame-response modal/disconnect outcome without an `Assert failed` log. The timeout branch is additionally confirmed by direct control-flow inspection because the probe does not inject a withheld server response.

## Notes

- **Change Workflow risk:** Tier 2 scoped client runtime behavior. The implementation is a two-line removal in one client-only subsystem, but it changes the reachable failure outcome when desync recovery is disabled.
- **Invariant exposure:** client-only (`BT_CLIENT`) diagnostic/failure handling outside the CRC'd simulation tick. No determinism/CRC, replay, save/`kiVersion`, `.pack`, serialization, wire protocol, server guard, shader, or allocation-tracked-path change.
- **Scoring:** Quick Win; Effort 1, Impact 3, Risks 1, Score -1. This restores an already-written user-visible failure fallback with a narrow, easily reverted change.
