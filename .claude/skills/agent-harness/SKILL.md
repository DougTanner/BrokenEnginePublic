---
name: agent-harness
description: Drive the Broken Engine client/server for automated verification — launch the two executables with an agent command channel, then send JSON commands via AgentCli to control the sim, drive the UI, and read back scene/UI/log/screenshot state. Use whenever you need to run the game to verify a change end-to-end, set up a test scenario (spawn players, inject StatusChanges), drive menus/HUD, capture a screenshot, describe the rendered scene, or run a replay determinism check. ALSO use whenever a plan's Verification section asks to launch, drive, query, or screenshot the client or server.
allowed-tools: [Bash, PowerShell]
---

# Agent Interaction Harness

The harness lets an agent run and control the running game headlessly. Each executable (`--agent-port N`) opens a loopback TCP JSON command channel on `127.0.0.1:N`. `AgentCli.exe` sends one length-prefixed JSON request and prints the JSON response. The **server** is a headless dev instance of the authoritative sim; the **client** renders and drives the UI. (Production servers are Azure-hosted — a local "Server" is only a dev instance.)

Convention: **server on port 27100, client on port 27101.**

## Launching

Launch each executable in the background so it keeps running across turns. Use `run_in_background: true`; do not poll or sleep. All configs land in one flat folder (`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/`) with the config as a filename suffix — `BrokenEngineSandbox.Debug.exe`, `BrokenEngineSandboxServer.Debug.exe`, `.Profile.exe` etc.; use the suffix matching the config you built.

Launch args (all optional): `--agent-port N` (opens the channel; required to drive it), `--log-file <path>` (mirror the log ring to a file; write it under `$ROOT\Temp\` — gitignored, but create the directory first: the sink soft-fails if the parent is missing), `--windowed WxH` (force a windowed client size; overrides fullscreen only at read-time, never mutates the saved setting). `1600x900` is a good windowed size — small enough to see, large enough for UI hit-testing.

```powershell
# run_in_background: true — start the server (headless), then the client windowed.
& "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\BrokenEngineSandboxServer.Debug.exe" --agent-port 27100 --log-file "$ROOT\Temp\server-agent.log"
& "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\BrokenEngineSandbox.Debug.exe" --agent-port 27101 --windowed 1600x900 --log-file "$ROOT\Temp\client-agent.log"
```

Bad `--agent-port` (outside `[1,65535]`) aborts startup. A mangled `--windowed` value is rejected and logged.

### Relaunch discipline

Before rebuilding/relinking, **send `quit` to any running instance** (the server autosaves on exit) — a live `.exe` also holds its file locked and blocks the linker. The single-instance mutex is server-only: launching a second server with `--agent-port` set logs a `kError` and exits cleanly instead of popping a modal dialog (a duplicate **client** fails fast on the agent bind). So a stale instance means your new one exits — always `quit` first, or confirm the port is free.

## AgentCli invocation

Build it via the `/compile` skill (AgentCli section) — separate `Tools/AgentCli/Platforms/VisualStudio2026/AgentCli.sln`, output `AgentCli.exe`.

**Prefer stdin mode** (trailing `-`): it sidesteps PowerShell/Bash quoting of the JSON. Piping the request avoids escaping quotes in the shell.

```bash
echo '{"cmd":"status"}' | "$ROOT/Tools/AgentCli/Platforms/VisualStudio2026/Output/AgentCli.exe" --port 27100 -
```

Argument form also works: `AgentCli.exe --port 27100 '{"cmd":"ping"}'`. Optional `--timeout-ms N` (default 15000, max 600000) bounds the response wait — raise it for deferred client commands (screenshots, scripted input) that resolve over several frames.

**Exit codes:** `0` = response parsed and `"ok":true`; `2` = parsed and `"ok":false` (a command error — read `.error`); `1` = transport/usage failure (connect failed, timeout, malformed args — message on stderr). Always read stdout (the full JSON) regardless of exit code.

## Request / response envelope

Request: `{"cmd":"<name>","params":{...},"id":<optional>}`. `params` may be omitted when a command takes none; `id` (any JSON) is echoed back for correlation.

Response: `{"id":<echoed|null>,"ok":true,"result":{...}}` on success, or `{"id":...,"ok":false,"error":"<message>"}` on failure. **All per-command fields below are the contents of `result`.**

Params are a trust boundary — a wrong type or missing required field returns `ok:false` with a descriptive error, never a crash.

---

## Command reference

Commands split by build: **shared** (both), **server-only** (`BT_SERVER`), **client-only** (`BT_CLIENT`). Sending a server command to the client (or vice versa) returns `error:"unknown command"`.

### Shared

**`ping`** — liveness + build id. No params.
`result`: `{"build":"server"|"client","tick":<int, -1 if no game yet>}`

**`quit`** — request a clean shutdown (server autosaves). No params. `result`: `{}`

**`get_logs`** — tail the in-memory log ring.
params: `{"count"?:64,"pattern"?:"<ECMAScript regex>","category"?:"<name>"}`. With `pattern`, the whole buffer is scanned and the last `count` *matching* lines (chronological) are returned. Omit `category` for the cross-category agent ring (most recent lines across all categories).
`result`: `{"lines":["...","..."]}`

**`set_log_level`** — raise/lower runtime log verbosity.
params: `{"level":"Verbose|Debug|Info|Warning|Error"(required),"category"?:"<name>"}`. A level below a category's compile-time floor is clamped up (it could never emit otherwise); the effective level is reported back.
`result`: `{"effective":"Info"}` (single category) or `{"effective":{"<cat>":"Info",...}}` (all).

### Server-only

**`status`** — sim snapshot.
`result`: `{"tick","paused":bool,"recording":bool,"replaying":bool,"clientCount":int,"activeCoords":[[x,y],...],"nextGlobalId"}`

**`pause`** — pause/unpause the sim (mirrors the client pause request).
params: `{"paused":bool(required)}`. `result`: `{"paused":bool}`

**`timescale`** — step sim speed up/down one notch (shared with the client timespeed path; broadcasts to clients).
params: `{"faster":bool(required)}`. `result`: `{"numerator","denominator"}`

**`save`** — write a save. params: `{"file"?:"<bare filename>"}` (default quicksave name; the name is validated — no separators, `..`, or `:` — and lands in appdata). `result`: `{"file":"<name>"}`

**`load`** — load a save. params: `{"file"?}`. A corrupt/truncated file falls back to a fresh game. `result`: `{"file","resetToFresh":bool}`

**`reset`** — reset to a fresh game (resets the server fleet manager). No params. `result`: `{}`

**`replay_record`** — start/stop replay recording. params: `{"start":bool(required)}`. `result`: `{"pending":bool}` (whether a state transition was scheduled). **Errors** (`ok:false`) on a build with `kbDebugInput` compiled out.

**`replay_play`** — start (or cancel) replay playback; doubles as a determinism check (per-tick CRC validation — watch `get_logs`). No params. `result`: `{"pending":true}`. **Errors** without `kbDebugInput`.

**`query_frame`** — per-collection counts for one grid cell.
params: `{"coord":[x,y](required)}`. Errors if the coord has no loaded/ready frame.
`result`: `{"players":{"count"},"spaceships":{"count"},"missiles":{"count"},"blasters":{"count"},"targets":{"count"}}`

**`query_players`** — player rows for one cell.
params: `{"coord":[x,y](required),"offset"?:0,"limit"?:256}`.
`result`: `{"total","players":[{"index","uuid","globalId","pos":[x,y,z],"dir":[x,y,z],"armor","shield","flags":<int bitfield>,"alignment"}]}`

**`query_collection`** — rows for a non-player collection.
params: `{"coord":[x,y](required),"collection":"spaceships|missiles|blasters|targets"(required),"offset"?:0,"limit"?:256}`.
`result`: `{"total","items":[...]}` — item fields vary: spaceships `{index,pos,dir,health,alignment}`; missiles/blasters `{index,pos,dir,alignment}`; targets `{index,uuid,pos,flags,alignment}`.

**`inject_status_changes`** — queue StatusChanges into the next tick's inputs.
params: `{"changes":[<change>,...](required)}`. Each `<change>`: `{"coord":[x,y](active),"type":"SpawnPlayer|DestroyPlayer|UpdatePlayer|UpdateFleet",...}`:
- `SpawnPlayer`: `{"isFlagship"?:false,"fleetWantedCoord"?:<coord>}` (id minted server-side)
- `DestroyPlayer`: `{"playerUuid"(required)}`
- `UpdatePlayer`: `{"playerUuid"(required),"useMissiles"?:false,"navigationDelay"?:60}` (delay clamped [0,60])
- `UpdateFleet`: `{"playerUuid"(required),"isFlagship"?:false,"fleetWantedCoord"(required)}`

All entries validate before any is queued (a malformed entry aborts the whole batch). `coord` must be active. **Errors while any client is waiting for spawn, or during replay playback.**
`result`: `{"injected":int,"globalIds":[...](newly minted),"deferred":bool}`. `deferred:true` means the server is paused, so the changes wait for the next unpaused tick.

**`spawn_players`** — bulk-spawn N players at one active cell.
params: `{"coord":[x,y](active,required),"count":int [0,256](required),"isFlagship"?:false}`. Same waiting-for-spawn / replay rejections as inject.
`result`: `{"injected","globalIds":[...],"deferred":bool}`

### Client-only

Client input/capture commands are **deferred**: they complete over several frames. Give AgentCli a generous `--timeout-ms`. A second scripted-input command while one is still running returns `error:"busy"`.

**`screenshot`** — capture the live window to a downscaled image.
params: `{"path"?,"maxWidth"?:1568,"format"?:"jpg"(default)|"png","quality"?:80 [1,100]}`. **Errors** if `kbScreenshots` is compiled out. `result`: capture info (saved path etc.).

**`dump_render_target`** — read back an offscreen render target.
params: `{"name":"<rt name>"(required),"index"?:0,"channel"?:0,"path"?,"raw"?:false}`. Single-channel → normalized grayscale PNG; 4×8-bit → direct PNG; `raw:true` → `.bin`. Unknown name / bad index errors synchronously. **Errors** without `kbScreenshots`.

**`describe_ui`** — structured dump of the last completed ImGui frame + game UI state.
No params. `result`: `{"uiState":"kNone|kGraphicsSettings|kModal|kPause|kSound|kTweaks","gameFlags":["kPaused",...],"framebuffer":[w,h],"mouse":[x,y],"windows":[{"name","rect":[x,y,w,h],"focused"}],"items":[{"label","window","rect","disabled","checked","inputable","hovered"}]}`. Only labeled (addressable) items appear.

**`click`** — click a widget by label.
params: `{"label"(required),"window"?,"timeoutFrames"?:120,"describeUiAfter"?:true}`. Stabilizes the target rect, then presses/releases left-mouse at its center. `result`: `{"found":true,"enabled":bool,"ui"?:{<describe_ui>}}`. Not-found / ambiguous errors list candidate labels.

**`hover`** — move to and hold on a labeled widget, then dump the UI.
params: `{"label"(required),"window"?,"holdFrames"?:2}`. `result`: `{"found","enabled","ui":{...}}`

**`set_slider`** — set an ImGui slider to a value (Ctrl+Click → type → Enter).
params: `{"label"(required),"window"?,"value":<number>(required)}`. `result`: `{"found","enabled"}`

**`key`** — press+release a named key (drives KeyboardPressed edges for game bindings).
params: `{"key":"<name>"(required),"holdFrames"?:1}`. Names: single letters/digits, `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`, `F1`–`F24`. `result`: `{"ok":true}`

**`mouse`** — raw pixel-coord mouse action (world clicks, unlabeled targets, camera zoom).
params: `{"action"?:"move"(default)|"down"|"up"|"click"|"wheel","x","y","button"?:"left"|"right"|"middle","notches"?:1}`. `x`/`y` required unless `action:"wheel"` (which uses `notches`). `result`: `{"ok":true}`

**`describe_scene`** — structured view of the rendered scene (the world-perception command; pair with `screenshot` for pixels and `describe_ui` for widgets).
params: `{"includeUnits"?:true,"maxUnits"?:200}`. Only units inside the camera visible area are listed; `truncated:true` when the cap is hit. Unit `world`/`screen` come from the committed sim snapshot (~2 ticks / ~62 ms behind the drawn pixels) — treat as sim-state view, don't expect pixel-exact match against a same-moment `screenshot`.
`result`:
```jsonc
{"camera":{"eye":[x,y,z],"visibleArea":[minX,maxY,maxX,minY],"lod":1},
 "uiState":"kNone","gameFlags":["kPaused"],"tick":12345,
 "clientGridCoord":[0,0],"subscribedCoords":[[0,0],[1,0]],
 "fleets":[{"index":0,"focused":true,"members":[{"globalPlayerId":42,"alive":true}]}],
 "units":[{"type":"player","globalId":42,"world":[x,y,z],"screen":[px,py],
           "armor":80.0,"shield":50.0,"alignment":1,"flags":["kUseMissiles"]},
          {"type":"spaceship","world":[x,y,z],"screen":[px,py],
           "health":30.0,"alignment":2,"flags":["kExploding"]}],
 "counts":{"players":3,"spaceships":34,"missiles":3,"blasters":57,"targets":2},
 "islands":[{"coord":[0,0],"center":[x,y],"rotation":0.7,"footprint":[fx,fy]}],
 "truncated":false}
```
Only the focused fleet exposes a `members` list; spaceship units carry no id (`globalId` is player-only). `footprint` is omitted when the client render-query cache is not yet built. `counts` are cell-wide totals across subscribed coords (compare against server `query_frame` — allow small drift, the client trails a few ticks); `units` are visible-area-filtered, so unit-list length ≠ counts. Edge units may project a few tens of pixels outside the framebuffer (world-space bbox filter vs perspective projection) — treat near-boundary screen coords with slack.

---

## Canonical workflow

1. **Scenario setup (server):** `reset` → `spawn_players`/`inject_status_changes` at an active coord → confirm with `query_players`/`query_frame`. Check `status` for `activeCoords` to pick a valid coord.
2. **Connect the client:** launch it, then `click "LOCAL SERVER"` on the main menu (discovers localhost) to join the running server.
3. **Drive the UI:** `click`/`key`/`set_slider`/`mouse`/`hover`; verify addressability first with `describe_ui`.
4. **Verify:** `describe_scene` (world state, unit screen positions), `screenshot` (pixels), `describe_ui` (widgets), `get_logs` (events, warnings). Client `describe_scene` counts should match the sum of server `query_frame` counts over the subscribed coords (equality per coord only when a single coord is subscribed; allow a few ticks of drift).
5. **Determinism check:** `replay_record {start:true}` → let it run → `replay_record {start:false}`, then `replay_play`; the resim validates a per-tick CRC — watch `get_logs` for desync/checksum warnings.

## Caveats

- **Do not drive a connected client's input requests while the server is paused.** Client requests drained during a pause are currently wiped by `BuildFrameInputs` before consumption (tracked by `Documents/Plans/Network/ServerPauseAndResetSemantics.md`, not yet landed) — they are silently lost. Pause is fine for server-side inspection/injection; just don't feed the client sim while paused.
- **Injection is deferred while paused** (`"deferred":true`) and **rejected while any client is waiting for spawn** (or during replay) — spawn assignment is by snapshot diff, so an injected spawn landing on a waiting client's tick would mis-assign. Set up scenarios before clients join, or after their spawn completes.
- **Focus/injection interleaving:** client focus state is logged — if a scripted action depends on which window/fleet is focused, read `get_logs` (or `describe_ui` `focused` flags) to confirm state before acting.
- **`busy`:** only one scripted-input command runs at a time; a second returns `error:"busy"`. Wait for the first to resolve (its deferred response) before the next.
- **Replay commands require `kbDebugInput`** — they error on builds without it.
- **Port in use:** if a command can't connect (AgentCli exit 1, "connect failed"), a prior instance is likely still running and holding the port/mutex. `quit` it (or confirm it exited) before relaunching — a duplicate launch exits itself.
