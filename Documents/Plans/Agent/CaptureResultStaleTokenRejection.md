# Capture Result Stale Token Rejection

## Context

The async capture-result mailbox does not enforce the token contract its comments describe. `ResetCaptureResult` clears the single result slot and mints a token, but does not record that token as the mailbox's active request. `SetCaptureResult` then unconditionally replaces the slot and its result token (`Engine/Source/Graphics/Screenshot.cpp:41-64`).

Normally the agent transport permits only one request at a time. However, `AgentCommandServer::Drain` can time out a deferred capture and publish `"capture timed out"` while that capture's encoder remains active (`Engine/Source/Agent/AgentCommandServer.cpp:191-250`). A later capture can then start. Screenshot and render-target dump encoding use independent static futures (`SaveScreenshot`'s `sSaveScreenshot` and `DumpRenderTarget`'s `sDump`), so opposite-type captures can overlap: an old screenshot can finish after a new dump has called `ResetCaptureResult`, or vice versa. The old `SetCaptureResult(oldToken, ...)` overwrites the new result slot; `TakeCaptureResult(newToken)` rejects the old token but has lost the new result and waits until the outer deferred timeout fires again.

This race is confirmed by the current lock and assignment order. The existing token prevents consuming a stale result as the new response, but does not prevent a stale publisher from destroying the new response.

## Design

- Make the mailbox track its current active capture token under `sCaptureResultMutex`.
- In `ResetCaptureResult`, mint the token, store it as active, clear any prior result, and return it while holding the same lock.
- In `SetCaptureResult`, ignore a publication whose token is not the active token. Only the active request may populate the single result slot.
- In `TakeCaptureResult`, require the requested token to be active and a result to exist, then consume the result. Remove the redundant result-token state if the active-token invariant makes it unnecessary.
- Update the capture-mailbox comments in `Screenshot.{h,cpp}` and request-token comments in `Graphics.h` to state the enforced stale-publication behavior.

Keep the API signatures and single-slot design unchanged. The active-token check must stay inside the existing mutex critical section so `ResetCaptureResult`, stale publication, and result consumption have one total order.

## Critical files

- `Engine/Source/Graphics/Screenshot.cpp` — capture mailbox state and `ResetCaptureResult`, `SetCaptureResult`, `TakeCaptureResult`; independent screenshot/dump futures establish the cross-type overlap.
- `Engine/Source/Graphics/Screenshot.h` — public mailbox contract comments.
- `Engine/Source/Graphics/Graphics.h` — `ScreenshotRequest::uiCaptureToken` and `DumpRenderTargetRequest::uiCaptureToken` contract comments, if wording needs synchronization.
- `Engine/Source/Agent/AgentCommandServer.cpp` — verify deferred timeout/shutdown behavior; no transport change expected.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — verify `BeginCaptureAndDefer` reset/take order and minimized-window cleanup; no command-flow change expected.

## Acceptance criteria

- A result published for any token older than the most recent `ResetCaptureResult` cannot replace or clear the active request's result.
- After an abandoned capture finishes late, the next opposite-type capture (`screenshot` then `dump_render_target`, and the reverse) completes with its own path/metadata instead of timing out.
- Normal screenshot and dump capture still complete from an agent-mode client launched minimized, including restore-without-activation and re-minimize cleanup.
- Timeout, disconnect, and shutdown paths remain bounded and safe while an encoder is active; no future ownership or thread-lifetime behavior changes.
- Client build passes. Live agent-harness verification covers both cross-type command orders; the stale-order case is forced with a temporary, non-landed timeout/delay adjustment or equivalent debugger scheduling if normal encoding does not reproduce it reliably.

## Out of scope

- Replacing minimized restore/capture/re-minimize with an off-screen renderer.
- Unifying or serializing the screenshot and dump encoder futures.
- Changing `AgentCommandServer`'s deferred timeout value, single-in-flight transport, connection model, or JSON schema.
- Adding a persistent test-only delay/fault-injection command.
- General screenshot/dump encoding, image-format, or filesystem error handling.

## Notes

- Client-only agent/graphics correctness fix. No deterministic simulation/CRC, replay, network wire, `kiVersion`, `.pack`, or shader exposure.
- `Screenshot.cpp` runs mailbox publication on encoder threads and consumption/reset on the main thread; keep all active-token/result state under `sCaptureResultMutex`.
- Allocation-tracked behavior is unchanged; the fix adds no per-capture heap work.
- The neighboring `Graphics/ScreenshotStbHeaderCentralization.md` and `Graphics/Refactor_RenderUniformsQuickWins.md` plans also touch `Screenshot.cpp`; co-schedule or refresh their citations.
