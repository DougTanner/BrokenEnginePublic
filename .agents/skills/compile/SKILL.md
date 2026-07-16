---
name: compile
description: Builds Broken Engine projects through WorktreeCli's serialized MSBuild driver and governs immutable prebuilt AgentTools bootstrap/maintenance policy. Use whenever you need to build, rebuild, compile, or check for compile/link errors in ThirdParty, DataPacker, AgentHarness, WorktreeCli, or BrokenEngineSandbox (client or server).
allowed-tools: [PowerShell]
---

# Build

Builds through the current checkout's WorktreeCli executable. `WorktreeCli build` serializes writers per target basename inside the current worktree, waits up to 660 seconds, preserves native Windows argument boundaries, and owns MSBuild through a kill-on-close Job Object.

## Bootstrap AgentTools

The canonical executables are `$ROOT\Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` and `$ROOT\Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe`. The Codex and Claude wrappers hold a live WorktreeCli session claim before bootstrap, worktree creation, provisioning, and client launch. Bootstrap atomically maintains both executables; routine work starts with their wrapper-provisioned immutable primary Outputs and must retain that claim. Never build either executable or write through either shared Output link in a routine worktree:

```powershell
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)) { throw 'Live wrapper WorktreeCli session claim is required.' }
& "$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot $ROOT
if ($LASTEXITCODE -ne 0) { throw "Worktree provisioning failed: $LASTEXITCODE" }
$WorktreeCli = Join-Path $ROOT 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
$AgentHarness = Join-Path $ROOT 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
if (-not (Test-Path -LiteralPath $WorktreeCli -PathType Leaf) -or -not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) { throw 'AgentTools output is incomplete.' }
```

If either primary prebuilt executable is missing, stop: worktree wrappers own AgentTools bootstrap before worktree creation. Routine mode neither builds a tool nor mutates its shared Output.

### Explicit primary-maintenance mode

Use this narrow mode only when the user explicitly authorizes AgentTools maintenance in the canonical primary checkout. The manager must supply `$PRIMARY`, the fixed `$BASELINE`, and the exact approved changed-path set. Before building, require all of the following:

- For initial rollout only, the user has explicitly confirmed every legacy pre-protocol session is closed; pass `-LegacySessionsClosed` when initializing the ledger. Later maintenance trusts the validated ledger.
- `$PRIMARY` is absolute, equals `git rev-parse --show-toplevel`, contains an ordinary `.git` directory, and that directory equals `git rev-parse --path-format=absolute --git-common-dir`.
- `git symbolic-ref --quiet --short HEAD` succeeds, `HEAD` descends from the fixed `$BASELINE`, and no merge, rebase, cherry-pick, revert, bisect, or sequencer operation is in progress.
- The normalized, deduplicated union of committed paths from `git -C $PRIMARY diff --name-only $BASELINE..HEAD --`, staged paths from `git -C $PRIMARY diff --cached --name-only --`, unstaged paths from `git -C $PRIMARY diff --name-only --`, and untracked paths from `git -C $PRIMARY ls-files --others --exclude-standard` is contained in the exact manager-supplied approved changed-path set. This permits approved commits made after the fixed direct-primary session baseline; any path outside the approved set is a hard stop.
- Both `Tools\WorktreeCli\Platforms\VisualStudio2026\Output` and `Tools\AgentHarness\Platforms\VisualStudio2026\Output` are ordinary primary directories when present, not reparse points.

Run the bundled sidecar. It enforces the preconditions above, locates Visual Studio 2026 MSBuild at the exact Community path with the documented `vswhere` fallback, acquires exclusive maintenance for the full capability-check/build/recheck window, and owner-conditionally releases it in `finally`:

```powershell
& "$PRIMARY\.agents\skills\compile\scripts\Invoke-AgentToolsPrimaryMaintenance.ps1" `
	-Primary $PRIMARY -Baseline $BASELINE -ApprovedChangedPath $ApprovedChangedPath `
	-LegacySessionsClosed:$LegacySessionsClosed
```

`-WaitSeconds` defaults to 660. The sidecar builds both solutions under one maintenance claim, accepts a first rollout where both new executables are absent, rejects a partial set, and then requires both nonempty ordinary executables to pass their separate `--help` contracts. Report the returned direct MSBuild status and both exact executable paths. Do not copy or install either executable elsewhere.

## Determine what to build

- Default: BrokenEngineSandbox client Debug.
- If any changed file is shared (`Common/`, `Engine/`, or non-exclusive game code), build both client and server.
- ThirdParty builds only on explicit request. Missing source or library links are provisioning failures; never rebuild ThirdParty automatically.
- DataPacker builds Release only. WorktreeCli still supplies the normal worktree-local target serialization; DataPacker's `"BrokenEngineDataPacker"` mutex remains the only PC-global coordination exception.

Before any DataPacker, client, or server build, invoke `$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1 -RepositoryRoot $ROOT` and stop on failure. Validated stable primary submodule trees plus shared immutable ThirdParty, WorktreeCli, and AgentHarness Output directories are the only exceptions to worktree-local build artifacts.

For routine work, build the checkout supplied by the caller and use its current `HEAD` as the comparison baseline when one is needed. An isolated session worktree remains appropriate for queue operations, concurrent work, or a final-evidence gate, but is not a prerequisite for a targeted build. Keep existing build serialization and the explicit AgentTools primary-maintenance path; do not share mutable build output between checkouts.

For a delegated call, return the complete build result inline. A build does not
create an evidence artifact; a later final-evidence gate records its decisive
exit status and relevant diagnostics once.

DataPacker's mutex coordinates across worktrees and its shared chunks live under `%TEMP%\DataPacker\<Project>`; do not add another PC-global DataPacker lock or a checkout-local cache copy. Gaea raw and split intermediates use the single mutable `%TEMP%\DataPacker\<Project>\Gaea\Islands` cache; source-tree island leaves retain only tracked BC outputs.

**Run every build synchronously in the foreground and stay in-turn until the `WorktreeCli build` call returns an exit code — only then report. NEVER background a build (`run_in_background`, a `Monitor` watcher, `Start-Job`, or a trailing `&`) and then end your turn to await completion.** A delegated subagent that yields its turn while a build runs is not reliably re-woken when the build finishes, so the workflow stalls half-done — a common failure is ThirdParty completing while the client/server targets are never started — and no result is ever reported. Give each foreground `WorktreeCli build` call the maximum execution timeout the tool allows so WorktreeCli's 660-second lock wait cannot be preempted; a cold first build of a fresh worktree can approach that limit. If a call times out with the build still running, **re-invoke the same `WorktreeCli build` command** — WorktreeCli serializes per target and its incremental/tlog state continues the same build to completion — rather than backgrounding it. If blocking is genuinely unacceptable, poll the build's own output to completion within the same turn (repeated short reads in a loop) and report only once you hold the final per-target exit code; never hand the wait to a fire-and-forget watcher and stop. Orchestrated worktree game builds never run DataPacker, Gaea, or texture export. Ordinary builds always preserve `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; the explicit Microsoft PREfast verification mode below is the sole exception to `RunCodeAnalysis=false`. The bundled VS2026 clang-tidy crashes on this codebase.

## Select runtime data mode

Every worktree game build uses one data mode and one canonical directory for both client and server:

- **Shared** is the default for ordinary code changes. Set `$GameDataDirectory` to `$PRIMARY\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`; this mode consumes primary generated headers/packs and disables every DataPacker build/export step.
- **Local** is mandatory when tracked changes from `$BASELINE`, staged changes, unstaged changes, or untracked files touch `DataPacker/**`, `Engine/Data/**`, `Projects/BrokenEngineSandbox/Data/**`, `Common/DataFile.h`, generated-header logic, exporter versions/fingerprints, compression, chunk layout, or pack/manifest contracts. Set `$GameDataDirectory` to `$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`.
- A user may force Local. Never force Shared over a Local trigger. Local consumes only output the user explicitly prepared for the current worktree state; it never initiates export and never falls back to Shared data.

The game projects detect the canonical repository-root Git marker. A linked worktree has a `.git` file and defaults `RunDataPacker=false`; the primary checkout has a `.git` directory and preserves ordinary Local Visual Studio behavior by defaulting true. An explicit Local `RunDataPacker=true` permits deliberate worktree generation; Shared rejects true. This compile skill always passes false and never opts in.

Build the changed-path set from `git -C $ROOT diff --name-only $BASELINE --` plus `git -C $ROOT ls-files --others --exclude-standard`; normalize separators before matching. This single baseline diff includes committed, staged, and unstaged tracked changes. If a changed file may affect generated or serialized bytes and the path rules do not prove otherwise, select Local.

Set `$GeneratedDataIncludeRoot` to the canonical parent of `$GameDataDirectory` (consumers include `Data/...`) and pass these exact properties to every client/server `.sln` or `.vcxproj` build, including `--files`:

```powershell
$DataProperties = @(
	"/p:DataBuildMode=$DataBuildMode",
	'/p:RunDataPacker=false',
	"/p:GameDataDirectory=$GameDataDirectory",
	"/p:GeneratedDataIncludeRoot=$GeneratedDataIncludeRoot"
)
```

Before Shared compilation, require these ten nonempty headers: `Data.h`, `DataTypes.h`, `Audio.h`, `Font.h`, `Scene.h`, `Islands.h`, `Model.h`, `Shader.h`, `Texture.h`, `Raw.h`. Also require nonempty `.manifest` and `.pack` files for each of `Audio`, `Font`, `Scene`, `Islands`, `Model`, `Shader`, `Texture`, and `Raw`. The Shared directory must be absolute, outside `$ROOT`, and must remain byte-identical: snapshot the required files as normalized relative path + length + SHA-256 before the first build, recheck after each client/server build, and return that snapshot for the harness's pre-launch check. Any missing, added, removed, or changed required file fails the workflow.

Local mode may build the worktree Release DataPacker as a standalone compile check, but must never execute it. Before compiling either game target, require the same complete nonempty output set and explicit confirmation that the user prepared it after the current relevant worktree changes. If either check is absent, stop with `Local data is missing or stale; manual worktree DataPacker export required` and do not build, export, or fall back. Actual generation is a separate user-controlled action outside this skill.

Snapshot the primary required-file set before Local work and recheck it after each game build; any primary write fails the workflow. After the last game build, snapshot the complete selected Local output for the harness. Never copy worktree DataPacker/data source changes into `$PRIMARY` to test them.

## Explicit Microsoft PREfast verification mode

Use this mode only when an approved plan explicitly requires Microsoft PREfast verification. Retain every ordinary game-build protection above: the live wrapper claim, immutable prebuilt WorktreeCli, worktree provisioning and lifecycle validation, WorktreeCli target serialization, synchronous foreground execution, data-mode selection, canonical `@DataProperties`, complete-data checks, and selected/primary identity snapshots. Do not invoke MSBuild or `/analyze` outside WorktreeCli.

Force Clang-Tidy off and Microsoft code analysis on only for the Release target commands below. Do not override the projects' warnings-as-errors settings:

```powershell
# BrokenEngineSandbox client Release PREfast, then server Release PREfast.
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
```

Outside this explicitly authorized mode, keep `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; never infer PREfast authorization from a routine compile, rebuild, or link-error check.

## Full-build commands

```powershell
# ThirdParty: choose requested configuration.
& $WorktreeCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Profile' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# DataPacker: Release only.
& $WorktreeCli build "$ROOT\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# BrokenEngineSandbox client, then server. Substitute Profile/Release only when requested.
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

```

## Selective file compile

Use `--files` to invalidate specific project-member `.cpp` objects before a normal project build. Target a `.vcxproj`, supply both Configuration and Platform, and separate the source list from the target with `--`:

```powershell
& $WorktreeCli build --files "$ROOT\Engine\Source\Example.cpp" "$ROOT\Projects\BrokenEngineSandbox\Source\Example.cpp" -- `
	"$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj" `
	'/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
```

Only `.cpp` inputs already present in the target project are valid. After a header edit, list the affected project-member `.cpp` files. Add new files to the vcxproj/filters before compiling. Shader changes require a separately prepared Local export before the client build; the build never compiles shaders through DataPacker automatically.

## Report results

- For a delegated call, return the complete results inline. Keep
  overall/per-project status, data mode/path, and decisive blockers visible.
- Report only after every build has returned an exit code; do not end your turn (or hand back to the caller) while any build is still running. If you delegated this skill, the build ran in the foreground of your turn per the rule above — its exit code is in hand before you report.
- Final status per project: success or fail.
- Every error line verbatim.
- Warning lines verbatim only for files involved in the change.
- LNK1168 or EXE LNK2019 can mean a client/server process still holds the executable; report it rather than diagnosing unless asked.
- A prior killed build's `unsuccessfulbuild` marker clears on the next successful run; rerun instead of deleting tlogs.
- A lock timeout means another WorktreeCli build still owns that target. Retry after it finishes; never delete `.claude/build-locks/` manually.
- For game builds, report `DataBuildMode`, `RunDataPacker=false`, canonical `GameDataDirectory`, canonical `GeneratedDataIncludeRoot`, the selected-data identity snapshot, and the primary identity snapshot when Local. Report every mode-selection trigger and the Local prepared-data confirmation. A harness run must use this exact mode/path; it must not infer or substitute one.

End with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed or skipped required build, or none>
```
