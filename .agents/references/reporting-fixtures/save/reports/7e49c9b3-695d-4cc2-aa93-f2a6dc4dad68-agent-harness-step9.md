Schema: be-agent-report/v1
Requested role: Opus/Terra process-verification subagent
Actual executor: Codex delegated subagent
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Documents/Plans/Save/ServerSaveFailureReporting.md; approved delta: none; C++ Code Change Process step 9 runtime verification

# Result

PASS. Every runtime item in the plan Verification section passed. No repository file was edited. The fixed compile result was used exactly: `DataBuildMode=Shared`, `RunDataPacker=false`, and `GameDataDirectory=<USER_HOME>\Documents\BrokenEnginePublic\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`. The compile report's complete 26-file relative-path/length/SHA-256 selected-data identity set matched before the server launch, before the client launch, and after verification. DataPacker was not run and no fallback data path was inferred or used.

# Harness lifecycle

- Provisioning: `.agents/scripts/Provision-WorktreeThirdParty.ps1 -RepositoryRoot <worktree>` exited 0; output confirmed shared worktree dependencies against `<USER_HOME>\Documents\BrokenEnginePublic`.
- Claim exit: 0.
- Claim metadata: owner `<GUID>`, session `save-step9-runtime`, claimedAt `2026-07-14T17:52:56.220Z`, worktree exactly as above.
- First command heartbeat proof: `heartbeatAt` advanced from `2026-07-14T17:52:56.220Z` to `2026-07-14T17:54:59.256Z` after server `ping`, `set_log_level`, and `status` commands.
- Server launch: `BrokenEngineSandboxServer.Debug.exe --agent-port 27100 --loopback-only --data-directory <exact GameDataDirectory> --log-file <unique worktree Temp path>`; PID 48364.
- Client launch: `BrokenEngineSandbox.Debug.exe --agent-port 27101 --loopback-only --data-directory <exact GameDataDirectory> --windowed 1600x900 --log-file <unique worktree Temp path>`; PID 44964.
- Initial server liveness: `ping -> {"ok":true,"result":{"build":"server","tick":222}}`; initial `status` showed active coord `[0,0]`, `clientCount:0`, `recording:false`, `replaying:false`.
- Client liveness/connection: client `ping -> build:"client"`; server status at tick 5812 showed `clientCount:1`; server logs showed connect, accepted hello, subscription, static data, and full state.
- Teardown: client `quit` exit 0 and server `quit` exit 0. Both processes exited; final `Get-Process BrokenEngineSandbox*` count was 0. The harness release command exited 0, and the process count remained 0 after release.

# Criterion verdicts

## H001 — PASS: unobstructed named agent save

- Precondition: `AgentHarnessSaveFailureReporting.save` absent.
- Command: `{"cmd":"save","params":{"file":"AgentHarnessSaveFailureReporting.save"},"id":"save-success"}`.
- Response/exit: exit 0, `{"id":"save-success","ok":true,"result":{"file":"AgentHarnessSaveFailureReporting.save"}}`.
- Artifact: regular file, length 803, SHA-256 `d25b9be00a2771d3b70e5e0697387647f647f447bfe136c63fb680490bd39c82` during the test.
- Log evidence at tick 594: opened `AgentHarnessSaveFailureReporting.save.tmp`, `WriteFileAtomically committed "AgentHarnessSaveFailureReporting.save"`, and `WriteGrid ... Committed: true`.

## H002 — PASS: obstructed named save reports failure

- Precondition: destination and `.tmp` absent; created a directory exactly at `<USER_HOME>\AppData\Roaming\Broken Engine Sandbox Server\AgentHarnessObstructed.save.tmp`.
- Command: `{"cmd":"save","params":{"file":"AgentHarnessObstructed.save"},"id":"save-obstructed"}`.
- Response/exit: exit 2, `{"error":"save failed to write 'AgentHarnessObstructed.save'","id":"save-obstructed","ok":false}`. There was no `result` object and therefore no `result.file`.
- Artifact evidence: final destination remained absent; obstruction remained a directory until deliberate removal.
- Log evidence at tick 1017: failed to open the exact `.tmp`, `WriteFileAtomically failed to open "AgentHarnessObstructed.save.tmp"`, and `WriteGrid ... Committed: false`.
- The obstruction was removed and both destination and `.tmp` were absent afterward.

## H003 — PASS: reserved-device and embedded-NUL validation before I/O

- Commands covered `NUL`, mixed-case `cOn.save`, `COM1.replay`, multi-extension `nul.replay.save`, `PRN`, `AUX`, `COM9`, `LPT1`, and `LPT9`. Every response exited 2 with `ok:false` and error `'file' must not use a reserved Windows device name`.
- Embedded-NUL request payload was exactly `{"cmd":"save","id":"embedded-nul","params":{"file":"NUL\u0000.save"}}`; response exited 2 with `ok:false` and error `'file' must not contain an embedded NUL`.
- The complete server-AppData content/type fingerprint immediately before and after all validation commands was identical: `5cd9df69d5b9a20aad90114edd3bd3e7fec16c93466a67f6800450c72ff0c898`.
- A matching I/O-log query returned `lines:[]`, proving validation rejected the names before any write attempt or artifact creation.

## H004 — PASS: connected-client quicksave failure warns and connection survives

- Client/server connection was healthy before the test (`clientCount:1`). A directory obstruction was held at exact default atomic target `ServerQuicksave.save.tmp` across command boundaries.
- Client command: `{"cmd":"key","params":{"key":"F5","holdFrames":1},"id":"client-quicksave-held"}` -> exit 0, `ok:true`.
- Server log evidence at tick 7828: `ServerSession::kClientSaveRequest Client: 1`; failed to open `ServerQuicksave.save.tmp`; `WriteFileAtomically failed to open "ServerQuicksave.save.tmp"`; `ServerSession::kClientSaveRequest ServerSave failed`.
- Severity/category identity is the exercised source site `ServerSession.cpp:274`, `LOG(kDefault, kWarning, "ServerSession::kClientSaveRequest ServerSave failed")`.
- Health after the diagnostic: server status tick 8170 showed `clientCount:1`, `recording:false`, `replaying:false`; client ping returned `ok:true`, build `client`, tick 8156.
- Timing note: an initial setup attempt removed the obstruction before the asynchronously delivered packet arrived at tick 6559, producing a normal save. It was excluded from the failure verdict. The accepted retry held the obstruction until the tick-7828 warning was observed. The prelaunch snapshot restored the incidental successful write byte-for-byte.

## H005 — PASS: replay start and stop failure branches

### Grid/start failure

- Obstructed exact `F7.replay.grid.tmp` with a directory while the prior `F7.replay.grid` hash was `9a1d1c7d646a491f3254e4afad61850ca6de2897c8c1858fdf035f5b606b3ebc`.
- First `replay_record {start:true}` returned `pending:true`; next status at tick 3048 showed `recording:false`. Repeating the same start request again returned `pending:true`, proving the prior toggle had been consumed; status at tick 3050 again showed `recording:false`.
- Both ticks logged the `.tmp` open failure and `Replay grid write failed; recording not started`. The matching log set contained no `Recording started` line. The existing grid hash remained unchanged. No writer existed (`status.recording:false`).

### Manifest stop failure

- Started normally; status tick 3642 showed `recording:true` and log `Recording started for 1 coords`.
- Obstructed `F7.replay.manifest.tmp`, then requested stop. Status tick 3644 showed `recording:false`.
- Tick-3644 order: manifest open failed; `F7.replay.0`, `.frames`, `.checksums`, and `.fullframes` each committed afterward; `F7.replay.meta` then committed; final diagnostic was `Replay persistence failed; recording stopped without a complete replay`.

### Metadata stop failure

- Started normally; status showed `recording:true`.
- Obstructed `F7.replay.meta.tmp`, then stopped. Status tick 4367 showed `recording:false`.
- Tick-4367 order: manifest committed; all four coordinate writer siblings committed; metadata open then failed; final diagnostic was the replay-persistence error.

### Coordinate-writer stop failure and sibling cleanup

- Started normally; status tick 5059 showed `recording:true`.
- Obstructed `F7.replay.0.frames.tmp`, then stopped. Status tick 5062 showed `recording:false`.
- Tick-5062 order: coordinate header committed; frames open failed; later checksums and fullframes still committed; `DifferenceStreamWriter save failed writing "F7.replay.0.frames"; deleting partial replay set`; cleanup attempted all four final siblings; metadata committed afterward; final diagnostic was the replay-persistence error.
- Post-stop artifact query proved cleanup: `F7.replay.0`, `.frames`, `.checksums`, and `.fullframes` were all absent. Metadata existed. The `.tmp` obstruction survived as a directory and was then deliberately removed.
- Before the later success run, repeated `get_logs` queries for the exact success text returned `lines:[]`; each failed stop emitted only the error path. Severity/category identity is the exercised `GameSaveLoad.cpp:373` `LOG(kDefault, kError, ...)` site.
- Every obstruction was removed. A directory enumeration before the success run returned `APPDATA_DIRECTORY_OBSTRUCTIONS=0`.

## H006 — PASS: normal short record/stop/playback

- Start response: exit 0, `pending:true`. Statuses at ticks 9803 through 9807 consistently showed `recording:true`, `clientCount:1`.
- Stop response: exit 0, `pending:true`. Status tick 9809 showed `recording:false`.
- Success logs: tick 9802 `Recording started for 1 coords`; tick 9809 all manifest/coordinate-sibling/metadata commits followed by exact `Recording stopped`.
- Complete output set existed after stop:
  - `F7.replay.grid`: 803 bytes, SHA-256 `09232ff2dead022c80c017437c4a0c1892f459e2a366380205d938eef1bd6ce9`
  - `F7.replay.manifest`: 24 bytes, SHA-256 `2dab308cb068a699e95029eb3f3893fa28b026a46950d57233712057fcd8bf99`
  - `F7.replay.meta`: 40 bytes, SHA-256 `bf0f18231cf693a802531b339ff98a28b97a186bc838a74fa7cdc15bd37b3597`
  - `F7.replay.0`: 686 bytes, SHA-256 `61c2eb865e1f390a0a019c2ab18752703c4ce77b1e36e15bbe95e804681112fe`
  - `F7.replay.0.frames`: 0 bytes, SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
  - `F7.replay.0.checksums`: 64 bytes, SHA-256 `46c04e6d54183954e51f4205e91a2103ab99d534f532f957ed164da690af2800`
  - `F7.replay.0.fullframes`: 2552 bytes, SHA-256 `0b9748939decfe6ca34c50d0bb39bdf5727caa1a25ef4001acc6ee60e616efa2`
- Playback command returned exit 0, `pending:true`. Status showed `replaying:true` and tick progression 9802, 9804, 9805, 9806, 9807, 9808. At the loop boundary it logged `End replay 9809, looping`, reloaded, and returned to `replaying:true`.
- A scoped 31-line playback delta logged successful reads of metadata (`iVersion: 2 == 2`, size 24), manifest, grid (`iVersion: 186`, one frame), coordinate header, checksums, and fullframes, plus reset/broadcast/load behavior twice across the loop.
- The scoped delta scan found zero occurrences of version mismatch, manifest-version inequality, compatibility, desync, checksum mismatch, CRC mismatch, corrupt replay, failed replay read/load, or `SaveLoadReplay aborted` diagnostics.
- A second `replay_play` cancelled playback; next status showed `replaying:false` and the connection remained present.

# H007 — PASS: external-state restoration and cleanup

## Preserved roots

- Server AppData: `<USER_HOME>\AppData\Roaming\Broken Engine Sandbox Server`.
- Client AppData: `<USER_HOME>\AppData\Roaming\Broken Engine Sandbox`.
- Before any launch, every entry beneath both roots was copied to a unique worktree-Temp backup and compared by relative path, type, length, and SHA-256. Initial original/backup fingerprints matched: server 168 entries, `751cecd812ebc51a1140f8d5d4b96c915e58098f46c9fd70a2f8f9bf846687e5`; client 6 entries, `437441f62d2f1c946b8e1e6679572d3b951fcced229b0bcb488f423739969bb8`.
- After clean process exit, the live roots were restored from those snapshots. A second normalized relative-path/type/length/SHA-256 comparison returned zero differences: server backup/restored fingerprint `f2a65b3045cfb575e113441c13edec1923d8bfc91dc3f03eab871b6f751bd731` with 168 entries; client backup/restored fingerprint `55e99abb8f27b284ce92620ff198894d6ec09497aea4e47cbcc6f821a07dc704` with 6 entries. The fingerprint strings differ from the first pair only because the second comparison used a different row encoding; each original-vs-backup and backup-vs-restored pair matched exactly.
- The server live root had 182 entries before restore and 168 after restore, so generated timestamp backups and all other new siblings were removed by the exact snapshot restore.

## Existing artifact byte restoration

- Server restored hashes matched their prelaunch values exactly:
  - `ServerQuicksave.save` `c9a515973ab21032b576214c76c34aab05c0b95f266e84fed4388f369775d1ad`
  - `ServerAutosave.save` `9d7023639ec50460fcf7fca93d044781977827712ec420812ac2c56cf0624194`
  - `F7.replay.grid` `9a1d1c7d646a491f3254e4afad61850ca6de2897c8c1858fdf035f5b606b3ebc`
  - `F7.replay.manifest` `2dab308cb068a699e95029eb3f3893fa28b026a46950d57233712057fcd8bf99`
  - `F7.replay.meta` `bf0f18231cf693a802531b339ff98a28b97a186bc838a74fa7cdc15bd37b3597`
  - `F7.replay.0` `378be6ad9627cf86784521f09df1d2035bc278c98becfaf075ec0dff7564c32f`
  - `F7.replay.0.frames` `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
  - `F7.replay.0.checksums` `e5a09b21258f4af29d297ef8584b651627f844cf3baf8823deae84e2eeac2144`
  - `F7.replay.0.fullframes` `4455f5d3f67f909e58fa67a2fec171fb03b1525059b06b619ae783158222036d`
- Client settings restored hashes matched their prelaunch values exactly:
  - `ClientState.bin` `836245378bb69a6b8632a9707b0fcdae0fb6bbb14736bb9e906f6032c8989821`
  - `GraphicsSettings.bin` `dd5a48bfac96c00061479b318f2411fbc14aa36dcf33bd87bfce8cb2a6097baa`
  - `SoundSettings.bin` `9fa671b362bab24cb30a70109f76f9f15a99e07dff76149d65d1c7193b422bbd`
  - `TweaksSettings.bin` `f331bfce6317b1a4001c5d8cbf41b74f154b1ed53e7adf1fc603df68727148de`

## Removed test artifacts

- Confirmed absent after restore: `AgentHarnessSaveFailureReporting.save`, its `.tmp`; `AgentHarnessObstructed.save`, its `.tmp`; `F7.replay.grid.tmp`; `F7.replay.manifest.tmp`; `F7.replay.meta.tmp`; `F7.replay.0.frames.tmp`; and `ServerQuicksave.save.tmp`.
- The worktree-Temp external-state backup and both unique harness log sinks were removed after evidence extraction. No BrokenEngineSandbox process remained. Harness claim release exited 0.
- External-state classification: both AppData roots restored; compile-selected Shared data unchanged; no external residual.

# Residual chain carried forward

- R001: Pre-existing mixed-generation replay persistence risk (adversarial R002 / fix R001) remains routed to step 11. It was not reproduced by any in-scope acceptance check: the normal final recording produced a complete same-generation sibling set, loaded, looped, and checksum/error scan passed.
- R002: PowerShell 5.1 provisioning-hook incompatibility (compile R001) remains routed to step 11. It did not block this harness run: direct wrapper-shell provisioning exited 0, no build or DataPacker was run here, and the selected data identity remained unchanged.

Files changed: none
Functions/regions touched: none
Residuals:
- R001: pre-existing mixed-generation replay persistence risk routed to step 11; not reproduced in scope.
- R002: PowerShell 5.1 provisioning-hook incompatibility routed to step 11; did not block harness verification.
