# Minimized-Skip Stale Comment Fixes

## Context

Two pre-existing doc/comment inaccuracies about the client render-loop-skip and the server tick-wait, surfaced during the `Graphics/MinimizedRenderLoopThrottle.md` session (step-10 audits + docs audit). Both are in files that session did not modify, so they were routed here per the "don't touch unrelated code" directive rather than folded into the throttle change. The throttle work made the minimized-skip control flow more prominent, which is what exposed item 1. Comment-only — no code behavior, CRC, wire, `kiVersion`, or build-affinity exposure.

## Design

### Item 1 — `AgentInput::AdvanceFrame` comment misdescribes minimized control flow
`Engine/Source/Agent/AgentInput.h:90-92`. The comment justifies "no minimized fast-fail is needed" with: *"The main loop runs ImGui + scripts even while the window is minimized (GameBase::Render / ImGuiManager::Prepare are unconditional; only swapchain recreation defers at a 0x0 extent)"*.

The **conclusion is correct** — `AdvanceFrame` runs unconditionally at the client drain point (`Main.cpp`, loop top, above `Render`), so it needs no minimized guard. But the **supporting claim is wrong**: `ImGuiManager::Prepare` is reached only through `Graphics::RenderMainPresentAcquire` (`Graphics.cpp:250`), which `GameBase::Render` calls *after* the swapchain-recreate-deferred skip branch's early `return` (`GameBase.cpp`, the `mbSwapchainRecreateDeferred || meDestroyType >= kSwapchain` branch). So while minimized/deferred, `ImGuiManager::Prepare` does **not** run. `GameBase::Render` is entered unconditionally, but its render/ImGui body (including `Prepare`) is skipped.

Fix: correct the parenthetical so it rests on the true invariant — `AdvanceFrame` runs at the drain point above `Render`, independent of whether the minimized skip fires — and stop claiming `ImGuiManager::Prepare` runs while minimized. Keep it one concise sentence; do not enumerate line numbers that will drift.

### Item 2 — server tick-wait spin margin doc/code drift (rider)
`Engine/Source/Network/Server/CLAUDE.md:10` (ServerSessionBase key-class line) says the waitable timer *"sleeps until ~2ms before target, then spins to precision"*. The code uses `kSpinMarginNs = 500'000ns` (~0.5 ms), not ~2 ms (`ServerSessionBase.cpp:29`). Fix the doc figure to ~0.5 ms (or "sub-millisecond"). The `WaitForTick` inline comment at `ServerSessionBase.cpp:28` ("waitable timer for the bulk, then spin-wait for precision") is accurate and needs no change.

## Critical files
- `Engine/Source/Agent/AgentInput.h` — the `AdvanceFrame` declaration comment (`:88-93`). Read-only cross-check: `GameBase.cpp` `Render` skip-branch `return`, `Graphics.cpp:250` (`ImGuiManager::Prepare` call site), `Main.cpp` drain point.
- `Engine/Source/Network/Server/CLAUDE.md` — the ServerSessionBase key-class bullet (`:10`). Read-only cross-check: `ServerSessionBase.cpp:29` (`kSpinMarginNs`).

## Out of scope
- No code-behavior change to `AgentInput`, `GameBase::Render`, or `ServerSessionBase` — comments/docs only.
- The `AgentInput` minimized fast-fail decision itself stands (verified correct); this only fixes the comment's *reasoning*, not the code.
- No other `AgentInput` / server-timing comments — only the two cited inaccuracies.

## Acceptance criteria
- `AgentInput.h`'s `AdvanceFrame` comment no longer claims `ImGuiManager::Prepare` runs while minimized; the retained justification is the unconditional drain-point call above `Render`.
- `Network/Server/CLAUDE.md` states the spin margin as ~0.5 ms (matching `kSpinMarginNs`), not ~2 ms.

## Notes
- **Invariant exposure: none** — comment/doc text only, two single-line edits in two files, no compile impact.
