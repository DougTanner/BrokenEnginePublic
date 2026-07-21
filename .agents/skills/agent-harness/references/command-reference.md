# Agent Harness Command Reference

## Contents

- [Shared commands](#shared-commands)
- [Server commands](#server-commands)
- [Client commands](#client-commands)
- [Client command behavior](#client-command-behavior)

Every field below belongs under request `params`; every returned field belongs under response `result`. Sending a side-specific command to the other executable returns `unknown command`.

## Shared commands

- `ping`: no params. Returns `{"build":"server"|"client","tick":int}`; tick is `-1` before game creation.
- `quit`: no params. Requests clean shutdown; server autosaves. Returns `{}`.
- `get_logs`: `{"count"?:64,"pattern"?:"ECMAScript regex","category"?:"Default|Temp|Audio|Graphics|Loading|NavData|Network|Input"}`. Pattern scans the whole selected wrapping ring (512 cross-category lines when category is omitted; 128 lines for one category), then returns the last `count` matching lines chronologically as `{"lines":[...]}`. Matching is case-sensitive; use character classes rather than `(?i)`.
- `set_log_level`: `{"level":"Verbose|Debug|Info|Warning|Error","category"?:name}`. Levels below a compile-time floor clamp upward. Returns one effective level or a category map.

## Server commands

- `status`: no params. Returns `{"tick","paused","recording","replaying","clientCount","activeCoords":[[x,y],...],"nextGlobalId","pendingFlagshipUpdateCount":int}`. `pendingFlagshipUpdateCount` counts restored or runtime-queued flagship navigation updates waiting for an advancing update.
- `pause`: `{"paused":bool}`. Returns the applied `paused` value.
- `timescale`: `{"faster":bool}`. Steps the shared timescale and returns `{"numerator","denominator"}`.
- `save`: `{"file"?:"bare filename"}`; defaults to quicksave. Rejects empty names, NUL, separators, `..`, `:`, and Windows device basenames. Returns `{"file"}`.
- `load`: `{"file"?:"bare filename","pauseAfterLoad"?:false}`. `pauseAfterLoad` must be Boolean when present; invalid parameters fail before loading or resetting and do not change pause state. Missing/corrupt/truncated data resets fresh while returning success. Absent/false leaves the normal post-load state unpaused; true atomically pauses after a successful load or completed fresh fallback, before frame inputs can be constructed. Returns `{"file","resetToFresh":bool,"paused":bool,"pendingFlagshipUpdateCount":int}`.
- `reset`: no params. Resets the fresh game and fleet manager. Returns `{}`.
- `replay_record`: `{"start":bool}`. Schedules or cancels an idempotent recording transition and returns `{"pending":bool}`. Poll `status.recording` for the effective transition. Requires `kbDebugInput`.
- `replay_play`: no params. Starts playback or cancels active playback and returns `{"pending":true}`. Poll `status.replaying`. Per-tick checks call `DifferenceStreamReader::ValidateChecksum`; mismatch logs `LogDifferences CRC Client: ...`. Readers retire independently. When the last reader reaches its endpoint, the server logs `End replay <tick>, looping` at `Default/Debug` and loops; playback never ends by itself. Requires `kbDebugInput`.
- `replay_drop_retained_end_frame`: `{"coord":[x,y]}`. During recording, removes that coord's retained terminal frame to exercise aggregate stop persistence failure. Returns `{"coord":[x,y],"dropped":true}`. Requires `kbDebugInput`.
- `replay_inject_persistence_failure`: `{"stage":"invalidation"|"grid"|"coordinate_writer"|"metadata"|"final_manifest","coord"?:[x,y]}`. `coordinate_writer` alone requires coord. `invalidation`/`grid` require inactive recording; other stages require active recording. Returns `{"stage","coord"?:[x,y],"armed":true}`. Requires `kbDebugInput`.
- `query_frame`: `{"coord":[x,y]}` for a loaded, ready cell. Returns counts under `players`, `spaceships`, `missiles`, `blasters`, and `targets`.
- `query_players`: `{"coord":[x,y],"offset"?:0,"limit"?:256}`. Returns `{"total","players":[{"index","uuid","globalId","pos":[x,y,z],"dir":[x,y,z],"armor","shield","flags":int,"alignment"}]}`.
- `query_collection`: `{"coord":[x,y],"collection":"spaceships"|"missiles"|"blasters"|"targets","offset"?:0,"limit"?:256}`. Returns `{"total","items":[...]}`. Spaceship rows: `index,pos,dir,health,deltaRotation,alignment`; missile rows: `index,pos,dir,deltaRotation,deltaRotationDelay,alignment`; blaster rows: `index,pos,dir,alignment`; target rows: `index,uuid,pos,flags,alignment`. `deltaRotation` is the live turn rate; a missile's `deltaRotationDelay` counts down through zero and stays negative after its launch ramp finishes.
- `query_profile`: no params. Returns raw `timers[{index,name,currentUs,averageUs,maxUs,allocations,threads}]` and `counters[{index,name,count}]`, including zero rows hidden by the Profile UI.
- `inject_status_changes`: `{"changes":[...]}`. Every entry requires active `coord:[x,y]` and a type. `SpawnPlayer` accepts optional `isFlagship` and `fleetWantedCoord`; `DestroyPlayer` requires `playerUuid`; `UpdatePlayer` requires `playerUuid` and accepts `useMissiles`, `navigationDelay` clamped to `[0,60]`; `UpdateFleet` requires `playerUuid` and `fleetWantedCoord`, and accepts `isFlagship`. The whole batch validates before queueing. Returns `{"injected":int,"globalIds":[...],"deferred":bool}`.
- `spawn_players`: `{"coord":[x,y],"count":int 0..256,"isFlagship"?:false}`. Returns `{"injected","globalIds":[...],"deferred":bool}`.

Injection (`inject_status_changes` and `spawn_players`) fails during replay or while any client waits for spawn. The spawn-wait case is rejected, never queued, because an injected spawn could corrupt snapshot-diff client assignment. While paused, injection remains accepted and queued; `deferred:true` means it applies on the next unpaused tick. A speculative client reconciliation line after that tick is not a confirmed desync; only `CONFIRMED DESYNC after full rollback/replay` proves one.

Pause/timescale persist on an empty server. They reset only when the last connected client disconnects. A client may join a paused server and render frozen state, but spawning still needs an unpaused tick.

Frame-read schemas/extractors (`query_frame`, `query_players`, `query_collection`) are owned by `AgentCommandsServerQueries.cpp`. `query_profile`, simulation control, replay, injection, and server dispatch remain in `AgentCommandsServer.cpp`.

## Client commands

- `client_full_state_fixture`: `{"action":"arm_stall"|"inspect"|"exercise_gap"|"clear"}`. `arm_stall` requires connected confirmed state and returns `{"clientTick","stalled","desyncTick","syntheticStall","armedTick","timeMultiply","timeDivide","coordState":...}`. `inspect` returns the same live shape. `clear` idempotently clears synthetic stall and returns that shape with `syntheticStall:false,armedTick:-1`. A present coord state is `{"coord":[x,y],"present":true,"confirmedTick","confirmedOffset","highWaterValidatedTick","lastFullStateTick","snapshotHead","snapshotCount","lastRenderedTick","lastRenderedTime","serverUpdateCount","lastReplayConfirmedTick","lastReplayServerUpdateCount","stuckFrameCount","pendingFullStateTick":int|null,"firstServerUpdateTick":int|null,"lastServerUpdateTick":int|null,"ringValid","tailTick"}`; absent state contains only coord and `present:false`. `exercise_gap` returns `pendingTick,deferTargetTick,beforeDefer,afterDefer,deferPendingPreserved,deferDesync,removedUpdateCount,uncappedConsecutiveEndpoint,directAdoptionRequired,adoptionDesync,afterAdoption,pendingCleared,adoptedTicksMatch,confirmedOffsetZero,obsoleteUpdatesAbsent,ringHeadIsAdopted,directAdoptionProven,renderBaseNotOlderThanLastRendered,cleared`.
- `desync_probe`: packet mode `{"desyncReports"?:0..8,"debugFrameRequests"?:0..8}` or mutually exclusive recovery mode `{"triggerRecovery":true}`. Requires connected in-game current state. Returns `{"tick","coord":[x,y],"desyncDebugFrames","stalled","desyncReports","debugFrameRequests","triggerRecovery"}`.
- `screenshot`: `{"path"?,"maxWidth"?:1568,"format"?:"jpg"|"png","quality"?:80}`. Quality range is `1..100`. Default path is `%TEMP%\Screenshots\agent_N.<ext>` using the process temp directory. Returns saved capture information. Requires `kbScreenshots`.
- `resize`: `{"width","height"}` within `320x180..16384x16384`; rounds each to 8, then returns OS-applied `{"width","height"}`. Rejects while minimized and never changes persisted/fullscreen state.
- `fullscreen`: `{"on":bool}`. Toggles borderless fullscreen/windowed without persisting. Windowed restore uses launch extent, not a later resize. Returns `{"fullscreen","width","height"}`. Rejects while minimized.
- `window_state`: `{"minimized":bool}`. Minimize uses normal minimize; restore does not activate and waits for settled swapchain. Returns `{"minimized","width"?,"height"?}`.
- `dump_render_target`: `{"name","index"?:0,"channel"?:0,"path"?,"raw"?:false}`. Unknown names list valid names. Single-channel output normalizes to grayscale PNG; four-byte RGBA writes direct PNG; float formats require raw `.bin`. The default base path is `%TEMP%\Screenshots\dump_<name>_N`. Requires `kbScreenshots`.
- `describe_ui`: no params. Returns UI state, game flags, framebuffer, mouse, windows, and labeled items with rect/disabled/checked/inputable/hovered/visible fields. Values are not exposed.
- `click`: `{"label","window"?,"timeoutFrames"?:120,"describeUiAfter"?:true}`. Stabilizes and clicks the item center. Returns found/enabled and optional UI. Ambiguous/not-found errors list candidates; scrolled-out targets fail.
- `hover`: `{"label","window"?,"holdFrames"?:2}`. Returns found/enabled plus UI.
- `set_slider`: `{"label","window"?,"value":number}`. Uses Ctrl+Click, typing, Enter. Returns found/enabled.
- `key`: `{"key":name,"holdFrames"?:1}`. Supports one letter/digit, Escape, Space, Tab, Enter, arrows, and F1-F24. Returns `{"ok":true}`.
- `mouse`: `{"action"?:"move"|"down"|"up"|"click"|"wheel","x"?,"y"?,"button"?:"left"|"right"|"middle","notches"?:1}`. Non-wheel actions require x/y. Wheel accepts both coordinates or neither; it always also feeds camera zoom. Returns `{"ok":true}`.
- `describe_scene`: `{"includeUnits"?:true,"maxUnits"?:200}`. Returns `camera{eye,visibleArea,lod}`, `uiState`, `gameFlags`, `tick`, `clientGridCoord`, `subscribedCoords`, `fleets[{index,focused,members?}]`, `units[{type,globalId?,world,screen,armor?,shield?,health?,alignment,flags}]`, cell-wide `counts{players,spaceships,missiles,blasters,targets}`, `islands[{coord,center,rotation,footprint?}]`, and `truncated`. Unit positions come from the committed snapshot and trail rendered pixels.

## Client command behavior

Client input/capture commands can defer across frames. The single channel remains occupied until the response; size timeout accordingly and never overlap calls.

For `client_full_state_fixture`, run `arm_stall`, accelerate the server, then `inspect`. Require unchanged client/armed tick, changed timescale, and a pending full-state tick above client tick. Run `exercise_gap`; require both desync fields false, pending preservation, `uncappedConsecutiveEndpoint < pendingTick`, direct adoption required/proven, pending cleared, adopted ticks match, confirmed offset zero, obsolete updates absent, ring head adopted/valid, and render base not older than last rendered. `removedUpdateCount` may be zero. Always `clear` interrupted setup.

Capture commands temporarily restore a minimized client without activation, wait for extent settlement, capture, then re-minimize. An already-visible client fails if swapchain recreation is deferred. `resize`/`fullscreen` reject minimized state; restore first. After resize/fullscreen/restore, call `describe_ui` or `screenshot` to remeasure.

Only labeled visible ImGui items are addressable. Scroll a clipped container with `mouse` wheel, then retry. `describe_scene.units` are visible-area-filtered while counts span subscribed cells; edge projection may lie slightly outside the framebuffer. Only players have global IDs, and only the focused fleet lists members.
