---
name: agent-harness
description: Drive the Broken Engine client/server for automated verification — launch the two executables with an agent command channel, then send JSON commands via AgentCli to control the sim, drive the UI, and read back scene/UI/log/screenshot state. Use whenever you need to run the game to verify a change end-to-end, set up a test scenario (spawn players, inject StatusChanges), drive menus/HUD, capture a screenshot, describe the rendered scene, or run a replay determinism check. ALSO use whenever a plan's Verification section asks to launch, drive, query, or screenshot the client or server.
allowed-tools: [Bash, PowerShell]
---

# Agent Interaction Harness

The harness lets an agent run and control the running game headlessly. Each executable (`--agent-port N`) opens a loopback TCP JSON command channel on `127.0.0.1:N`. AgentCli sends one length-prefixed JSON request and prints the JSON response. The **server** is a headless dev instance of the authoritative sim; the **client** renders and drives the UI. (Production servers are Azure-hosted — a local "Server" is only a dev instance.)

Convention: **server on port 27100, client on port 27101.** `$ROOT` below is the absolute repo root.

## Claiming the harness (do this first)

Only one Claude session may drive the harness at a time — ports are fixed by convention, so an unclaimed second session would reset scenarios, drive UI, or `quit` exes another session is mid-test on. The claim is an advisory lock file, same pattern as the msbuild.sh build locks. The lock lives at a fixed user-global path (`$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock`) so every git worktree claims the *same* file. This literal is duplicated in `Tools/AgentCli/AgentCli.cpp` `TouchHarnessLock()` — the two MUST stay identical.

**Claim** before the first launch or command of your session (atomic create — fails if another session holds it):

```bash
mkdir -p "$LOCALAPPDATA/BrokenEngineHarness" && ( set -o noclobber; printf 'session: %s\nclaimed: %s\n' "<short task label>" "$(date -Iseconds)" > "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock" ) && echo claimed
```

Report the claim line (`claimed`) verbatim in your final report, alongside the release line below — a report missing either is an incomplete harness session.

**Verify the heartbeat after claiming:** every AgentCli command touches the lock's mtime, so after your first launch send a `ping` (or any first command) and confirm the lock mtime advanced (`ls -l --full-time "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock"` — or `stat -c %y` — before and after; plain `ls -l` prints only minute resolution, so a healthy touch seconds after the claim reads as "did not advance" and triggers a spurious rebuild). If it did not advance, rebuild AgentCli via the `/compile` skill (AgentCli section) before proceeding, or your live claim will look dead to a steal within ~5 minutes. This check also catches any AgentCli whose build targets a different lock path (e.g. a stale binary not yet rebuilt): it touches a different file, so this lock's mtime won't advance and the check correctly says "rebuild."

**If the claim fails**, another session owns the harness. `cat` the lock. A lock mtime older than **~5 minutes** means the owner has issued no harness command for that long (dead session, crashed shell, or stale pre-touch AgentCli) — every live AgentCli command freshens the mtime, so age is direct evidence of owner liveness. The lock's `claimed:` content line records when the claim was *first* taken, not liveness — a `cat` showing an hours-old `claimed:` on a healthy long session is not grounds to steal; mtime is the sole staleness criterion. To steal a stale lock: send `quit` to both agent ports (server autosaves), fall back to `taskkill` only if `quit` gets no response, verify `tasklist | grep -i BrokenEngineSandbox` is empty, then take the lock over by an **atomic rename** — run the whole takeover (mv → rm the moved file → noclobber re-claim) as ONE shell invocation so `$$` stays consistent (splitting it across tool calls gives each a fresh pid and the later `rm` misses the file): `mv "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock" "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.steal.$$" && rm "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.steal.$$" && ( set -o noclobber; printf 'session: %s\nclaimed: %s\n' "<label>" "$(date -Iseconds)" > "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock" ) && echo claimed`. Rename is atomic, so exactly one stealer wins; if `mv` fails (file already gone) another session won the steal — back off and re-evaluate, do NOT proceed (do not bare-`rm` the live lock — two concurrent stealers would both `rm` and re-create, defeating noclobber). A non-stale lock (mtime within ~5 min) is NOT stealable, full stop — you may not `quit` or kill its processes to qualify a steal. Otherwise wait and retry later, or report the verification as blocked-by-harness-contention in your residuals; never drive or `quit` instances you didn't claim.

**Release** at the end of your session — this is a mandatory checklist, not prose:

1. `quit` both exes (the server autosaves on exit).
2. Verify nothing lingers: `tasklist | grep -i BrokenEngineSandbox` is empty. Mandatory after ANY crashed, reaped, or abandoned launch attempt during the session — a detached orphan from a failed launch survives your quits.
3. `rm "$LOCALAPPDATA/BrokenEngineHarness/agent-harness.lock" && echo released` — and put the `released` line verbatim in your final report. "Quit cleanly" prose is not evidence of release; the echoed line is.

Hold the claim across your whole verification, including rebuild/relaunch cycles — release only when you are done with the harness entirely.

## Launching

Launch each executable in the background so it keeps running across turns. Use `run_in_background: true`; do not poll or sleep. All configs land in one flat folder (`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/`) with the config as a filename suffix — `BrokenEngineSandbox.Debug.exe`, `BrokenEngineSandboxServer.Debug.exe`, `.Profile.exe` etc.; use the suffix matching the config you built.

Launch args (all optional): `--agent-port N` (opens the channel; required to drive it), `--log-file <path>` (mirror the log ring to a file; write it under `$ROOT\Temp\` — gitignored, but create the directory first: the sink soft-fails if the parent is missing), `--windowed WxH` (force a windowed client size; overrides fullscreen only at read-time, never mutates the saved setting). `1600x900` is a good windowed size — small enough to see, large enough for UI hit-testing.

Both executables start minimized without activation when `--agent-port` is set, so launches stay in the background. Client capture commands temporarily restore the client without activation, capture from the live swapchain, and re-minimize it before responding.

**Windowed vs fullscreen:** windowed for sim/logic/scenario verification (unintrusive); for UI sizing/readability work, launch **without `--windowed`** — the persisted fullscreen setting applies (native res). Fonts are fixed pixel sizes (`kfMenuUiScale` constants; `FontSizeBase` is not resolution-derived), so a half-res window shows text 2× larger relative to screen than real 4K fullscreen — any 16:9 window matches layout/aspect, only native res matches text proportions. Window chrome shifts the framebuffer a few px from the requested size (1600x900 → 1600×904 observed). An `--agent-port` client suppresses all physical input (mouse buttons/wheel included, ImGui feed included), fullscreen or not, so real clicks/scrolls never interfere with scripted input (see the caveat below).

```powershell
# run_in_background: true — start the server (headless), then the client windowed.
& "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\BrokenEngineSandboxServer.Debug.exe" --agent-port 27100 --log-file "$ROOT\Temp\server-agent.log"
& "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\BrokenEngineSandbox.Debug.exe" --agent-port 27101 --windowed 1600x900 --log-file "$ROOT\Temp\client-agent.log"
```

Bad `--agent-port` (outside `[1,65535]`) aborts startup. A mangled `--windowed` value is rejected and logged.

**Debug/Profile clients auto-connect** (`kbAutoConnect`, game `Pch.h`): seconds after launch the client discovers the local server and joins — it never idles at the main menu; `click "LOCAL SERVER"` is only needed on Release. The server auto-loads its exit autosave on boot, so a fresh launch resumes the previous game — `reset` when you need a clean scenario.

### Relaunch discipline

Before rebuilding/relinking, **send `quit` to any running instance** (the server autosaves on exit) — a live `.exe` also holds its file locked and blocks the linker. The single-instance mutex is server-only: launching a second server with `--agent-port` set logs a `kError` and exits cleanly instead of popping a modal dialog (a duplicate **client** fails fast on the agent bind). So a stale instance means your new one exits — always `quit` first, or confirm the port is free.

## AgentCli invocation

Build it via the `/compile` skill (AgentCli section) — separate `Tools/AgentCli/Platforms/VisualStudio2026/AgentCli.sln`. Output is config-suffixed like the game exes: **`AgentCli.Debug.exe`** (Debug), `AgentCli.exe` (Release) — use the suffix matching what you built; a stale unsuffixed exe may sit beside a fresh Debug one.

**Prefer stdin mode** (trailing `-`): it sidesteps PowerShell/Bash quoting of the JSON. Piping the request avoids escaping quotes in the shell. **Windows paths inside JSON:** use forward slashes (`C:/...` — accepted everywhere) — Git Bash `echo` mangles `\\` into `\`, producing invalid JSON escapes ("malformed JSON").

```bash
echo '{"cmd":"status"}' | "$ROOT/Tools/AgentCli/Platforms/VisualStudio2026/Output/AgentCli.Debug.exe" --port 27100 -
```

Argument form also works: `AgentCli.Debug.exe --port 27100 '{"cmd":"ping"}'`. Optional `--timeout-ms N` (default 15000, max 600000) bounds the response wait — raise it for deferred client commands (screenshots, scripted input) that resolve over several frames.

**Exit codes:** `0` = response parsed and `"ok":true`; `2` = parsed and `"ok":false` (a command error — read `.error`); `1` = transport/usage failure (connect failed, timeout, malformed args — message on stderr). Always read stdout (the full JSON) regardless of exit code.

## Request / response envelope

Request: `{"cmd":"<name>","params":{...},"id":<optional>}`. `params` may be omitted when a command takes none; `id` (any JSON) is echoed back for correlation.

Response: `{"id":<echoed|null>,"ok":true,"result":{...}}` on success, or `{"id":...,"ok":false,"error":"<message>"}` on failure. **All per-command fields below are the contents of `result`.**

Params are a trust boundary — a wrong type or missing required field returns `ok:false` with a descriptive error, never a crash. Command validation throws are a designed error path (`common::ScopedExpectedThrows` around dispatch in `AgentCommandServer::Drain`) — no crash-diagnostic callstack floods the log ring and no sim stall.

---

## Command reference

Commands split by build: **shared** (both), **server-only** (`BT_SERVER`), **client-only** (`BT_CLIENT`). Sending a server command to the client (or vice versa) returns `error:"unknown command"`.

### Shared

**`ping`** — liveness + build id. No params.
`result`: `{"build":"server"|"client","tick":<int, -1 if no game yet>}`

**`quit`** — request a clean shutdown (server autosaves). No params. `result`: `{}`

**`get_logs`** — tail the in-memory log ring.
params: `{"count"?:64,"pattern"?:"<ECMAScript regex>","category"?:"<name>"}`. With `pattern`, the whole buffer is scanned and the last `count` *matching* lines (chronological) are returned. Matching is case-sensitive with no flag (`(?i)` → `"invalid regex pattern"`) — use `[Dd]esync`-style classes. Omit `category` for the cross-category agent ring (most recent lines across all categories).
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

**Hold on an empty server**: pause and timescale persist with zero clients — the disconnect pass (runs every update, even paused) reverts them to running/1× only when the last client disconnects, not while the server is simply empty. So `deferred:true` injection is reachable headless (pause, then inject). A client can connect to a paused server and receives full state (renders the frozen world — `kPaused` has no wire broadcast, so there's no explicit pause signal); fleet/player spawning is never automatic — it needs the HUD `[+]` buttons (drive via `click`), and the resulting spawn completes only on an unpaused tick. An agent-held pause persists when a client connects — the client can lift it via its own pause request.

**`save`** — write a save. params: `{"file"?:"<bare filename>"}` (default quicksave name; the name is validated — no separators, `..`, or `:` — and lands in appdata). `result`: `{"file":"<name>"}`

**`load`** — load a save. params: `{"file"?}`. A corrupt/truncated/missing file falls back to a fresh game (`resetToFresh:true`, still `ok:true`). `result`: `{"file","resetToFresh":bool}`

**`reset`** — reset to a fresh game (resets the server fleet manager). No params. `result`: `{}`

**`replay_record`** — start/stop replay recording. params: `{"start":bool(required)}`. `result`: `{"pending":bool}` (whether a state transition was scheduled). **Errors** (`ok:false`) on a build with `kbDebugInput` compiled out.

**`replay_play`** — start (or cancel) replay playback; doubles as a determinism check (per-tick CRC validation — watch `get_logs`; validation is silent, only mismatches log). No params. `result`: `{"pending":true}`. **Errors** without `kbDebugInput`. **Playback loops forever** ("End replay N, looping" at `kDebug`) — `status.replaying` never clears on its own; send `replay_play` again to cancel.

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

All entries validate before any is queued (a malformed entry aborts the whole batch). `coord` must be active. **Errors during replay playback.** While the server is paused or a client is waiting for spawn, changes are accepted and *held* (never dropped) until the next tick with no waiting client — spawn assignment is by snapshot diff, so this protects the waiting client's zip order.
`result`: `{"injected":int,"globalIds":[...](newly minted),"deferred":bool}`. `deferred:true` means the server is paused, so the changes wait for the next unpaused tick (`deferred` reflects pause only, not the waiting-client hold). A deferred batch landing on the first unpaused tick can log ONE self-healing `CrcValidateLoop sharedCrc mismatch` on a connected client (designed soft-desync recovery — resolves that tick; only a *repeating* mismatch signals a real desync).

**`spawn_players`** — bulk-spawn N players at one active cell.
params: `{"coord":[x,y](active,required),"count":int [0,256](required),"isFlagship"?:false}`. Same replay rejection / paused-and-waiting-client hold semantics as inject.
`result`: `{"injected","globalIds":[...],"deferred":bool}`

### Client-only

Client input/capture commands are **deferred**: they complete over several frames, and the channel serves one request at a time — a concurrent AgentCli call simply queues and waits (it does not error), so size `--timeout-ms` to cover any command already in flight.

**`screenshot`** — capture the live window to a downscaled image.
params: `{"path"?,"maxWidth"?:1568,"format"?:"jpg"(default)|"png","quality"?:80 [1,100]}`. Default `path` lands in `%LOCALAPPDATA%\Temp\Screenshots\agent_N.jpg`. If the client is minimized, the command restores it with `SW_SHOWNOACTIVATE`, waits for `ExtentSettled()`, captures, then re-minimizes it with `SW_SHOWMINNOACTIVE` before responding. An initially visible client remains visible. **Errors** if `kbScreenshots` is compiled out; an already-visible client fast-fails if swapchain recreation is deferred. `result`: capture info (saved path etc.).

**`resize`** — change the live client window/framebuffer size mid-session (no relaunch).
params: `{"width","height"}` (pixels, required). Validated to `[320x180, 16384x16384]`, then each rounded up to a multiple of 8; the applied extent reflects the OS/monitor window clamp (the swapchain copies the surface's current extent), so it can differ from the requested one. Rejects while the window is minimized. Drives the real HWND resize (WM_SIZE → swapchain recreate), keeping the window fully on its current monitor — a fixed top-left shifts left/up as it grows; a resize to exactly the monitor's pixel size lands at the monitor origin. Never toggles fullscreen or mutates the persisted setting. **Deferred**: completes once the swapchain has recreated (synchronous only if already at the target). `result`: `{"width","height"}` = the applied extent. Follow with `describe_ui` / `screenshot` to re-measure at the new size.

**`fullscreen`** — toggle the live client between borderless windowed-fullscreen and windowed mid-session (no relaunch).
params: `{"on":bool}` (required). Drives the engine's `WantedFullscreen()` → main-loop reconciliation style swap (`WS_POPUP` ↔ `WS_OVERLAPPEDWINDOW`) via an agent override → `SetupWindow` → swapchain recreate. Rejects while the window is minimized. Idempotent: an already-in-state request answers synchronously. Never mutates the persisted `gFullscreen` setting (in-memory override only, like `--windowed`). Windowed restore returns to the **launch** extent (the `--windowed` extent; default inset rect otherwise) — a mid-run `resize` is not preserved across a fullscreen round-trip, so re-issue `resize` afterward if you need the changed size back. **Deferred**: completes once the style bit matches and the swapchain has settled. `result`: `{"fullscreen","width","height"}` = the applied state/extent. Follow with `describe_ui` / `screenshot` to re-measure at the new mode.

**`window_state`** — minimize the live client window or restore it mid-session (no relaunch).
params: `{"minimized":bool}` (required). Drives `ShowWindow` on the real HWND: explicit minimize uses `SW_MINIMIZE`; restore uses `SW_SHOWNOACTIVATE` (a no-activate restore — never steals foreground focus, per the agent-mode convention). Idempotent: an already-in-state request answers synchronously. Never mutates the persisted `gFullscreen` setting or any `.bin`. This is the command that *produces* the minimized / recreate-deferred state `resize` / `fullscreen` reject; capture commands handle it by temporarily restoring and re-minimizing the client. **Deferred**: minimize completes once the window reports iconic; restore completes once it is no longer iconic **and** the swapchain has recreated at the restored extent (settled). `result`: `{"minimized"}`, plus `{"width","height"}` = the settled extent on restore. Follow a restore with `describe_ui` / `screenshot` to re-measure.

**`dump_render_target`** — read back an offscreen render target.
params: `{"name":"<rt name>"(required),"index"?:0,"channel"?:0,"path"?,"raw"?:false}`. Single-channel → normalized grayscale PNG (result reports `min`/`max`); 4×8-bit → direct PNG; float formats are not PNG-encodable — pass `raw:true` for `.bin` texels. Unknown name / bad index errors synchronously, and the unknown-name error lists every valid name — probe with a bogus name to discover them. A minimized client follows the same restore → `ExtentSettled()` → capture → re-minimize sequence as `screenshot`; an initially visible client remains visible. **Errors** without `kbScreenshots`; an already-visible client fast-fails if swapchain recreation is deferred.

**`describe_ui`** — structured dump of the last completed ImGui frame + game UI state.
No params. `result`: `{"uiState":"kNone|kGraphicsSettings|kModal|kPause|kSound|kTweaks","gameFlags":["kPaused",...],"framebuffer":[w,h],"mouse":[x,y],"windows":[{"name","rect":[x0,y0,x1,y1],"focused"}],"items":[{"label","window","rect","disabled","checked","inputable","hovered","visible"}]}`. Only labeled (addressable) items appear. Items carry no value field — verify slider/input changes via `checked`, a screenshot, or a game-side effect. Scroll-clipped items keep their off-screen rect (may exceed `framebuffer`); `visible:false` flags them.

**`click`** — click a widget by label.
params: `{"label"(required),"window"?,"timeoutFrames"?:120,"describeUiAfter"?:true}`. Stabilizes the target rect, then presses/releases left-mouse at its center. `result`: `{"found":true,"enabled":bool,"ui"?:{<describe_ui>}}`. Not-found / ambiguous errors list candidate labels. Clicking a scroll-clipped (off-screen) widget errors with "target not visible (scrolled out of view)" — scroll it into view first via `mouse action:"wheel"` at the container's coords (hover/set_slider too; scroll-clipped sliders/buttons instead usually error not-found — either way check `visible`/presence in describe_ui first).

**`hover`** — move to and hold on a labeled widget, then dump the UI.
params: `{"label"(required),"window"?,"holdFrames"?:2}`. `result`: `{"found","enabled","ui":{...}}`

**`set_slider`** — set an ImGui slider to a value (Ctrl+Click → type → Enter).
params: `{"label"(required),"window"?,"value":<number>(required)}`. `result`: `{"found","enabled"}`

**`key`** — press+release a named key (drives KeyboardPressed edges for game bindings).
params: `{"key":"<name>"(required),"holdFrames"?:1}`. Names: single letters/digits, `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`, `F1`–`F24`. `result`: `{"ok":true}`

**`mouse`** — raw pixel-coord mouse action (world clicks, unlabeled targets, camera zoom).
params: `{"action"?:"move"(default)|"down"|"up"|"click"|"wheel","x","y","button"?:"left"|"right"|"middle","notches"?:1}`. `x`/`y` required for move/down/up/click. For `action:"wheel"` `x`/`y` are OPTIONAL (pass both or neither — one alone throws): both route the ImGui wheel to the window under those coords (its scroll region); omit them and the ImGui wheel routes to whichever window the last position targeted (headless: the OS-cursor window). Wheel ALWAYS also feeds the game camera-zoom accumulator regardless of coords. Wheeling at a scroll container's coords is the remedy for `click`'s "scrolled out of view" (not visible) error — scroll the target in, then retry the click. `result`: `{"ok":true}`

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
2. **Connect the client:** launch it — Debug/Profile builds auto-connect (see Launching); on Release, `click "LOCAL SERVER"` on the main menu. Confirm with server `status` `clientCount`.
3. **Drive the UI:** `click`/`key`/`set_slider`/`mouse`/`hover`; verify addressability first with `describe_ui`.
4. **Verify:** `describe_scene` (world state, unit screen positions), `screenshot` (pixels), `describe_ui` (widgets), `get_logs` (events, warnings). Client `describe_scene` counts should match the sum of server `query_frame` counts over the subscribed coords (equality per coord only when a single coord is subscribed; allow a few ticks of drift — churning collections like blasters can differ by dozens over a multi-second query gap, stable ones like players should match exactly).
5. **Determinism check:** `replay_record {start:true}` → let it run → `replay_record {start:false}`, then `replay_play`; the resim validates a per-tick CRC — watch `get_logs` for desync/checksum warnings (silence = pass). Playback loops until cancelled with a second `replay_play`.

## Caveats

- **Don't drive client weapon-mode / fleet-nav update requests while the server is paused.** Those request queues are wiped by `BuildFrameInputs` before an unpaused tick can consume them (tracked by `Documents/Plans/Network/ServerPauseAndResetSemantics.md`, not yet landed) — silently lost. Spawn requests survive a pause (separate persist-until-served queue). Pause is fine for server-side inspection/injection.
- **Pause/timescale hold on an empty server** (see the command entries) — revert happens only when the last client disconnects, so `deferred:true` injection is reachable headless, and a client can join a paused server (receives full state, renders the frozen world; spawning still needs the HUD `[+]` buttons and an unpaused tick).
- **Injection is deferred while paused** (`"deferred":true`) and held while any client is waiting for spawn; it errors only during replay playback.
- **Physical input is fully suppressed on an `--agent-port` client** (kbAgent builds only): for the whole process lifetime, real mouse/keyboard/wheel/gamepad activity — including ImGui's own NewFrame cursor and gamepad-nav polls — is dropped before it reaches ImGui or the game-world snapshot, even while the window is focused. So a human sharing the machine cannot perturb a run: no need to keep the physical cursor off the window. The synthetic path (`hover`/`click`/`mouse`/`key`/`set_slider`) is the sole input source and drives normally; the cursor trap is released. Escape hatch preserved: Alt+F4 and the window Close button still quit. A client launched **without** `--agent-port` behaves exactly as before (no suppression).
- **Injected mouse pos owns `io.MousePos`:** an injected ImGui mouse pos (`hover`/`click`/`mouse move`) is pinned and persists until the next script — so `describe_ui` `mouse` and mouse-proximity UI keep the injected pixel. With no pin set, the physical cursor is suppressed to ImGui's no-mouse sentinel, so `describe_ui` `mouse` on a focused client reports the no-mouse state, not the real cursor. The game-world (RawInput) mouse pos still reverts after each script — under suppression physical mouse messages never reach DirectXTK Mouse, so it reverts to a frozen boot-time snapshot, not the live physical cursor (the pins have different lifetimes by design).
- **Audio is focus-gated:** `WM_KILLFOCUS` suspends the XAudio2 thread and `WM_SETFOCUS` resumes it — audio verification requires the game window to hold real OS focus. Focus messages are **not** suppressed, so a human clicking the window still focuses it and resumes audio (only input feeds are dropped).
- **Agent-mode launches stay minimized:** both executables use `SW_SHOWMINNOACTIVE` with `--agent-port`. The client also skips `SetForegroundWindow`/`BringWindowToTop`/`SetFocus` and suspends audio at boot, so it starts minimized, unfocused, and silent; a human restoring and clicking the window resumes audio, so hold focus before any audio check. `screenshot` and `dump_render_target` may briefly restore it without activation, but re-minimize it before replying.
- **Focus/injection interleaving:** client focus state is logged — if a scripted action depends on which window/fleet is focused, read `get_logs` (or `describe_ui` `focused` flags) to confirm state before acting.
- **Replay commands require `kbDebugInput`** — they error on builds without it (on in Debug, off in Profile/Release).
- **Keep the claim warm during long soaks:** the lock goes stealable after a ~5-minute mtime gap. During long unattended runs (e.g. replay determinism soaks) poll `status`/`get_logs` at least every few minutes — each AgentCli command refreshes the claim; a silent 5-minute gap (worst with the game already quit) exposes it to a steal. Do not leave a background AgentCli poll loop running past your session: an orphaned loop keeps touching the lock and holds it fresh indefinitely — the exact orphan-owner failure this staleness design exists to catch.
- **Port in use:** if a command can't connect (AgentCli exit 1, "connect failed"), a prior instance is likely still running and holding the port/mutex. `quit` it (or confirm it exited) before relaunching — a duplicate launch exits itself.

## Missing capability? Extend the harness

If a verification step needs an interaction the harness can't do — a missing command, param, result field, queryable state, or scripted-input primitive — **do not** fake it with fragile workarounds (pixel-guessing, log-scraping for state a query should expose) and do not silently skip the verification. Instead:

1. **Preferred: build it.** Additions are cheap and follow existing patterns — shared commands in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp`, server commands/queries in `AgentCommandsServer*.cpp`, client commands in `AgentCommandsClient.cpp` (deferred capture/input via `AgentCommandServer::DeferResponse`), input primitives in `Engine/Source/Agent/AgentInput.cpp`, scene fields in `AgentScene.cpp`. Route through the C++ Code Change Process; update this skill's command reference in the same session.
2. **Otherwise: defer, visibly.** If the extension is out of scope right now, create a plan in `Documents/Plans/Agent/` (+ `Order.md` row) specifying the missing capability and the verification it blocks, and report the skipped verification as a residual — never drop it silently.
