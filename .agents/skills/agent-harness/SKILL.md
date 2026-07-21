---
name: agent-harness
description: Drive the Broken Engine client/server for automated verification — launch both executables with an agent command channel and send JSON commands to control the sim, drive the UI, and read back scene/UI/log/screenshot state. Use for runtime-observable acceptance criteria, replay determinism, or whenever the user or a plan's Verification section asks to launch, drive, query, or screenshot the game.
allowed-tools: [PowerShell]
---

# Agent Interaction Harness

Drive the local headless server and rendered client through loopback, length-prefixed JSON. Use server port `27100`, client port `27101`, and the absolute adopted worktree as `$ROOT`. A local server is a development instance, not an Azure production server.

Read the focused references only when applicable:

- Read [Command reference](references/command-reference.md) before sending a command; it owns every parameter/result schema and command caveat.
- Read [Private-LAN firewall](references/private-lan-firewall.md) only for an explicitly requested cross-machine or private-Wi-Fi scenario. Ordinary same-machine runs stay loopback-only and never inspect or change firewall state.

## Provision and claim

Provision the checkout and use only its provisioned primary AgentHarness output. Wrapper sessions use their existing WorktreeCli session owner; a non-worktree checkout may let the provisioner create a transient provisioning session.

```powershell
& "$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot $ROOT
if ($LASTEXITCODE -ne 0) { throw "Worktree provisioning failed: $LASTEXITCODE" }
$AgentHarness = Join-Path $ROOT 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
if (-not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) { throw "AgentHarness missing: '$AgentHarness'." }

$Owner = & $AgentHarness lock token
$Session = '<short task label>'
& $AgentHarness lock claim --key default --owner $Owner --session $Session --worktree $ROOT
$ClaimExit = $LASTEXITCODE
if ($ClaimExit -eq 2) { & $AgentHarness lock status --key default; throw 'Harness owned by another session' }
if ($ClaimExit -ne 0) { throw "Harness claim failed: $ClaimExit" }
```

Report successful claim metadata verbatim. Hold one owner token across relaunches. Pass `--owner $Owner` on every socket command. After the first command, compare `lock status` before/after and require `heartbeatAt` to advance. During a non-harness phase that may exceed five minutes, run `lock heartbeat --key default --owner $Owner`; otherwise quit and release before the phase, then reclaim afterward.

Never build AgentHarness or write through shared Output links during routine harness work; `/compile` owns AgentTools candidate production and promotion. Stop if a required executable is absent.

## Ownership and takeover

A claim is stale only when its reported heartbeat is older than five minutes. Never disturb a fresh owner. For a stale owner:

1. Record the old owner and resolve listeners on ports `27100` and `27101` with `Get-NetTCPConnection -State Listen`.
2. Resolve each `OwningProcess` through `Get-CimInstance Win32_Process`. Require its normalized `ExecutablePath` to equal the expected server/client executable beneath the reported owner worktree and its `CommandLine` to contain the matching `--agent-port`. If listener identity, command line, or worktree cannot be proved, return `BLOCKED`; do not stop it.
3. Send `quit` to both ports with `--owner $OldOwner`. Wait only on the exact validated listener PIDs. If a validated PID remains and the command could not connect, stop only that exact PID with `Stop-Process -Id`; never use a process-name search or broad kill.
4. Generate a new token and conditionally steal with the recorded owner:

```powershell
$Owner = & $AgentHarness lock token
& $AgentHarness lock steal --key default --expect $OldOwner --owner $Owner --session $Session --worktree $ROOT
if ($LASTEXITCODE -ne 0) { throw 'Harness ownership changed during takeover' }
```

Old-owner cleanup may refresh its heartbeat; that does not invalidate staleness established before cleanup. `--expect` still protects against an ownership change.

## Launch

Require the latest `/compile` result's `DataBuildMode`, `RunDataPacker=false`, canonical `GameDataDirectory`, and selected-data required-file identity snapshot. Recheck the selected snapshot before each launch. In Local mode, also recheck the primary snapshot before launch and after verification. Stop on any changed, missing, or newly appeared required file. Never infer or switch data mode, fall back to Shared data, or run DataPacker/Gaea/texture export.

Use the compiled configuration suffix. Ordinary same-machine runs pass `--loopback-only`. Create log parents under `$ROOT\Temp`. Do not change process working directories; `--data-directory` is the only data-root override.

On Codex for Windows, launch with hidden `Start-Process -PassThru`, retain the returned exact PIDs, and use only those PIDs for lifecycle checks. Quote path-valued arguments because `Start-Process` joins `ArgumentList` items.

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

Agent-mode executables start minimized without activation. Capture commands temporarily restore the client without activation and re-minimize it. Omit `--windowed` only when native-resolution UI sizing/readability is part of acceptance. Debug/Profile clients auto-connect; Release requires `click "LOCAL SERVER"`. The server loads its exit autosave, so use `reset` when the scenario needs fresh state.

Before relinking or relaunching, send `quit` and wait for the retained exact PID. A live executable locks its image. Do not launch a duplicate to displace it.

## Invoke commands

Prefer stdin JSON and always capture stdout even when exit is nonzero:

```powershell
'{"cmd":"status"}' | & $AgentHarness --owner $Owner --port 27100 -
```

Use `--timeout-ms N` for a deferred client command; default is 15000 and maximum is 600000. Exit `0` means parsed `ok:true`, exit `2` means parsed `ok:false`, and exit `1` means transport, usage, or OS failure.

Request envelope: `{"cmd":"<name>","params":{...},"id":<optional JSON>}`. Omit `params` only for parameterless commands. Unknown top-level fields fail. Responses are `{"id":<echo|null>,"ok":true,"result":{...}}` or `{"id":...,"ok":false,"error":"..."}`. Parameters are an external trust boundary and invalid values must produce the error envelope.

The channel permits one in-flight request. Client deferred commands occupy it until completion; another AgentHarness call waits rather than bypassing it. Do not overlap calls.

## Canonical verification

1. Set up server state with `reset`, then `spawn_players` or `inject_status_changes` at a coord from `status.activeCoords`; confirm through `query_players`/`query_frame`.
2. Launch/connect the client and require `status.clientCount` to increase.
3. Use `describe_ui` before label-addressed `click`, `hover`, or `set_slider`; use `key`/`mouse` for raw input.
4. Verify with the narrowest observable combination of `describe_scene`, `describe_ui`, `screenshot`, server queries, and `get_logs`. Stable counts such as players should agree exactly; allow bounded tick drift for churning collections.
5. Release through the lifecycle checklist below.

### Replay determinism acceptance

Run replay acceptance only on a `kbDebugInput` build. It must prove transitions and a completed playback loop, not merely the absence of old errors:

1. Set the server `Default` log level to `Debug`. Capture server and client relevant-log baselines with `get_logs {"count":512,"pattern":"[Rr]eplay|CRC|[Cc]hecksum|[Dd]esync"}`. Preserve the ordered arrays so later checks can identify only appended lines.
2. Send `pause {"paused":false}` and require `status.paused:false`, `recording:false`, and `replaying:false`.
3. Send `replay_record {"start":true}` and require a later status with `recording:true` and `paused:false`. Let multiple ticks complete.
4. Send `replay_record {"start":false}` and require a later status with `recording:false` and `paused:false`.
5. Send `replay_play`; require `replaying:true` and `paused:false`. Wait for a newly appended server line matching `End replay [0-9]+, looping`. This current runtime marker proves every reader reached its recorded endpoint and the first playback loop completed.
6. Compare post-run relevant logs with both ordered baselines. Require no newly appended replay-persistence/read failure, `LogDifferences CRC Client`, checksum-mismatch, `CONFIRMED DESYNC`, or unresolved-CRC error line. Do not count pre-baseline lines as new evidence, and do not treat speculative reconciliation messages alone as a replay failure.
7. Send `replay_play` again to cancel. Require a later status with `replaying:false`, `recording:false`, and `paused:false`.

If the 512-line relevant-log window cannot retain the baseline through this bounded scenario, return `BLOCKED` and rerun with file-offset evidence from the configured per-process logs; never downgrade to “silence = pass.”

## Lifecycle and release

After every successful, failed, crashed, or abandoned launch attempt:

1. Send `quit` to both ports with the current owner; server quit autosaves.
2. Wait for `$ServerPid` and `$ClientPid` when assigned, then verify only those exact PIDs are absent. Stop only a retained exact PID when clean quit cannot connect or complete. Never use `Get-Process BrokenEngineSandbox*` or a name-based kill.
3. Run `lock release --key default --owner $Owner` and report exit `0` verbatim.
4. In Local data mode, recheck the primary identity snapshot.

An owner mismatch is a hard stop. Never remove coordination state manually.

## Process verification report

Run plan-provided runtime steps when present; otherwise derive the smallest live checks for runtime-observable criteria only. Report each criterion `PASS`, `FAIL`, or `BLOCKED` with exact command/query/scene/UI/screenshot/log evidence. Treat setup limitations as blocked checks. Do not diagnose or edit a failure in this role; return reproducing commands and evidence to the main agent for `/resolve-findings` adjudication and affected-check retest.

If a required command, parameter, result field, query, or input primitive is missing, return that criterion `BLOCKED`. Name the missing capability and the narrowest harness extension that would expose it. The main agent decides whether the authorized change includes that extension or whether user authority/criterion revision is required. Never fake state with pixel guessing or log scraping, create an out-of-scope runtime edit, waive the gate with a follow-up plan, or silently skip the criterion.

End delegated process verification with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed criterion, blocked prerequisite/missing capability, or none>
```

Return the complete report inline. A failed or blocked in-scope criterion remains incomplete until the capability/environment is supplied or the user explicitly revises acceptance.

## Durable caveats

- Client weapon-mode requests received during paused or other zero-tick updates and internally queued flagship navigation updates persist until the first advancing update. Client `kClientFleetNavigationDelay` requests apply immediately. To verify load-requeued flagship updates, use `load {"pauseAfterLoad":true}` and inspect `pendingFlagshipUpdateCount` before unpausing.
- Injection during replay fails. Injection while clients wait for spawn also fails immediately so it cannot corrupt snapshot-diff assignment; it is never queued for later. Injection while paused remains accepted with `deferred:true` and applies on the next unpaused tick.
- Physical mouse, keyboard, wheel, and gamepad input is suppressed for an agent-mode client. Synthetic input is the sole game/ImGui source; Alt+F4 and window Close still quit. Focus messages remain real, and audio requires real OS focus.
- Keep ownership warm during long soaks. Never leave a background heartbeat/poll process after the session.
- Server frame-read query schema and extraction live in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServerQueries.cpp`; simulation control, replay, profile, injection, and dispatch live in `AgentCommandsServer.cpp`. Client capture/input/UI/scene commands live in `AgentCommandsClient.cpp` and `AgentScene.cpp`.
