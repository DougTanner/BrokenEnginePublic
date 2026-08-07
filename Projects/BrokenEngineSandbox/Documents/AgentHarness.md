# BrokenEngineSandbox Agent Harness

Project-specific launch configuration, verification recipes, durable caveats, and command schemas for driving BrokenEngineSandbox through the `/agent-harness` skill. The skill ([SKILL.md](../../../.agents/skills/agent-harness/SKILL.md)) owns provision/claim, ownership/takeover, the request/response envelope, lifecycle/release, and — in [command-reference.md](../../../.agents/skills/agent-harness/references/command-reference.md) — the four engine-shared command schemas (`ping`, `quit`, `get_logs`, `set_log_level`). Read this doc after selecting BrokenEngineSandbox and before launching; it owns the executable paths, output directory, extra launch arguments, game command schemas, and authoritative verification recipes.

## Launch

Follow the skill's generic launch requirements (data-oracle checks, `--loopback-only`, `$ROOT\Temp` log parents, and Codex `Start-Process -PassThru` PID retention), then launch these executables:

```powershell
$Output = Join-Path $ROOT 'Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output'
$ServerExe = Join-Path $Output 'BrokenEngineSandboxServer.Debug.exe'
$ClientExe = Join-Path $Output 'BrokenEngineSandbox.Debug.exe'
$TempDir = Join-Path $ROOT 'Temp'
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null
$QuotedData = '"' + $GameDataDirectory + '"'
$ServerLog = Join-Path $TempDir 'server-agent.log'
$ClientLog = Join-Path $TempDir 'client-agent.log'
$ServerPid = $null
$ClientPid = $null

$ServerProcess = Start-Process -FilePath $ServerExe -ArgumentList @(
	'--agent-port', '27100', '--loopback-only', '--data-directory', $QuotedData,
	'--log-file', ('"' + $ServerLog + '"')) -WindowStyle Hidden -PassThru
$ClientProcess = Start-Process -FilePath $ClientExe -ArgumentList @(
	'--agent-port', '27101', '--loopback-only', '--data-directory', $QuotedData,
	'--windowed', '1600x900', '--log-file', ('"' + $ClientLog + '"')) -WindowStyle Hidden -PassThru
$ServerPid = $ServerProcess.Id
$ClientPid = $ClientProcess.Id
```

After both generic deadline-limited `Wait-HarnessPing.ps1` readiness checks succeed (server port `27100`, then client port `27101`), restore the minimized agent-mode client and require a successful response confirming `result.minimized:false` before relying on Debug/Profile UI auto-connect:

```powershell
$WindowStateResponse = '{"cmd":"window_state","params":{"minimized":false}}' |
	& $AgentHarness --owner $Owner --port 27101 -
if ($LASTEXITCODE -ne 0) { throw 'window_state restore failed.' }
$WindowStateResponse = $WindowStateResponse | ConvertFrom-Json
if ($WindowStateResponse.ok -ne $true -or $WindowStateResponse.result.minimized -isnot [bool] -or
	$WindowStateResponse.result.minimized) {
	throw 'window_state did not confirm result.minimized:false.'
}
```

Keep the client visible for the scenario. Release requires `click "LOCAL SERVER"`. The server loads its exit autosave, so use `reset` when the scenario needs fresh state.

For an approved island-footprint or island-render criterion only, run the readiness helper after both processes launch, preserving the `$ServerPid` and `$ClientPid` variables above:

```powershell
$IslandReadinessArtifact = Join-Path $TempDir 'island-scene-readiness.json'
& "$ROOT\.agents\skills\agent-harness\scripts\Wait-IslandSceneReady.ps1" `
	-AgentHarness $AgentHarness -Owner $Owner -ClientPort 27101 `
	-TimeoutSeconds 120 -ArtifactPath $IslandReadinessArtifact
if ($LASTEXITCODE -ne 0) { throw "Island scene readiness failed; inspect '$IslandReadinessArtifact'." }
$IslandReadiness = Get-Content -Raw -LiteralPath $IslandReadinessArtifact | ConvertFrom-Json -Depth 100
if ($IslandReadiness.SchemaVersion -cne 'broken-engine-island-scene-readiness/v1' -or
	$IslandReadiness.Status -cne 'success' -or $IslandReadiness.Code -cne 'ready' -or
	$IslandReadiness.Ready -isnot [bool] -or -not $IslandReadiness.Ready) {
	throw "Invalid island readiness evidence: '$IslandReadinessArtifact'."
}
```

This is a criterion-specific gate, not general launch readiness. It holds the client visible and proves a ready client tick plus two stable complete footprint samples.

## Authoritative verification

Set up server and client state with the recipe below, then verify and release per the skill's Authoritative verification evidence principles and lifecycle checklist.

1. Set up server state with `reset`, then `spawn_players` or `inject_status_changes` at a coord from `status.activeCoords`; confirm through `query_players`/`query_frame`.
2. Launch/connect the client and require `status.clientCount` to increase.
3. Use `describe_ui` before label-addressed `click`, `hover`, or `set_slider`; use `key`/`mouse` for raw input.

### Replay determinism acceptance

Run replay acceptance only on a `kbDebugInput` build. It must prove transitions and a completed playback loop, not merely the absence of old errors:

1. Set the server `Default` log level to `Debug`. Capture server and client relevant-log baselines with `get_logs {"count":512,"pattern":"[Rr]eplay|CRC|[Cc]hecksum|[Dd]esync"}`. Preserve the ordered arrays so later checks can identify only appended lines.
2. Send `pause {"paused":false}` and require `status.paused:false`, `recording:false`, and `replaying:false`.
3. Send `replay_record {"start":true}` and require a later status with `recording:true` and `paused:false`. Let multiple ticks complete.
4. Send `replay_record {"start":false}` and require a later status with `recording:false` and `paused:false`.
5. Send `replay_play`; require `replaying:true` and `paused:false`. Wait for a newly appended server line matching `End replay [0-9]+, looping`. This current runtime marker proves every reader reached its recorded endpoint and the first playback loop completed.
6. Compare post-run relevant logs with both ordered baselines. Require no newly appended replay-persistence/read failure, `LogDifferences CRC Client`, checksum-mismatch, `CONFIRMED DESYNC`, or unresolved-CRC error line. Do not count pre-baseline lines as new evidence, and do not treat speculative reconciliation messages alone as a replay failure.
7. Send `replay_play` again to cancel. Require a later status with `replaying:false`, `recording:false`, and `paused:false`.

If the 512-line relevant-log window cannot retain the baseline through this bounded scenario, rerun with file-offset evidence from the configured per-process logs; never downgrade to “silence = pass.”

### Replay transfer-capture fixture

Run this on the Debug server only (`kbDebugInput` is required). It replaces the unavailable historical tick-39222 artifact with deterministic transfers. Keep a newly appended server-log slice for the whole fixture and reject any new `LogDifferences CRC Client`, checksum mismatch, `CONFIRMED DESYNC`, replay-reader error, or `SaveLoadReplay aborted` line.

`replay_transfer_fixture` is accepted during active recording, or only while the server is paused with an unconsumed `replay_record {"start":true}` transition. It rejects playback, ordinary non-recording use, an unpaused pending start, cancelled starts, invalid source frames, and invalid coordinates. Its optional Boolean `pauseAfterWriterInput` arms count `1` for a pending start. During active recording it snapshots the current writer-input count `N` and arms `N + 2`: after command drain, that update's `SyncReplayTick` records `N + 1`; `FinalizeFrameTick` harvests the fixture at `E`; and `N + 2` records the transfer-bearing input at `E + 1` before pausing. The result echoes the applied Boolean.

`replay_transfer_capture` exposes `firstWriterInputTick` and `writerInputCount` in addition to the event fields. The count is one per advancing simulation tick with one or more nonterminal writer updates, never one per writer or coordinate; terminal flushes do not increment it. `followingEmptyInputTick` is the aggregate next writer-input tick: it latches only after every eligible normal or terminal writer composed an input with no transfer and all required persistence succeeded.

For the focused active-recording pause scenario, begin with `writerInputCount == N > 1`, then queue a player fixture with `pauseAfterWriterInput:true`. A prompt `replay_transfer_capture` may observe the pre-harvest `N + 1` input with the server still unpaused, but response delivery does not synchronize the next command drain; observing only the final paused `N + 2` state is not a failure. Require one automatic pause at count `N + 2`, `recordingEventTick == E`, `writerInputTick == E + 1`, and exactly one player transfer. Resume, allow a later writer input, and prove it does not pause again.

Run the following A–G matrix. Keep a newly appended server-log slice for the entire matrix and reject any new `LogDifferences CRC Client`, checksum mismatch, `CONFIRMED DESYNC`, replay-reader error, or `SaveLoadReplay aborted` line.

A. Send `reset`, `pause {"paused":true}`, then `replay_record {"start":true}`. While still paused, queue `replay_transfer_fixture {"type":"player","source":[0,0],"destination":[1,0],"pauseAfterWriterInput":true}` and require its echoed Boolean. Unpause. Tick `E` starts recording and harvests the fixture; after the `E + 1` writer input, require automatic `status.paused:true`, `recordingEventTick == E`, `writerInputTick == E + 1`, `firstWriterInputTick == E + 1`, `writerInputCount == 1`, `followingEmptyInputTick == -1`, `playerCount == 1`, and every other transfer count zero.
B. While automatically paused after A, send `replay_record {"start":false}`, then unpause. The stop transition at `E + 2` must yield `followingEmptyInputTick == E + 2` and retain `writerInputCount == 1`. Poll until `status.recording:false` before sending `replay_play`, then poll `status.replaying:true`.
C. During that playback, require `[1,0]` absent through tick `E`, then active at `E + 1`; require `playbackEventTick == writerInputTick`, exactly one player from `query_players {"coord":[1,0]}`, and no CRC/read error. This proves activation, replayed transfer, and checksum validation.
D. Record a multi-event variant: use the player fixture to create `[1,0]`, then queue one each of `spaceship`, `blaster`, and `missile` from `[0,0]` to `[1,0]`. For each event, require its matching count to be `1`, the other counts zero, `writerInputTick == recordingEventTick + 1`, and the matching `query_collection` count at `[1,0]`. The Blaster query is the deterministic tick-39222 regression signal.
E. Retire `[1,0]` after D by reading its player `uuid`, sending `inject_status_changes {"changes":[{"coord":[1,0],"type":"DestroyPlayer","playerUuid":<uuid>}]}`, and waiting for it to leave `status.activeCoords`. Stop, poll `recording:false`, then issue `replay_play`. The destination must retire at its recorded terminal boundary without missing/duplicate collections, `Replay reader terminal data was not fully consumed`, or `advanced beyond terminal`; require an `End replay <tick>, looping` marker.
F. Abort a pending start: `reset`, pause, request recording, queue the player fixture, then send `replay_record {"start":false}` before unpausing. After unpausing, require no `[1,0]` destination, no transferred entity, and no recording event in `replay_transfer_capture`.
G. Abort an injected start failure: `reset`, pause, arm `replay_inject_persistence_failure {"stage":"invalidation"}`, request recording, and queue the player fixture. Unpause. Require no `[1,0]` destination, no transferred entity, and no recording event in `replay_transfer_capture`.

#### Replay manifest v3 integrity matrix

This is a stopped-server AppData test, not an agent command. Produce the valid multi-coordinate fixture above and prove one loop. Stop/release the server, back up the complete `F7.replay.manifest`, `.grid`, `.meta`, and every coord sibling; restore the backup before each change and relaunch. The v3 manifest is fixed-width little-endian: version, initial tick, activation count and `(activationTick,x,y)` records; a `hasFullFrames` byte; inventory count and ordered `(kind,coordKey,byteCount,sha256[32])` entries; then the 32-byte generation digest. Kinds are grid/meta/header/frames/checksums/fullframes = 0..5. The SHA-256 preimage is, in order, the four little-endian length bytes `2B 00 00 00`, the 43 bytes of `broken-engine/replay-manifest-generation/v3` without a NUL, then the exact manifest semantic payload bytes from version through inventory.

For every rejection case, pause immediately after relaunch, record `status.activeCoords` and the live ID state, send `replay_play`, and require `status.replaying:false` with unchanged live state (no adoption). Corruption cases require a new `SaveLoadReplay aborted: corrupt replay data:` line.

H1. Change version to `2`: reject with a new `Replay manifest version ...` line; restore v3 unchanged and require a loop as the control.
H2. Change one byte or byte count of each inventory kind; then separately remove, replace with a directory, or replace with a reparse point each named artifact. Reject before grid/meta/stream parsing.
H3. Change the generation digest and reject. For the binary coordinate-identity case, change a kind-2 coord-header entry from `[1,0]` to unrecorded `[2,0]` (little-endian `ToKey` bytes `00 00 00 00 02 00 00 00`), preserve inventory ordering, and recompute the generation digest; require `ReplayManifest inventory identity` to corrupt-abort before adoption. Separately test invalid fullframes flag, unknown kind, reordered inventory, duplicate inventory entry, missing/surplus entry, negative count/size, and trailing data. Reject each.
H4. On Debug, kind-5 `.fullframes` holds complete frame snapshots distinct from the kind-3 `.frames` input-difference stream; require one kind-5 fullframes manifest entry for each recorded coord's kind-2 header, and require every named ordinary fullframes file to match its manifest size/hash identity. A clean replay control must prove the debug reader consumes the snapshots without CRC, desync, or read errors. On non-Debug, static inspection proves `kbReplayFullFrames == false` and v3 has no kind-5 entry. Do not enable runtime Release diagnostics.
H5. While recording with no special coord requirement, arm `replay_inject_persistence_failure {"stage":"inventory"}`, stop, then call `replay_play`: it must fail with no valid manifest/adoption. This verifies manifest-last publication.
H6. These procedures do not claim authentication or protection against files changed after preflight.

## Durable caveats

- Client weapon-mode requests received during paused or other zero-tick updates and internally queued flagship navigation updates persist until the first advancing update. Client `kClientFleetNavigationDelay` requests apply immediately. To verify load-requeued flagship updates, use `load {"pauseAfterLoad":true}` and inspect `pendingFlagshipUpdateCount` before unpausing.
- Injection during replay fails. Injection while clients wait for spawn also fails immediately so it cannot corrupt snapshot-diff assignment; it is never queued for later. Injection while paused remains accepted with `deferred:true` and applies on the next unpaused tick.
- `navQueryActivation` is sticky until its exact event sequence is acknowledged. Do not issue another armed transition while `activationEvent.available` is true. A retained event is one immutable payload, not a history: an additional query-count-eight candidate cannot overwrite it and sets only `activationEvent.overrun`; treat `overrun:true` as measurement failure. A false latch (pause, non-`1/1` timescale, burst/catch-up, save/replay, zero-tick, or replay no-dispatch), shared reset/load, and replay-load entry cancel only an unpublished arm/floor and clear pending raw values; they preserve a retained payload, sequence, and sticky overrun. The client command surface and client profile schema remain unchanged.
- When comparing `query_profile` timings, run and discard a warm-up cohort first, take baselines only after values settle, and compare cohorts of equal sample count captured within one process lifetime. For the server `NavQuery` raw row, retain every observed sequence and gap marker, then qualify cohorts only from records with `queryCount == 8 && aStarCount == 8`; do not reconstruct skipped sequences or use the overlapping `averageUs`/`maxUs` smoother rows as cohorts.
- Server frame-read query schema and extraction live in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServerQueries.cpp`; simulation control, replay, CPU `query_profile`, injection, and dispatch live in `AgentCommandsServer.cpp`. Client capture/input/UI/GPU `query_profile` commands live in `AgentCommandsClient.cpp`; scene commands live in `AgentScene.cpp`.

## Command reference

These schemas follow the shared `params`/`result` placement convention in [command-reference.md](../../../.agents/skills/agent-harness/references/command-reference.md): every field below belongs under request `params`, every returned field belongs under response `result`, and sending a side-specific command to the other executable returns `unknown command`.

### Both-endpoint commands

Game-specific commands compiled into both executables; send to either port. Unlike the side-specific commands below, they never return `unknown command` for the "wrong" build.

- `collection_layout_capacity_fixture`: no params. SOA physical-layout-capacity retention self-check on the real `BlastersPostRender`, driving production `SharedCollectionRead` through logical capacities 100 -> 70 -> 60 -> 150 on one reused instance. Returns `{"build":"server"|"client","steps":[{"label","logicalCapacity","physicalCapacity","reused"}],"sharedRowMismatches":int,"sharedRowsPreserved":bool,"passed":bool}`, plus client-only `{"clientSoundsZeroed":bool,"clientSoundsNonZeroCount":int}` or server-only `{"serverMembersEqualShared":bool}`. `passed` requires both shrinks to reuse the 100-wide buffer, the >100-row read to reallocate once (physical 150), every shared row preserved, and — on the client — the full physical `puiSounds` layout (rows 70-99 included) zeroed, or — on the server — `serverMembersEqualShared` true. Requires `kbDebugInput`.

### Server commands

- `status`: no params. Returns `{"tick","paused","recording","replaying","clientCount","activeCoords":[[x,y],...],"nextGlobalId","pendingFlagshipUpdateCount":int}`. `pendingFlagshipUpdateCount` counts restored or runtime-queued flagship navigation updates waiting for an advancing update.
- `pause`: `{"paused":bool}`. Returns the applied `paused` value.
- `timescale`: `{"faster":bool}`. Steps the shared timescale and returns `{"numerator","denominator"}`.
- `save`: `{"file"?:"bare filename"}`; defaults to quicksave. Rejects empty names, NUL, separators, `..`, `:`, and Windows device basenames. Returns `{"file"}`.
- `load`: `{"file"?:"bare filename","pauseAfterLoad"?:false}`. `pauseAfterLoad` must be Boolean when present; invalid parameters fail before loading or resetting and do not change pause state. Missing/corrupt/truncated data resets fresh while returning success. Absent/false leaves the normal post-load state unpaused; true atomically pauses after a successful load or completed fresh fallback, before frame inputs can be constructed. Returns `{"file","resetToFresh":bool,"paused":bool,"pendingFlagshipUpdateCount":int}`.
- `reset`: no params. Resets the fresh game and fleet manager. Returns `{}`.
- `replay_record`: `{"start":bool}`. Schedules or cancels an idempotent recording transition and returns `{"pending":bool}`. Poll `status.recording` for the effective transition. Requires `kbDebugInput`.
- `replay_play`: no params. Starts playback or cancels active playback and returns `{"pending":true}`. Poll `status.replaying`. Per-tick checks call `DifferenceStreamReader::ValidateChecksum`; mismatch logs `LogDifferences CRC Client: ...`. Readers retire independently. When the last reader reaches its endpoint, the server logs `End replay <tick>, looping` at `Default/Debug` and loops; playback never ends by itself. Requires `kbDebugInput`.
- `replay_transfer_fixture`: `{"type":"player"|"spaceship"|"blaster"|"missile","source":[x,y],"destination":[x,y],"pauseAfterWriterInput"?:false}`. `pauseAfterWriterInput` must be Boolean when present. Requires no active replay, a ready active source frame, and distinct Chebyshev-adjacent coordinates. It is accepted only during active recording or while paused with an unconsumed pending recording start. The response is `{"type","source":[x,y],"destination":[x,y],"pauseAfterWriterInput"}`. It queues one deterministic, type-valid transfer through the ordinary harvest/sort/capture path and, when requested, arms count `1` for a pending start or snapshots active count `N` and pauses at `N + 2` after the transfer-bearing writer input. A second `pauseAfterWriterInput:true` fixture while an automatic pause is armed is rejected before it mints an ID or queues a transfer; `false` remains allowed. `player` may create a missing destination; each other type requires a live destination (a player or active client subscription). Requires `kbDebugInput`.
- `replay_transfer_capture`: no params. Returns `{"recordingEventTick","playbackEventTick","writerInputTick","followingEmptyInputTick","firstWriterInputTick","writerInputCount","playerCount","spaceshipCount","blasterCount","missileCount"}`. Tick fields are `-1` until observed. `writerInputCount` is the total normal-writer update ticks since a successful recording start; terminal flushes do not increment it. Event counts describe the last captured transfer event, while `followingEmptyInputTick` is the aggregate next writer-input tick with no transfer. Requires `kbDebugInput`.
- `replay_drop_retained_end_frame`: `{"coord":[x,y]}`. During recording, removes that coord's retained terminal frame to exercise aggregate stop persistence failure. Returns `{"coord":[x,y],"dropped":true}`. Requires `kbDebugInput`.
- `replay_inject_persistence_failure`: `{"stage":"invalidation"|"grid"|"coordinate_writer"|"metadata"|"inventory"|"final_manifest","coord"?:[x,y]}`. `coordinate_writer` alone requires coord. `invalidation`/`grid` require inactive recording; other stages require active recording. `inventory` is consumed immediately before final artifact hashing and manifest publication. Returns `{"stage","coord"?:[x,y],"armed":true}`. Requires `kbDebugInput`.
- `query_frame`: `{"coord":[x,y]}` for a loaded, ready cell. Returns counts under `players`, `spaceships`, `missiles`, `blasters`, and `targets`.
- `query_players`: `{"coord":[x,y],"offset"?:0,"limit"?:256}`. Returns `{"total","players":[{"index","uuid","globalId","pos":[x,y,z],"dir":[x,y,z],"armor","shield","flags":int,"alignment"}]}`.
- `query_collection`: `{"coord":[x,y],"collection":"spaceships"|"missiles"|"blasters"|"targets","offset"?:0,"limit"?:256}`. Returns `{"total","items":[...]}`. Spaceship rows: `index,pos,dir,health,deltaRotation,alignment`; missile rows: `index,pos,dir,deltaRotation,deltaRotationDelay,alignment`; blaster rows: `index,pos,dir,alignment`; target rows: `index,uuid,pos,flags,alignment`. `deltaRotation` is the live turn rate; a missile's `deltaRotationDelay` counts down through zero and stays negative after its launch ramp finishes.
- `query_profile`: server-only CPU profile query. `params` must be `{}` or exactly `{"ackActivationEventSequence":<nonzero unsigned integer>}`; arrays, non-objects, signed/float/string/zero acknowledgements, and unknown keys fail before any profile read or acknowledgement. It returns raw `timers[{index,name,currentUs,averageUs,maxUs,allocations,threads}]` and `counters[{index,name,count}]`, including zero rows hidden by the Profile UI. On a server built with `kbProfiling`, only the `NavQuery` timer row additionally returns `sampleSequence`, `sampleUs`, `queryCount`, `aStarCount`, and `activationEvent:{available,eventSequence,sampleSequence,sampleTick,sampleUs,queryCount,aStarCount,qualifying,overrun}`. `qualifying` is `available && queryCount == 8 && aStarCount == 8`; it is presentation logic, not an Engine event gate. An acknowledgement request also adds `activationEventAcknowledged` to that row (`true` only when the exact available event sequence was cleared). The timer row and event are read under one profile mutex, so the post-ack fields are coherent. `sampleSequence` starts at `0` and advances once per accepted normal one-tick update; `sampleUs` is the accumulated timed `NavQueryDirection` duration latched after all active-cell workers join, `queryCount` counts timed calls, and `aStarCount` counts calls that entered A*. Repeated reads before a newly accepted tick return the same raw sequence and values. An available event's sample payload and event sequence are immutable across reads; a later candidate sets only its separate sticky `overrun` bit. An exact acknowledgement clears `available` and `overrun` without rewinding the event sequence or retained payload; stale/future acknowledgements change nothing. Paused, burst, non-`1/1` timescale, save, replay, zero-tick, and other rejected/no-dispatch paths discard pending raw values and cancel an unpublished arm/floor without changing a retained event. Polling can skip raw sequence values; skipped values are unobserved gaps, not rejected-update records. The client `query_profile` remains `{}`-only and GPU-only, with no activation fields.
- `inject_status_changes`: `{"changes":[...],"navQueryActivation":{"arm":true}}`, where `changes` is required and `navQueryActivation` is optional; these are the only top-level keys. The nested object is exactly `{"arm":true}`: unknown nested keys, a missing/non-array `changes`, a non-object activation, or any `arm` value other than JSON boolean `true` fails before change validation, profiler state, or queue mutation. Every entry requires active `coord:[x,y]` and a type. `SpawnPlayer` accepts optional `isFlagship` and `fleetWantedCoord`; `DestroyPlayer` requires `playerUuid`; `UpdatePlayer` requires `playerUuid` and accepts `useMissiles`, `navigationDelay` clamped to `[0,60]`; `UpdateFleet` requires `playerUuid` and `fleetWantedCoord`, and accepts `isFlagship`. The whole batch validates before queueing. An explicit activation arm is server/profiling-only and additionally requires unpaused normal state (`kPaused`, `kSaveReplay`, and `kLoadReplay` clear; timescale `1/1`; recording and replay inactive); the existing replay-playback and client-spawn-wait guards still reject. The handler captures its own `queuedAtTick`, computes `minimumSampleTick = queuedAtTick + engine::kiTickRate + 1`, arms and queues the already-built batch as one main-thread transaction, and rejects an occupied retained event or sticky overrun before queueing anything. Ordinary requests retain their existing paused/deferred behavior and response. An armed response is exactly `{"injected":int,"globalIds":[...],"deferred":bool,"navQueryActivation":{"armed":true,"queuedAtTick":int,"minimumSampleTick":int}}`; ordinary responses omit `navQueryActivation`.
- `spawn_players`: `{"coord":[x,y],"count":int 0..256,"isFlagship"?:false}`. Returns `{"injected","globalIds":[...],"deferred":bool}`.

Injection (`inject_status_changes` and `spawn_players`) fails during replay or while any client waits for spawn. The spawn-wait case is rejected, never queued, because an injected spawn could corrupt snapshot-diff client assignment. While paused, injection remains accepted and queued; `deferred:true` means it applies on the next unpaused tick. A speculative client reconciliation line after that tick is not a confirmed desync; only `CONFIRMED DESYNC after full rollback/replay` proves one.

Pause/timescale persist on an empty server. They reset only when the last connected client disconnects. A client may join a paused server and render frozen state, but spawning still needs an unpaused tick.

Frame-read schemas/extractors (`query_frame`, `query_players`, `query_collection`) are owned by `AgentCommandsServerQueries.cpp`. CPU `query_profile`, simulation control, replay, injection, and server dispatch remain in `AgentCommandsServer.cpp`.

### Client commands

- `client_full_state_fixture`: `{"action":"arm_stall"|"inspect"|"exercise_gap"|"clear"}`. `arm_stall` requires connected confirmed state and returns `{"clientTick","stalled","desyncTick","syntheticStall","armedTick","timeMultiply","timeDivide","coordState":...}`. `inspect` returns the same live shape. `clear` idempotently clears synthetic stall and returns that shape with `syntheticStall:false,armedTick:-1`. A present coord state is `{"coord":[x,y],"present":true,"confirmedTick","confirmedOffset","highWaterValidatedTick","lastFullStateTick","snapshotHead","snapshotCount","lastRenderedTick","lastRenderedTime","serverUpdateCount","lastReplayConfirmedTick","lastReplayServerUpdateCount","stuckFrameCount","pendingFullStateTick":int|null,"firstServerUpdateTick":int|null,"lastServerUpdateTick":int|null,"ringValid","tailTick"}`; absent state contains only coord and `present:false`. `exercise_gap` returns `pendingTick,deferTargetTick,beforeDefer,afterDefer,deferPendingPreserved,deferDesync,removedUpdateCount,uncappedConsecutiveEndpoint,directAdoptionRequired,adoptionDesync,afterAdoption,pendingCleared,adoptedTicksMatch,confirmedOffsetZero,obsoleteUpdatesAbsent,ringHeadIsAdopted,directAdoptionProven,renderBaseNotOlderThanLastRendered,cleared`.
- `desync_probe`: packet mode `{"desyncReports"?:0..8,"debugFrameRequests"?:0..8}` or mutually exclusive recovery mode `{"triggerRecovery":true}`. Requires connected in-game current state. Returns `{"tick","coord":[x,y],"desyncDebugFrames","stalled","desyncReports","debugFrameRequests","triggerRecovery"}`.
- `screenshot`: `{"path"?,"maxWidth"?:1568,"format"?:"jpg"|"png","quality"?:80}`. Quality range is `1..100`. Default path is `%TEMP%\Screenshots\agent_N.<ext>` using the process temp directory. Returns `{"path","width","height"}` — the saved file and its pixel dimensions, never image bytes. Requires `kbScreenshots`.
- `renderdoc_capture`: `{"frames"?:1}`; `frames` is `1..8`. Triggers RenderDoc capture(s) of the next presented frame(s) and returns `{"paths":[absolute .rdc paths]}`. Requires a client launched with `--renderdoc`; otherwise returns `ok:false` naming `--renderdoc`. Temporarily restores a minimized client without activation, captures, then re-minimizes; an already-visible client fails when swapchain recreation is deferred. Occupies the single channel until RenderDoc has serialized every capture. The wait is bounded by the deferred-response drain-count liveness timeout (`kiDeferredTimeoutDrains` drains, counted once per client render frame), not wall-clock, so set `--timeout-ms` at least as large as the expected wall-clock capture time. See [RenderDoc capture](../../../.agents/skills/agent-harness/references/renderdoc.md).
- `resize`: `{"width","height"}` within `320x180..16384x16384`; rounds each to 8, then returns OS-applied `{"width","height"}`. Rejects while minimized and never changes persisted/fullscreen state.
- `fullscreen`: `{"on":bool}`. Toggles borderless fullscreen/windowed without persisting. Windowed restore uses launch extent, not a later resize. Returns `{"fullscreen","width","height"}`. Rejects while minimized.
- `window_state`: `{"minimized":bool}`. Minimize uses normal minimize; restore does not activate and waits for settled swapchain. Returns `{"minimized","width"?,"height"?}`.
- `dump_render_target`: `{"name","index"?:0,"channel"?:0,"path"?,"raw"?:false}`. Unknown names list valid names. Single-channel output normalizes to grayscale PNG; four-byte RGBA writes direct PNG; float formats require raw `.bin`. The default base path is `%TEMP%\Screenshots\dump_<name>_N`. Requires `kbScreenshots`.
- `describe_ui`: no params. Returns UI state, game flags, framebuffer, mouse, windows, and labeled items with rect/disabled/checked/inputable/hovered/visible fields. Values are not exposed.
- `click`: `{"label","window"?,"timeoutFrames"?:120,"describeUiAfter"?:true}`. Stabilizes and clicks the item center. Returns found/enabled and optional UI. Ambiguous/not-found errors list candidates; scrolled-out targets fail.
- `hover`: `{"label","window"?,"holdFrames"?:2}`. Returns found/enabled plus UI.
- `set_slider`: `{"label","window"?,"value":number}`. Uses Ctrl+Click, typing, Enter. Returns found/enabled.
- `key`: `{"key":name,"holdFrames"?:1}`. `holdFrames` is an integer in `0..1798` (default `1`); signed negative values normalize to `0`. The limit counts client drains/frames rather than wall-clock time, so duration varies with the client drain rate. Supports case-insensitive single letters/digits; `ESC`/`ESCAPE`, `SPACE`, `TAB`, `ENTER`/`RETURN`, `UP`/`DOWN`/`LEFT`/`RIGHT`, and `F1`–`F24`. Returns `{"ok":true}`.
- `mouse`: `{"action"?:"move"|"down"|"up"|"click"|"wheel","x"?,"y"?,"button"?:"left"|"right"|"middle","notches"?:1}`. Non-wheel actions require x/y. Wheel accepts both coordinates or neither; it always feeds the ImGui wheel event, and it also feeds camera zoom unless the hovered ImGui window can actually scroll, or ImGui's wheeling lock still holds an earlier scroll target from a recent scroll (menus that do not scroll still zoom the camera). Returns `{"ok":true}`.
- `describe_scene`: `{"includeUnits"?:true,"maxUnits"?:200}`. Returns `camera{eye,visibleArea,lod}`, `uiState`, `gameFlags`, `tick`, `clientGridCoord`, `subscribedCoords`, `fleets[{index,focused,members?}]`, `units[{type,globalId?,world,screen,armor?,shield?,health?,alignment,flags}]`, cell-wide `counts{players,spaceships,missiles,blasters,targets}`, `islands[{coord,center,rotation,footprint?}]`, and `truncated`. Unit positions come from the committed snapshot and trail rendered pixels.
- `query_profile`: no params (only an empty object is accepted). Client-only GPU profile query; returns `gpuTimers[{index,name,currentUs,averageUs,maxUs}]` for every timer row, including zeros, and frame-coherent `shadowSample{sequence,currentUs}`. The sample is published after the matching framebuffer fence; sequence advances only when a new available Shadow result is latched.

### Client command behavior

Client input/capture commands can defer across frames. The single channel remains occupied until the response; size timeout accordingly and never overlap calls.

For `client_full_state_fixture`, run `arm_stall`, accelerate the server, then `inspect`. Require unchanged client/armed tick, changed timescale, and a pending full-state tick above client tick. Run `exercise_gap`; require both desync fields false, pending preservation, `uncappedConsecutiveEndpoint < pendingTick`, direct adoption required/proven, pending cleared, adopted ticks match, confirmed offset zero, obsolete updates absent, ring head adopted/valid, and render base not older than last rendered. `removedUpdateCount` may be zero. Always `clear` interrupted setup.

Capture commands temporarily restore a minimized client without activation, wait for extent settlement, capture, then re-minimize. An already-visible client fails if swapchain recreation is deferred. `resize`/`fullscreen` reject minimized state; restore first. After resize/fullscreen/restore, call `describe_ui` or `screenshot` to remeasure.

Only labeled visible ImGui items are addressable. Scroll a clipped container with `mouse` wheel, then retry. `describe_scene.units` are visible-area-filtered while counts span subscribed cells; edge projection may lie slightly outside the framebuffer. Only players have global IDs, and only the focused fleet lists members.

Injected mouse position owns `io.MousePos`: a `hover`/`click`/`mouse move` pins an ImGui mouse position that persists across scripts, so `describe_ui.mouse` and mouse-proximity UI keep that pixel. With no pin, a focused agent client's `describe_ui.mouse` reports ImGui's no-mouse sentinel rather than the real cursor, and the RawInput game mouse stays at a frozen boot-time snapshot (under agent suppression, physical mouse messages never reach DirectXTK Mouse).
