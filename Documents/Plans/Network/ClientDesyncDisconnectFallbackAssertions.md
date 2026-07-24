<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Keep Client Desync Disconnect Fallbacks Reachable

## Context

`ClientDesyncManager::PollDebugFrameResponse` and `ClientDesyncManager::PollDesyncTimeout` in `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` each contain an intentional modal-message + `Client::Disconnect` fallback in their `!kbDesyncRecovery` `else` branch. Both branches begin with an unconditional `ASSERT(false)` immediately before that fallback. `common::Assert` (`Common/ErrorUtils.cpp`) logs, optionally breaks, and always throws `std::runtime_error`, so neither fallback can execute: the disabled-recovery path throws instead of showing the modal and disconnecting.

The fixed session baseline `29376fe49c2287e12b1f6ec6d84b54bd3126b4a4` already contained both assertions (`ClientDesyncManager.cpp:46` and `:69` at that revision), proving the defect predates the desync-debug infrastructure work that surfaced it. That work owns compile-time debug gating, server request throttling, deadline handling, and the diagnostic probe; the client fallback policy is outside its accepted scope. The repository rule being restored: recovery/fallback code must not be blocked by an assertion immediately before it (AGENTS.md, "No useless ASSERTs").

The two removals belong together: same root cause, same client manager, and they guard the two terminal outcomes (response received vs. timeout) of the single debug-frame wait entered by `OnDesyncDetected`.

## Design

Delete exactly two statements, both `ASSERT(false);` lines:

1. In `ClientDesyncManager::PollDebugFrameResponse`, the `ASSERT(false);` that is the first statement of the `else` branch of `if constexpr (kbDesyncRecovery)` (the branch that sets `gpGame->mModalMessage` to `"Desynced from server"` and calls `Disconnect()`).
2. In `ClientDesyncManager::PollDesyncTimeout`, the `ASSERT(false);` in the `else` branch of `if constexpr (kbDesyncRecovery)`, located between the existing `LOG(kNetwork, kError, ...timed out, disconnecting")` call and the `std::snprintf` that sets the `"Desynced from server (debug frame timeout)"` modal message.

Do not replace either assertion with any other break, throw, log, or comment. Preserve unchanged: every existing `LOG` call, `mDesyncDebugState = {};` clearing, modal text, `Disconnect()` call, `if constexpr` gates (`kbDesyncRecovery`, `kbDebugBreak`), the `kDesyncDebugTimeout` deadline check, the `return true;` in `PollDesyncTimeout`, and all recovery-enabled behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` — the only file edited; the two `ASSERT(false);` statements above.
- `Common/ErrorUtils.cpp` — read-only evidence that `common::Assert` always throws; no edit.
- `Projects/BrokenEngineSandbox/Source/Pch.h` — read-only reference for `kbDesyncRecovery` / `kbDesyncDebugFrames` / `kbDebugBreak` definitions; no edit.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

**In scope (exact regions):**

- `ClientDesyncManager.cpp`, function `ClientDesyncManager::PollDebugFrameResponse`, `!kbDesyncRecovery` `else` branch: delete the single `ASSERT(false);` statement.
- `ClientDesyncManager.cpp`, function `ClientDesyncManager::PollDesyncTimeout`, `!kbDesyncRecovery` `else` branch: delete the single `ASSERT(false);` statement.

Naming this file grants no permission to touch anything else in it. No other function, statement, include, or comment in `ClientDesyncManager.cpp` changes; the deletions require no new includes or declarations.

**Out of scope:**

- Changing `kbDesyncRecovery`, `kbDesyncDebugFrames`, or `kbDebugBreak` values or their placement in `Pch.h`.
- Changing desync-debug request/response gating, server throttling, `kDesyncDebugTimeout` duration or clock, diagnostic commands, or wire behavior.
- Changing `RecoverFromDesync` soft recovery, repeated-desync escalation, modal wording, disconnect semantics, `OnDesyncDetected`, or any other assertion anywhere in the repository (including the `ASSERT(!IsStalled())` in `ArmAgentFullStateFixture`).
- Adding a dedicated timeout-injection command or unit tests.

## Risk tier

Tier 2 — scoped client runtime behavior. A two-line removal in one client-only (`BT_CLIENT`) subsystem, but it changes the reachable failure outcome when desync recovery is disabled.

**Invariant exposure:** client-only diagnostic/failure handling outside the CRC'd simulation tick. No determinism/CRC, replay, save/`kiVersion`, `.pack`, serialization, wire-protocol, server-guard, shader, or allocation-tracked-path change.

## Acceptance criteria

- Neither `!kbDesyncRecovery` branch executes an assertion, debug break, or throw before its existing modal-message + `Disconnect()` fallback.
- `PollDebugFrameResponse` still logs frame differences via `LogDifferences` and clears `mDesyncDebugState` before choosing recovery or disconnect; `PollDesyncTimeout` still clears `mDesyncDebugState`, logs the timeout outcome, returns `true`, and chooses recovery or disconnect.
- A client target build passes after the removals.
- In a diagnostic build with `kbDesyncDebugFrames = true` and `kbDesyncRecovery = false`, the existing `desync_probe` recovery scenario (see `/agent-harness`) reaches the debug-frame-response modal/disconnect outcome without an `Assert failed` log. The timeout branch is additionally confirmed by direct control-flow inspection, because the probe does not inject a withheld server response.

## Notes

- **Scoring (historical anchors, non-scheduling):** Quick Win; Effort 1, Impact 3, Risks 1. Restores an already-written user-visible failure fallback with a narrow, easily reverted change.
