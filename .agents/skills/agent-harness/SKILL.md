---
name: agent-harness
description: >-
  Drive the Broken Engine client/server for automated verification. Use for
  runtime-observable acceptance criteria, replay determinism, or whenever the
  user or a plan's Verification section asks to launch, drive, query, or
  screenshot the game, or requests GPU frame capture or RenderDoc capture
  analysis.
allowed-tools: [PowerShell]
---

# Agent Interaction Harness

Drive the local headless server and rendered client through loopback, length-prefixed JSON. Use server port `27100`, client port `27101`, and the absolute adopted worktree as `$ROOT`. A local server is a development instance, not an Azure production server.

Read the focused references only when applicable:

- Read the command reference (`references/command-reference.md`) for the four engine-shared command schemas (`ping`, `quit`, `get_logs`, `set_log_level`) and the `params`/`result` placement convention; the request/response envelope itself is defined in the Invoke commands section below. The selected project's `Projects/<Project>/Documents/AgentHarness.md` owns every game command schema, verification recipe, and game caveat.
- Read the private-LAN firewall reference (`references/private-lan-firewall.md`) only for an explicitly requested cross-machine or private-Wi-Fi scenario. Ordinary same-machine runs stay loopback-only and never inspect or change firewall state.
- Read the RenderDoc capture reference (`references/renderdoc.md`) only for a GPU frame-capture or capture-analysis scenario; it owns the `--renderdoc` launch, `renderdoc_capture`, and the headless `rdc_*` analysis scripts.

## Select the project

Target the project the latest `/compile` result built, unless the user or plan names a different one. Default today: BrokenEngineSandbox. Before launching, read `Projects/<Project>/Documents/AgentHarness.md`; it owns the executable names, output directory, extra launch arguments, game command schemas, and authoritative verification recipes referenced throughout this skill.

## Provision and claim

Provision the checkout and use only its provisioned primary AgentHarness output. Wrapper sessions use their existing WorktreeCli session owner; a non-worktree checkout may let the provisioner create a transient provisioning session.

Claim through `scripts/Invoke-HarnessClaim.ps1`. It requires the selected project's server and client executables (reading their names and `Output` directory from the project harness doc's launch block), provisions the checkout, requires the resolved `AgentHarness.exe`, mints the owner token, and claims the lock, printing one compact `broken-engine-harness-claim/v1` JSON object. Add `-Configuration <name>` only for a build other than `Debug`. In Codex's PowerShell 7 terminal:

```powershell
$Script = Join-Path $ROOT '.agents\skills\agent-harness\scripts\Invoke-HarnessClaim.ps1'
pwsh -NoProfile -ExecutionPolicy Bypass -File $Script -RepositoryRoot $ROOT -Session '<short task label>'
```

In Claude Code's Git Bash terminal, convert the script path first — do the same for every script invocation below:

```bash
script="$(cygpath -w "$ROOT/.agents/skills/agent-harness/scripts/Invoke-HarnessClaim.ps1")"
pwsh -NoProfile -ExecutionPolicy Bypass -File "$script" -RepositoryRoot "$(cygpath -w "$ROOT")" -Session '<short task label>'
```

Exit `0` (`status` `pass`) supplies the `owner` token, the resolved `agentHarness` path, and the `claim` record; hold that token in `$Owner`, keep that path in `$AgentHarness`, and report the claim metadata verbatim. Exit `2` (`status` `blocked`) carries one of two codes. `claim.executable-missing` means a required project executable is absent for the requested configuration: no provisioning ran and no lock was taken, so route `/compile` for the named executables and re-enter. `claim.foreign-owner` means another session holds the lock: the payload's `currentOwner` carries its `claimedAt` and the derived `holdSeconds` — heartbeat never advances `claimedAt` — so decide between waiting and reordering non-harness work instead of polling blind. The script never steals, never waits for the lock, and never touches a foreign owner's processes. Exit `1` is a failure naming its step (`claim.repository-missing`, `claim.provisioner-missing`, `claim.launch-doc-unreadable`, `claim.provision-failed`, `claim.harness-missing`, `claim.token-failed`, `claim.metadata-unreadable`, `claim.failed`, or `internal.error`); stop and report it. Never reconstruct provisioning, harness-path resolution, token minting, or the claim inline.

Hold one owner token across relaunches. Every socket command requires `--owner $Owner`: after request acquisition, AgentHarness proves ownership before Winsock/connect, refreshes it when the 60-second interval becomes due while transport remains active, and refreshes it once more before response output. Ownership loss stops local response handling with exit `1`; game work already dispatched cannot be retracted. After the first command, compare `lock status` before/after and require `heartbeatAt` to advance. `lock status` prints ordinary pretty-printed JSON; the backslashes in its `worktree` field are standard JSON escaping of the Windows path, not nested or stringified JSON. During a non-harness phase that may exceed five minutes, run `lock heartbeat --key default --owner $Owner`; otherwise quit and release before the phase, then reclaim afterward.

Never build AgentHarness or write through shared Output links during routine harness work; `/compile` owns AgentTools newly-built-binary production and promotion. The claim step itself verifies that the selected project's server and client executables exist, so a missing one blocks the claim instead of surfacing later.

## Ownership and takeover

A claim is stale only when its reported heartbeat is older than five minutes. Never disturb a fresh owner. For a stale owner:

1. Record the old owner and resolve listeners on ports `27100` and `27101` with `Get-NetTCPConnection -State Listen`.
2. Resolve each `OwningProcess` through `Get-CimInstance Win32_Process`. Require its normalized `ExecutablePath` to equal the active project's expected server/client executable (per its harness doc) beneath the reported owner worktree and its `CommandLine` to contain the matching `--agent-port`. If listener identity, command line, or worktree cannot be proved, return `BLOCKED`; do not stop it.
3. Send `quit` to both ports with `--owner $OldOwner`. Wait only on the exact validated listener PIDs. If a validated PID remains and the command could not connect, stop only that exact PID with `Stop-Process -Id`; never use a process-name search or broad kill.
4. Generate a new token and conditionally steal with the recorded owner:

```powershell
$Owner = & $AgentHarness lock token
& $AgentHarness lock steal --key default --expect $OldOwner --owner $Owner --session $Session --worktree $ROOT
if ($LASTEXITCODE -ne 0) { throw 'Harness ownership changed during takeover' }
```

Old-owner cleanup may refresh its heartbeat; that does not invalidate staleness established before cleanup. `--expect` still protects against an ownership change.

## Launch

Require the latest `/compile` result's `DataBuildMode`, `RunDataPacker=false`, normalized `GameDataDirectory`, fixed data baseline, and the path and SHA-256 of the selected `broken-engine-data-oracle/v1` receipt — the record file proving exactly which data files the build used. Before each launch and after verification, invoke the compile package's `scripts/Test-DataOracleReceipt.ps1` with that exact receipt/path/mode/baseline tuple and require its typed passing result. In Local mode, do the same for the independent primary Shared receipt. Stop on any receipt, path, mode, baseline, inventory, or byte mismatch. Never infer an identity, compare Shared and Local receipts for equality, switch data mode, fall back to Shared data, or run DataPacker/Gaea/texture export.

Use the compiled configuration suffix. Ordinary same-machine runs pass `--loopback-only`. Create log parents under `$ROOT\Temp`. Do not change process working directories; `--data-directory` is the only data-root override.

On Codex for Windows, launch with hidden `Start-Process -PassThru`, retain the returned exact PIDs, and use only those PIDs for lifecycle checks. Quote path-valued arguments because `Start-Process` joins `ArgumentList` items.

The selected project's harness doc owns the concrete launch block — the server/client executable paths beneath its `Output` directory and any extra project launch arguments — plus its game-specific connect and fresh-state notes. The `--agent-port` values are fixed by this skill (server `27100`, client `27101`) and embedded in that launch block.

Agent-mode executables start minimized without activation. Capture commands temporarily restore the client without activation and re-minimize it. A criterion that depends on render progression must hold a visible window for its duration — capture restores an iconic window only for the readback and re-minimizes it, so a capture taken mid-scenario silently returns the client to the non-rendering state. Use `window_state` to hold visibility across such a scenario. Omit `--windowed` only when native-resolution UI sizing/readability is part of acceptance. Add the optional `--renderdoc` client argument only for GPU frame capture; it force-loads renderdoc.dll and drops the Vulkan validation layer, so keep it off ordinary runs (see the RenderDoc capture reference `references/renderdoc.md`).

After launching either executable, wait for readiness with `scripts/Wait-HarnessPing.ps1` before the first real command. It handles one port per call, so launch order stays with you:

```powershell
$Script = Join-Path $ROOT '.agents\skills\agent-harness\scripts\Wait-HarnessPing.ps1'
pwsh -NoProfile -ExecutionPolicy Bypass -File $Script -AgentHarness $AgentHarness -Owner $Owner -Port 27100 -TimeoutSeconds 120
```

The script polls until the port answers `ok:true`, tolerating the individual timeouts long client startup (terrain elevation and priority-texture waits) produces after connect already succeeded. Exit `0` (`status` `pass`) reports the observed `tick`; `tick` of `-1` means the listener is up but the game is not yet created (see the command reference `references/command-reference.md` for `ping`). Exit `2` (`status` `blocked`, code `ping.timeout`) reports the attempt count and elapsed time, and blocks the first real command. Exit `1` is a setup failure. Never hand-write a ping loop or invent a sleep window in its place.

Invoke `scripts/Wait-IslandSceneReady.ps1` only after launch and only when an approved criterion depends on island footprints or rendered islands. Supply the exact `-AgentHarness`, harness-lock `-Owner`, client `-ClientPort`, bounded `-TimeoutSeconds`, and absolute ignored `-ArtifactPath`; do not rename or replace the retained `$ServerPid`/`$ClientPid` lifecycle variables. The helper requires client `ping` with `tick >= 0`, restores the client with `window_state {minimized:false}`, and requires the complete `clientGridCoord` plus nonempty island footprints to be byte-stable across two consecutive normalized samples. Exit `0` is usable only with a `broken-engine-island-scene-readiness/v1` artifact reporting `Status:success`, `Code:ready`, and `Ready:true`; missing, malformed, or failed evidence blocks the criterion. The selected project document owns the concrete invocation.

Before relinking or relaunching, send `quit` and wait for the retained exact PID. A live executable locks its image. Do not launch a duplicate to displace it — with `SO_REUSEADDR`, a duplicate on the same port binds alongside the live listener instead of failing fast, and connection routing between the two becomes nondeterministic. The engine listener sets `SO_REUSEADDR` and briefly retries address-in-use binds, so relaunch immediately once the exact PID has exited and rely on the ping poll for readiness — never invent a sleep window.

## Invoke commands

Prefer stdin JSON and always capture stdout even when exit is nonzero:

```powershell
'{"cmd":"ping"}' | & $AgentHarness --owner $Owner --port 27100 -
```

Use `--timeout-ms N` for a deferred client command; default is 15000 and maximum is 600000. The CLI retries the loopback connect until this same deadline rather than making a single short attempt, so a probe against a dead port consumes the full budget — give a quick negative probe a small `--timeout-ms` (e.g. `1000`). Exit `0` means parsed `ok:true`, exit `2` means parsed `ok:false`, and exit `1` means transport, usage, or OS failure.

Request envelope: `{"cmd":"<name>","params":{...},"id":<optional JSON>}`. Omit `params` only for parameterless commands. Unknown top-level fields fail. Responses are `{"id":<echo|null>,"ok":true,"result":{...}}` or `{"id":...,"ok":false,"error":"..."}`. Parameters are an external trust boundary and invalid values must produce the error envelope.

The channel permits one request still in progress. Client deferred commands occupy it until completion; another AgentHarness call waits rather than bypassing it. Do not overlap calls.

Write any multi-line PowerShell driver — anything with `function` definitions, loops, or more than a few statements — to a script file (e.g. under `$ROOT\Temp`) and run it with `pwsh -File`. Never compact such a driver into a semicolon-joined one-liner; compaction corrupts function definitions (observed: `function Snap([string]$p)` mangled into an unrecognized `Snap$d` token).

## Authoritative verification

The selected project's harness doc owns the concrete setup recipe (which commands seed server state, confirm client connection, and address UI) and its replay determinism acceptance sequence. Regardless of project, hold these verification-evidence principles:

- Verify with the narrowest observable combination of the project's scene, UI, screenshot, server-query, and log commands. `describe_ui`, scene/server queries, and `get_logs` close a criterion more cheaply than pixels; reach for a capture only when the criterion is genuinely about what was rendered. Stable counts such as players should agree exactly; allow bounded tick drift for collections whose entries are being added and removed rapidly.
- Release through the lifecycle and release section below.

## Lifecycle and release

After every successful, failed, crashed, or abandoned launch attempt, release through `scripts/Invoke-HarnessRelease.ps1`. One call performs the whole sequence, and it is safe after a crashed run whose PIDs are already gone:

```powershell
$Script = Join-Path $ROOT '.agents\skills\agent-harness\scripts\Invoke-HarnessRelease.ps1'
pwsh -NoProfile -ExecutionPolicy Bypass -File $Script -AgentHarness $AgentHarness -Owner $Owner -ServerPid $ServerPid -ClientPid $ClientPid
```

Pass only the retained exact `$ServerPid`/`$ClientPid`, and omit either one that was never assigned. The script quits both ports with the owner (server quit autosaves), waits for those exact PIDs, stops only a supplied exact PID that survived the quit, and runs `lock release` only once every supplied PID is confirmed absent. It never searches by process name and never stops a PID you did not supply.

Exit `0` (`status` `pass`) means every supplied PID is absent and the lock was released; report the payload's per-step outcomes verbatim. Exit `2` (`status` `blocked`, code `release.process-survived`) means a supplied PID is still alive: no release was attempted, the claim stays held, and you decide whether to retry the release or escalate to the user. Exit `1` is a failure naming its step, including `release.lock-owner-mismatch` and `release.lock-absent`, which are never a success. Never reconstruct the quit, wait, stop, or release steps inline.

Then, in Local data mode, reverify the independent oracle for the primary Shared data.

An owner mismatch is a hard stop. Never remove coordination state manually.

## Process verification report

Run plan-provided runtime steps when present; otherwise derive the smallest live checks for runtime-observable criteria only. Report each criterion `PASS`, `FAIL`, or `BLOCKED` with exact command/query/scene/UI/screenshot/log evidence. Treat setup limitations as blocked checks. Do not diagnose or edit a failure in this role; return reproducing commands and evidence to the main agent for the `/resolve-findings` decision and affected-check retest.

Captures stay on disk. The `screenshot` result `{path, width, height}` is the evidence — cite it by path. Loading an image into context is a deliberate act for a check that genuinely needs pixels, and the report names which check and why.

- Prefer a script over the image. Most visual criteria — frame non-black, region matches an expected color, two captures differ, pixel count past a threshold — are assertions a few lines of code settle more precisely than an eye on a downscaled JPEG. Write the driver under `$ROOT\Temp` and run it with `pwsh -File` as above; its stdout is a few bytes of decisive text instead of megabytes of image. The same holds for a large driver result file: query it targeted first, and read the whole record only after a targeted read provably misses.
- Request only the resolution the check needs. `screenshot` downscales through `maxWidth` (default 1568) and `quality` (default 80), and those defaults are sized for UI readability. A "did terrain render at all" check does not need them; pass a `maxWidth` and `quality` matched to the assertion as part of designing the check.

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

The selected project's harness doc owns game-specific caveats (injection/pause/replay timing, queued-update semantics, and command source-file ownership). These engine-generic caveats hold for every project:

- Physical mouse, keyboard, wheel, and gamepad input is suppressed for an agent-mode client. Synthetic input is the sole game/ImGui source; Alt+F4 and window Close still quit. Focus messages remain real, and audio requires real OS focus.
- Keep ownership warm during long soaks. Never leave a background heartbeat/poll process after the session.
