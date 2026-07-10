# mouse wheel Ignores x/y and Scrolls the OS-Cursor Window

## Summary

**What this plan does:** Makes the agent harness `mouse` command's `action:"wheel"` honor optional `x`/`y` pixel coords by routing them through the synthetic-position path the harness already drives for `click`/`hover`. Today the wheel branch of `CommandMouse` parses only `notches`, never sets `bHasCoord`/`f2CoordPixels`, so `AgentInput::AdvanceFrame` skips its pre-switch `IssueImGuiMousePos` call and ImGui resolves the wheel's hovered window from stale `io.MousePos` — i.e. wherever the real OS cursor last sat. On a headless/automated run the real cursor is typically outside the game window, so `AddMouseWheelEvent` lands on nothing and the command returns `ok:true` having scrolled nothing.

**Why it's good for the codebase:** Closes the same silent-false-success hole the just-landed `AgentClickScrollClippedWidgets` fix closed for `click`/`hover`/`set_slider`. It matters more now: scroll is the documented remedy for that fix's new "target not visible (scrolled out of view)" error, so an agent told to "scroll it into view, then retry" needs the wheel to actually target the scroll container. The fix reuses existing plumbing (`AgentInput::IssueImGuiMousePos`, already called for `bHasCoord` scripts) — a few lines in `CommandMouse` plus a SKILL.md doc line.

## Context

- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 1 / Score 1 — co-scheduled with the two sibling Agent harness plans (`AgentClickScrollClippedWidgets.md`, `AgentPauseTimescaleEmptyServer.md`), same `AgentInput.cpp`/`AgentCommandsClient.cpp` files.
- Residual source: live harness verification this session — the wheel scroll was a silent no-op (`ok:true`) because the real OS cursor sat outside the game window; the verification agent had to work around it with direct Win32 `SetCursorPos`.
- Relevance: Fully — every cited mechanism verified against current source (see next section).

## Verified mechanism (current source)

- `CommandMouse` (`AgentCommandsClient.cpp`, `~:417-485`): the `if (script.eMouseAction == kWheel)` branch (`:448-451`) reads only `notches` into `iWheelNotches`. The `else` branch (`:452-482`) is the *only* place that reads `x`/`y` into `script.f2CoordPixels` and sets `script.bHasCoord = true`. So a wheel script always arrives with `bHasCoord == false` (struct default, `AgentInput.h:64`).
- `AgentInput::AdvanceFrame` kMouse case (`AgentInput.cpp:340-349`): **before** the per-action `switch`, `if (mScript.bHasCoord)` sets the overlay synthetic pixel pos **and** calls `IssueImGuiMousePos(f2CoordPixels...)`. `IssueImGuiMousePos` (`:88-94`) issues `ImGui::GetIO().AddMousePosEvent(fX, fY)`. Because wheel scripts have `bHasCoord == false`, this block is skipped and no synthetic `io.MousePos` is issued for the wheel.
- Wheel event synthesis (`AgentInput.cpp:361-377`): phase 0 bumps `miSyntheticScrollAccumulator` (overlay/game-camera sink) and issues `pIo->AddMouseWheelEvent(0.0f, notches)` (ImGui sink). ImGui's `NewFrame` applies queued input events in order — a `MousePos` event issued the same frame is processed before hovered-window computation, so `g.HoveredWindow` (which drives `UpdateMouseWheel` routing) reflects the synthetic pos. With no synthetic pos issued, `io.MousePos` retains the last hardware/`WndProc` value (the OS cursor), which is the observed coupling.
- The overlay/game-camera-zoom sink (`miSyntheticScrollAccumulator`, folded into `iScrollWheelValue` in `RawInputManager::Update`) is position-independent and already works headless — this defect is ImGui-window routing only.

## Design

KISS fix, reusing the existing synthetic-position path:

1. `AgentCommandsClient.cpp` `CommandMouse` — in the `kWheel` branch, additionally parse optional `x`/`y` (when both present): set `script.f2CoordPixels[0/1]` and `script.bHasCoord = true`, exactly as the non-wheel branch does. No button parse for wheel. When `x`/`y` are omitted, leave `bHasCoord == false` (current behavior preserved — camera-zoom-style wheel that does not target a specific ImGui window still works). Simplest shape: hoist the coord parse so both branches share it, or duplicate the three coord lines into the wheel branch — pick the smaller diff.
2. No change needed in `AgentInput.cpp`: once `bHasCoord` is true the existing pre-switch `IssueImGuiMousePos` (`:343-349`) issues the synthetic `io.MousePos`, so `AddMouseWheelEvent` routes to the window under the target coords. Verify at execution that the same-frame ordering (pos event then wheel event, both in phase 0 / consumed by the next `NewFrame`) resolves the hovered window as intended — if ImGui needs the pos settled a frame earlier, add a one-frame pos-only settle phase before the wheel (mirrors `kClick`'s phase 0 stabilize). Prefer the no-settle path if the live check confirms it works.
3. `.claude/skills/agent-harness/SKILL.md` `mouse` entry (`:166-167`) — document that `x`/`y` are now honored for `action:"wheel"` to target a specific window's scroll region, and that omitting them scrolls via the camera-zoom sink / whatever window the last position targeted. Note it as the remedy for the `click` entry's "scrolled out of view" error (scroll the container into view by wheeling at its coords).

Decision surfaced by the residual — "or fail with a clear error when the cursor isn't over the game window": rejected as the primary approach. The KISS answer is to *give* the caller a way to aim the wheel (honor `x`/`y`), not to error. There is no reliable "is the OS cursor over the game window" signal on the synthetic path anyway (the overlay drives a normalized synthetic pos), and erroring would break the position-independent camera-zoom use. Left as a non-goal (Out of scope).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `CommandMouse` (`~:417-485`), the `kWheel` branch coord parse (primary change).
- `Engine/Source/Agent/AgentInput.cpp` — `AdvanceFrame` kMouse/`kWheel` (`:340-377`) and `IssueImGuiMousePos` (`:88-94`): read-only reference; edit only if step 2's live check shows a pos-settle phase is required.
- `Engine/Source/Agent/AgentInput.h` — `AgentScript` (`bHasCoord`/`f2CoordPixels`/`iWheelNotches`, `:62-64`): reference only, no layout change.
- `.claude/skills/agent-harness/SKILL.md` — `mouse` entry doc (`:166-167`).

## Out of scope

- Erroring when the cursor is off-window (rejected above — no reliable signal, would break camera-zoom wheel).
- Auto-scrolling a clipped target into view for the caller (the `AgentClickScrollClippedWidgets` Out-of-scope item; still a larger follow-up).
- Adding wheel-button/`button` param semantics (wheel has no button).
- Horizontal wheel (`AddMouseWheelEvent` X axis stays 0 — no horizontal-scroll consumer in the UI).
- The overlay/camera-zoom scroll accumulator path (`miSyntheticScrollAccumulator`) — already works headless, untouched.

## Acceptance criteria

- Live harness: with a scrollable menu open and the real OS cursor parked outside the game window, `mouse action:"wheel" x:<cx> y:<cy> notches:-3` where (cx,cy) is inside the scroll container actually scrolls that container (verified via `describe_ui` rects shifting / a previously `visible:false` item becoming `visible:true`) — no `SetCursorPos` workaround needed.
- `mouse action:"wheel" notches:N` with no `x`/`y` still behaves as today (camera-zoom sink unaffected; no throw).

## Notes

- **Invariant exposure.** Client-only automation path (`AgentInput`/`AgentCommandsClient`, whole-file `BT_CLIENT`). No wire/CRC/`kiVersion`/determinism change; not an allocation-tracked concern (reuses existing event-issue calls, no new formatting/allocation). No `AgentScript` layout change (fields already exist).
- **Staged grill decision (one open item).** Confirm the KISS shape: honor optional `x`/`y` and keep the no-coords wheel working (recommended), vs. requiring `x`/`y` for wheel. Recommended: keep optional (backward-compatible; camera-zoom wheel and Ctrl+wheel-style tests that don't target a window still work). Also confirm whether step 2 needs the extra pos-settle frame — resolve by the live check during execution, not by pre-decision.
- **Concurrent-session caution.** `AgentInput.cpp`/`AgentCommandsClient.cpp` are the shared files of the two `[CLAIMED]` sibling Agent plans; expect line drift and re-anchor on symbols (`CommandMouse`, the `kWheel` case), never line numbers.
