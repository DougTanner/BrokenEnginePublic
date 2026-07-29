<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-28T19:24:14.882Z","dependsOn":[]} -->
# Restore Agent Client Before Debug Auto-Connect

## Context

The canonical BrokenEngineSandbox launch recipe passes a nonzero `--agent-port` to the client and then states that Debug/Profile clients auto-connect (`Projects/BrokenEngineSandbox/Documents/AgentHarness.md:21-31`). Agent-mode startup deliberately minimizes the client (`Engine/Source/Main.cpp:302-303`). While that surface remains minimized, `GameBase::Render` returns from the deferred swapchain-recreate path (`Engine/Source/GameBase.cpp:568-625`) before `MainMenuScreen::Render` can run its Debug/Profile auto-connect block (`Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp:94-102`). The documented sequence can therefore leave the server at `clientCount:0` and the client on the main menu indefinitely without reporting a connection failure.

This contradiction was exposed during the out-of-scope harness setup retry for acceptance criterion C5 of `ClientDesyncDisconnectFallbackAssertions`; C5 passed after the client was restored, so this is not an acceptance failure in that change. The failed launch logged `Swapchain recreate deferred: surface extent 0 x 0 (window off-screen/minimized)` with no connection event (`Temp/desync-response-client-20260728T190522199Z.log:95-97`). In the corrected retry, sending `window_state {"minimized":false}` immediately after client ping returned a settled `1600x904` extent, after which Debug auto-connect raised server `clientCount` to `1` without a click (`Temp/desync-response-evidence.json:103-167`).

## Design

1. In the `## Launch` recipe of `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, replace the unconditional Debug/Profile auto-connect statement with an ordered agent-mode instruction: after the bounded server and client ping loops succeed, send the client command `{"cmd":"window_state","params":{"minimized":false}}` on port `27101`, require a successful result with `minimized:false`, and keep the client visible before relying on Debug/Profile UI-driven auto-connect.
2. Include the concrete request using the recipe's existing `$AgentHarness` and `$Owner` values so the restore step is directly executable: pipe the JSON request to `& $AgentHarness --owner $Owner --port 27101 -`.
3. Preserve the Release instruction to click `LOCAL SERVER`, the exit-autosave/reset caveat, and the existing canonical requirement that server `status.clientCount` increase. Do not change generic harness lifecycle policy or runtime behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — `## Launch` connection recipe immediately after the launch/ping sequence; the only planned edit.
- `Engine/Source/Main.cpp` — agent-port minimization evidence at `:302-303`; read-only.
- `Engine/Source/GameBase.cpp` — minimized render early return at `:568-625`; read-only.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp` — UI-rendered auto-connect at `:94-102`; read-only.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — settled no-activate restore behavior in `CommandWindowState` at `:781-845`; read-only.

## Out of scope

- Changing agent-mode minimization, render-loop behavior, Debug/Profile auto-connect, discovery, connection timing, or any other runtime code.
- Changing Release connection behavior, generic AgentHarness ownership/lifecycle rules, command schemas, readiness helpers, or unrelated verification recipes.
- Adding commands, compatibility behavior, or unit tests.

## Risk tier and invariants

Change Workflow Tier 1: documentation-only correction with no public signature or runtime invariant exposure. The documented order must preserve no-activation agent startup, restore the client through the existing `window_state` command, and require observable connection state before scenario setup. No determinism/CRC, wire/protocol, serialization, replay, threading, allocation, shader, build, or client/server-affinity contract changes.

## Acceptance criteria

- The BrokenEngineSandbox launch recipe explicitly restores an agent-mode client after successful ping and before relying on Debug/Profile auto-connect.
- The documented request is valid against the current `window_state` schema and requires `minimized:false`; the recipe does not instruct a `LOCAL SERVER` click for Debug/Profile builds.
- The recipe retains the Release click instruction, exit-autosave/reset caveat, and canonical `status.clientCount` increase check.
- The implementation diff is confined to the launch/connection recipe in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`; no source files change, and `git diff --check` passes.

## Notes

No runtime verification or compilation is required for this documentation-only correction. Static comparison against the launch, render, auto-connect, and `window_state` source paths is decisive; the retained runtime artifacts above document the original failure and successful ordering.
