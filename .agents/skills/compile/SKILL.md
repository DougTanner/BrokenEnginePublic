---
name: compile
description: Builds Broken Engine projects through WorktreeCli's serialized MSBuild driver and governs immutable prebuilt AgentTools bootstrap/maintenance policy. Use whenever you need to build, rebuild, compile, or check for compile/link errors in ThirdParty, DataPacker, AgentHarness, WorktreeCli, or BrokenEngineSandbox (client or server).
allowed-tools: [PowerShell]
---

# Build

Builds through the current checkout's WorktreeCli executable. `WorktreeCli build` serializes writers per target basename inside the current worktree, waits up to 660 seconds, preserves native Windows argument boundaries, and owns MSBuild through a kill-on-close Job Object.

## Structured build result

`WorktreeCli build` writes exactly one schema-versioned `broken-engine-build-result/v1` JSON object to stdout; human progress goes to stderr. The JSON — not scraped terminal text — is the authoritative result. Capture stdout, parse it, and read:

- `status` (`success`/`fail`), `failureKind` (`none`/`tool`/`msbuild`), and `exitCode` — the process exit code keeps its existing meaning (MSBuild's exit code once launched; `1` for tool failures including a retained-log failure after a successful build).
- `target`/`worktreeRoot` normalized identities, `arguments`, `selectedFiles`, `invalidatedObjects`.
- `lock` disposition (`acquired`/`timeout`/`failed`) with the lock path and waited seconds.
- `msbuild` discovery/launch state and MSBuild's own exit code.
- `retainedLog` — the complete combined MSBuild stdout+stderr stream in observed read order, untruncated, below the invoking worktree's ignored `Temp/AgentBuildLogs/`. `complete: false` or a missing log is a build-result failure, never an omitted side effect.
- `diagnostics` — structured MSBuild error/warning entries (`severity`, `code`, `file`, `line`, `column`, `project`, `message`, `raw`), capped with `diagnosticsTruncated: true` when the raw log holds more; `messages` carries tool failures and unmatched fatal lines.
- `elapsedMilliseconds` and `startedAt`.

## Bootstrap AgentTools

The canonical executables are `$ROOT\Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` and `$ROOT\Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe`. The Codex and Claude wrappers hold a live WorktreeCli session claim before bootstrap, worktree creation, provisioning, and client launch; wrapper sessions build under that claim (`BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER`). Non-worktree checkouts (typically the parent/primary checkout) may build without one — when that variable is empty, the provisioner registers a transient WorktreeCli session for the provisioning step automatically. In both modes, routine work starts with the immutable primary Outputs. Never build either executable or write through either shared Output link in a routine checkout:

```powershell
& "$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot $ROOT
if ($LASTEXITCODE -ne 0) { throw "Worktree provisioning failed: $LASTEXITCODE" }
$WorktreeCli = Join-Path $ROOT 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
$AgentHarness = Join-Path $ROOT 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
if (-not (Test-Path -LiteralPath $WorktreeCli -PathType Leaf) -or -not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) { throw 'AgentTools output is incomplete.' }
```

If either primary prebuilt executable is missing, stop: worktree wrappers own AgentTools bootstrap before worktree creation. Routine mode neither builds a tool nor mutates its shared Output.

### AgentTools candidate production and promotion

AgentTools source changes (`Tools/WorktreeCli/**`, `Tools/AgentHarness/**`, `Tools/ToolCommon/**`) never build into or through a canonical `Output` directory or link. They flow through two gated steps:

1. **Candidate production** — run the bundled sidecar from the checkout that holds the changes. It builds both solutions into the worktree's ignored `Temp\AgentToolsCandidate\` tree with overridden `OutDir`/`IntDir`, capability-checks the pair, verifies the canonical output identity was untouched, and writes a `broken-engine-agenttools-candidate/v1` receipt binding both executable SHA-256 hashes to the source commit and the three `Tools/` tree hashes:

   ```powershell
   & "$ROOT\.agents\skills\compile\scripts\New-AgentToolsCandidate.ps1" -WorktreeRoot $ROOT
   ```

   Exit `0` returns the receipt path/SHA-256 in one JSON result; `2` is a deterministic build or capability blocker. This doubles as the compile check for tool-source changes, and is safe in any registered checkout at any time. A candidate is valid for promotion iff its receipt has `dirtyToolPaths: false` and its three `toolTreeHashes` match the landed commit's tool trees; `sourceCommit` is informational. Tree hashes survive a rebase that leaves tool bytes unchanged, so a rebase alone does not require a rebuild — rebuild only when `dirtyToolPaths: true` or the landed tool trees differ from the receipt. When `BuildCommand.cpp`'s result contract changes, run [`scripts/Test-BuildResultFixtures.ps1`](scripts/Test-BuildResultFixtures.ps1) against the candidate `WorktreeCli.exe`.

2. **Promotion** — owned by `/finalize-changes` (`.agents/skills/finalize-changes/scripts/Invoke-AgentToolsPromotion.ps1`) and possible only during an approved session landing, after the landed commit's tool trees match the candidate receipt. Routine builds, this skill, and unlanded session trees cannot promote; do not copy or install either executable elsewhere.

If a canonical primary executable is missing or a legacy first rollout applies, wrapper bootstrap (`.agents/scripts/Bootstrap-AgentTools.ps1`) remains the only in-place build path.

## Determine what to build

- Default: BrokenEngineSandbox client Debug.
- If any changed file is shared (`Common/`, `Engine/`, or non-exclusive game code), build both client and server.
- If the session's approved plan or acceptance matrix includes an agent-harness scenario, build both client and server in the same request regardless of changed-file affinity — the harness launches both executables and hard-stops when either is missing. A delegator requesting the build states this trigger.
- ThirdParty builds only on explicit request. Missing source or library links are provisioning failures; never rebuild ThirdParty automatically.
- DataPacker builds Release only. WorktreeCli still supplies the normal worktree-local target serialization; DataPacker's `"BrokenEngineDataPacker"` mutex remains the only PC-global coordination exception.

Before any DataPacker, client, or server build, invoke `$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1 -RepositoryRoot $ROOT` and stop on failure. Validated stable primary submodule trees plus shared immutable ThirdParty, WorktreeCli, and AgentHarness Output directories are the only exceptions to worktree-local build artifacts.

For routine work, build the checkout supplied by the caller and use its current `HEAD` as the comparison baseline when one is needed. An isolated session worktree remains appropriate for queue operations, concurrent work, or a final-evidence gate, but is not a prerequisite for a targeted build. Keep existing build serialization and the gated AgentTools candidate/promotion path; do not share mutable build output between checkouts.

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
- **Deletion-only exception:** pure deletions of source asset files under `Engine/Data/**` or `Projects/BrokenEngineSandbox/Data/**` do not trigger Local when a repository-wide search proves nothing tracked references the deleted asset's generated identity (its generated CRC constant, chunk, or path). With `RunDataPacker=false` the build consumes only pre-existing generated output, which a source deletion cannot alter — the dead chunk persists in the published pack and the unused constant in the generated header until the next real DataPacker export drops both. Record the reference-search evidence with the mode selection. Any addition, modification, rename, exporter, or contract change keeps the Local requirement.

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

Before Shared compilation, require these ten nonempty headers: `Data.h`, `DataTypes.h`, `Audio.h`, `Font.h`, `Scene.h`, `Islands.h`, `Model.h`, `Shader.h`, `Texture.h`, `Raw.h`. Also require nonempty `.manifest` and `.pack` files for each of `Audio`, `Font`, `Scene`, `Islands`, `Model`, `Shader`, `Texture`, and `Raw`. The Shared directory must be absolute, outside `$ROOT`, and must remain unchanged: snapshot the required files as normalized relative path + length + last-write time before the first build, recheck after each client/server build, and return that snapshot for the harness's pre-launch check. Any missing, added, removed, or changed required file fails the workflow. Use full SHA-256 hashing in place of length + last-write time only when the run feeds replay/determinism verification.

Local mode may build the worktree Release DataPacker as a standalone compile check, but must never execute it. Before compiling either game target, require the same complete nonempty output set and explicit confirmation that the user prepared it after the current relevant worktree changes. If either check is absent, stop with `Local data is missing or stale; manual worktree DataPacker export required` and do not build, export, or fall back. Actual generation is a separate user-controlled action outside this skill.

Snapshot the primary required-file set before Local work and recheck it after each game build; any primary write fails the workflow. After the last game build, snapshot the complete selected Local output for the harness. Never copy worktree DataPacker/data source changes into `$PRIMARY` to test them.

## Explicit Microsoft PREfast verification mode

Use this mode only when an approved plan explicitly requires Microsoft PREfast verification. Retain every ordinary game-build protection above: the session-claim discipline (wrapper or transient), immutable prebuilt WorktreeCli, worktree provisioning and lifecycle validation, WorktreeCli target serialization, synchronous foreground execution, data-mode selection, canonical `@DataProperties`, complete-data checks, and selected/primary identity snapshots. Do not invoke MSBuild or `/analyze` outside WorktreeCli.

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
- Read every reported field from the captured `broken-engine-build-result/v1` JSON, never from scraped terminal text.
- Final status per project: `status` plus `exitCode` and `failureKind`.
- Every `severity: error` diagnostic's `raw` line verbatim, plus all `messages` entries; note `diagnosticsTruncated: true` and point at the retained log for the remainder.
- `severity: warning` diagnostics' `raw` lines verbatim only for files involved in the change.
- The exact `retainedLog.path` for each build, and `complete: false` as a failure.
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
