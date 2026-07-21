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

AgentTools promotion-triggering changes — any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/` — never build into or through a canonical `Output` directory or link. They flow through two gated steps:

1. **Candidate production** — run the bundled sidecar from the checkout that holds the changes. It takes a zero-wait exclusive file lock at that checkout's ignored `Temp\AgentToolsCandidate\.producer.lock`; another producer in the same checkout returns `candidate.concurrent-producer`, while different checkouts remain independent and never use the PC-global session ledger. It then snapshots every nonignored ordinary file under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, and `Tools/ToolCommon/`, plus `ThirdParty/tinygltf/json.hpp`, builds both solutions into run-specific paths below `Temp\AgentToolsCandidate\`, parses the compiler dependency tlogs, capability-checks the pair, and re-snapshots the exact source set. Every repository-local compiler input must be present in the pre-build manifest, and membership, bytes, SHA-256, or clean-filter Git blob identity must remain unchanged:

   ```powershell
   & "$ROOT\.agents\skills\compile\scripts\New-AgentToolsCandidate.ps1" -WorktreeRoot $ROOT
   ```

   The producer emits one `broken-engine-agenttools-candidate-result/v1` JSON object. Exit `0` returns a `broken-engine-agenttools-candidate/v2` receipt path/SHA-256; the receipt records the before/after manifest and digest, dependency logs and repository-local compiler inputs, checkout/commit identity, MSBuild identity, executable hashes, capability-check identity, and before/after canonical executable identity. Exit `2` is a deterministic negative result: concurrent producer, source change (`candidate.source-changed`), missing/unbound compiler evidence, build/output/capability failure, or disturbed canonical output. Exit `1` is malformed input, Git/MSBuild discovery, OS, or internal failure. This doubles as the compile check for tool-source changes. It neither waits for other checkouts nor certifies a commit for promotion.

   Finalization owns commit certification: after landing, it accepts v2 only, proves the receipt's source manifest equals the landed clean-filter blobs, rechecks candidate executable hashes, and then promotes. A rebase that preserves all manifest identities does not require a rebuild; any landed source membership or blob mismatch does. When `BuildCommand.cpp`'s result contract changes, run [`scripts/Test-BuildResultFixtures.ps1`](scripts/Test-BuildResultFixtures.ps1) against the candidate `WorktreeCli.exe`.

   Candidate production may finish while other wrapper sessions remain active.
   Its handoff must flag AgentTools promotion as a [canonical shared-artifact
   mutation](../next-plan/references/execution-gates.md#canonical-shared-artifacts);
   `/finalize-changes` waits for every other active wrapper session to end,
   rechecks the canonical session ledger, and presents landing confirmation only
   after that recheck passes.

2. **Promotion** — owned by `/finalize-changes` (`.agents/skills/finalize-changes/scripts/Invoke-AgentToolsPromotion.ps1`) and possible only during an approved session landing, after v2 commit certification succeeds. Routine builds, this skill, and unlanded session trees cannot promote; do not copy or install either executable elsewhere.

If a canonical primary executable is missing or a legacy first rollout applies, wrapper bootstrap (`.agents/scripts/Bootstrap-AgentTools.ps1`) remains the only in-place build path.

## Determine what to build

Resolve identities once before selecting data mode or changed paths. An explicitly supplied fixed baseline from the caller or approved execution card is authoritative and must not be replaced with a later `HEAD`. Otherwise use `BROKEN_ENGINE_BASELINE` when present, then the selected checkout's current `HEAD`. Resolve `$ROOT` from `BROKEN_ENGINE_WORKTREE_PATH` when present, otherwise from `git rev-parse --show-toplevel` in the caller-supplied/current checkout. Resolve `$PRIMARY` from `BROKEN_ENGINE_PRIMARY_CHECKOUT` when present; otherwise use the parent of the absolute Git common directory (`git -C $ROOT rev-parse --path-format=absolute --git-common-dir`). Canonicalize and validate each path, require `$ROOT` to equal its Git top level, require `$PRIMARY\.git` to be an ordinary directory, and require `$BASELINE` to resolve to a commit. Environment values are wrapper-provided identity hints, not permission to move a supplied baseline.

- Default: BrokenEngineSandbox client Debug.
- If any changed file is shared (`Common/`, `Engine/`, or non-exclusive game code), build both client and server.
- If the session's approved plan or acceptance matrix includes an agent-harness scenario, build both client and server in the same request regardless of changed-file affinity — the harness launches both executables and hard-stops when either is missing. A delegator requesting the build states this trigger.
- ThirdParty builds only on explicit request. Missing source or library links are provisioning failures; never rebuild ThirdParty automatically.
- DataPacker builds Release only. WorktreeCli still supplies the normal worktree-local target serialization; DataPacker's `"BrokenEngineDataPacker"` mutex remains the only PC-global coordination exception.

Before any DataPacker, client, or server build, invoke `$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1 -RepositoryRoot $ROOT` and stop on failure. Validated stable primary submodule trees plus shared immutable ThirdParty, WorktreeCli, and AgentHarness Output directories are the only exceptions to worktree-local build artifacts.

For routine work, build the checkout supplied by the caller. An isolated session worktree remains appropriate for queue operations, concurrent work, or a final-evidence gate, but is not a prerequisite for a targeted build. Keep existing build serialization and the gated AgentTools candidate/promotion path; do not share mutable build output between checkouts.

For a delegated call, return the complete build result inline. A build does not
create an evidence artifact; a later final-evidence gate records its decisive
exit status and relevant diagnostics once.

DataPacker's mutex coordinates across worktrees and its shared chunks live under `%TEMP%\DataPacker\<Project>`; do not add another PC-global DataPacker lock or a checkout-local cache copy. Gaea raw and split intermediates use the single mutable `%TEMP%\DataPacker\<Project>\Gaea\Islands` cache; source-tree island leaves retain only tracked BC outputs.

### Execution and result discipline

Run each `WorktreeCli build` synchronously in the foreground and remain in-turn until its process exit code and single JSON result are captured. Give the call the maximum available execution timeout so the 660-second target-lock wait is not preempted. Never use `Start-Job`, a trailing `&`, a fire-and-forget watcher, or end a delegated turn while a build is running. If the host call times out while the build continues, re-invoke the identical command; target serialization and incremental tlogs carry it to completion. When a blocking call is unavailable, poll the same invocation to completion in-turn.

Parse and report only the structured result described above after every requested target has returned. Ordinary builds do not run DataPacker, Gaea, or texture export; the authorized Local-generation path is the sole exception. Keep `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; explicit PREfast verification is the sole exception to `RunCodeAnalysis=false`. VS2026 clang-tidy crashes on this codebase.

## Select runtime data mode

Every worktree game build uses one data mode and one canonical directory for both client and server:

- **Shared** is the default for ordinary code changes. Set `$GameDataDirectory` to `$PRIMARY\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`; this mode consumes primary generated headers/packs and disables every DataPacker build/export step.
- **Local** is mandatory when tracked changes from `$BASELINE`, staged changes, unstaged changes, or untracked files touch `DataPacker/**`, `Engine/Data/**`, `Projects/BrokenEngineSandbox/Data/**`, `Common/DataFile.h`, generated-header logic, exporter versions/fingerprints, compression, chunk layout, or pack/manifest contracts. Set `$GameDataDirectory` to `$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`.
- A user may force Local. Never force Shared over a Local trigger. Local never falls back to Shared data. Generate Local output only with explicit authorization; a user-approved plan or acceptance criterion requiring shader or data repack is authorization.
- **An absent worktree `Data` output never blocks Local.** A linked worktree that has never generated has no `Output\Data` directory at all; that is the legitimate expected starting state, not missing setup. On an authorized Local generation build DataPacker seeds it from the primary checkout itself — the Local generation section below states the exact mechanism and its conditions. Never stop, ask the user to pre-stage output, or hand-run an export because the directory is absent. The only thing an agent must supply is generation *authorization*; the genuine blockers are validation and environment failures DataPacker reports itself — an unrecognized reparse point, absent primary output, or insufficient disk — never a user-prepared directory.
- **Deletion-only exception:** pure deletions of source asset files under `Engine/Data/**` or `Projects/BrokenEngineSandbox/Data/**` do not trigger Local when a repository-wide search proves nothing tracked references the deleted asset's generated identity (its generated CRC constant, chunk, or path). With `RunDataPacker=false` the build consumes only pre-existing generated output, which a source deletion cannot alter — the dead chunk persists in the published pack and the unused constant in the generated header until the next real DataPacker export drops both. Record the reference-search evidence with the mode selection. Any addition, modification, rename, exporter, or contract change keeps the Local requirement.

The game projects detect the canonical repository-root Git marker. A linked worktree has a `.git` file and defaults `RunDataPacker=false`; the primary checkout has a `.git` directory and preserves ordinary Local Visual Studio behavior by defaulting true. An explicit Local `RunDataPacker=true` permits deliberate worktree generation; Shared rejects true.

Build the changed-path set from `git -C $ROOT diff --name-only $BASELINE --` plus `git -C $ROOT ls-files --others --exclude-standard`; normalize separators before matching. This single baseline diff includes committed, staged, and unstaged tracked changes. If a changed file may affect generated or serialized bytes and the path rules do not prove otherwise, select Local.

Set `$GeneratedDataIncludeRoot` to the canonical parent of `$GameDataDirectory` (consumers include `Data/...`). Set `$RunDataPacker` to `'false'` for Shared and ordinary Local builds, or to `'true'` only for the first game build in an authorized Local generation. Pass these exact properties to every client/server `.sln` or `.vcxproj` build, including `--files`:

```powershell
$DataProperties = @(
	"/p:DataBuildMode=$DataBuildMode",
	"/p:RunDataPacker=$RunDataPacker",
	"/p:GameDataDirectory=$GameDataDirectory",
	"/p:GeneratedDataIncludeRoot=$GeneratedDataIncludeRoot"
)
```

When `$RunDataPacker -eq 'true'`, the game project's nested DataPacker build also requires `DevEnvDir`. Resolve the exact VS2026 Community install first, then fall back to `vswhere`; require `Common7\IDE\devenv.com` or `devenv.exe`, preserve the directory's trailing separator, and append the property only for that generation build:

```powershell
$VsInstall = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$DevEnvDirectory = Join-Path $VsInstall 'Common7\IDE'
if (-not (Test-Path -LiteralPath (Join-Path $DevEnvDirectory 'devenv.com') -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $DevEnvDirectory 'devenv.exe') -PathType Leaf))
{
	$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf)) { throw 'Unable to locate vswhere for Visual Studio 2026.' }
	$VsInstall = (& $VsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
	if (-not $VsInstall) { throw 'Unable to locate Visual Studio 2026.' }
	$DevEnvDirectory = Join-Path $VsInstall 'Common7\IDE'
}
if (-not (Test-Path -LiteralPath (Join-Path $DevEnvDirectory 'devenv.com') -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $DevEnvDirectory 'devenv.exe') -PathType Leaf)) { throw "Visual Studio IDE executable missing under '$DevEnvDirectory'." }
$DevEnvDirectory = $DevEnvDirectory.TrimEnd('\') + '\'
$GenerationDataProperties = @($DataProperties) + "/p:DevEnvDir=$DevEnvDirectory"
```

Use `$GenerationDataProperties` only for the first `RunDataPacker=true` build. For every later build, set `$RunDataPacker = 'false'`, reconstruct `$DataProperties` from the four-property block above, and pass that array; `DevEnvDir` must not leak into subsequent calls.

Before Shared compilation, require these ten nonempty headers: `Data.h`, `DataTypes.h`, `Audio.h`, `Font.h`, `Scene.h`, `Islands.h`, `Model.h`, `Shader.h`, `Texture.h`, `Raw.h`. Also require nonempty `.manifest` and `.pack` files for each of `Audio`, `Font`, `Scene`, `Islands`, `Model`, `Shader`, `Texture`, and `Raw`. The Shared directory must be absolute, outside `$ROOT`, and must remain unchanged: snapshot the required files as normalized relative path + length + last-write time before the first build, recheck after each client/server build, and return that snapshot for the harness's pre-launch check. Any missing, added, removed, or changed required file fails the workflow. Use full SHA-256 hashing in place of length + last-write time only when the run feeds replay/determinism verification.

Local mode may build the worktree Release DataPacker as a standalone compile check. Before Local work, snapshot the primary required-file set. For an authorized Local generation, set `$RunDataPacker = 'true'` on the first game build so the project builds and runs the worktree DataPacker. In a validated linked worktree, DataPacker seeds an absent worktree `Data` or `Attribution` output from the corresponding primary ordinary directory when it exists, using a recognized link; dirty output uses copy-on-write materialization, staging primary files and atomically replacing only the affected recognized link.

Agent-driven Local generation forbids Gaea by default. Scope `BT_DATAPACKER_FORBID_GAEA_EXPORT=1` around the entire first `RunDataPacker=true` WorktreeCli call and restore the caller's prior environment exactly in `finally`. Only an explicit user-approved plan or acceptance criterion that requires regenerating Gaea output authorizes omitting/clearing this guard; changed island inputs, DataPacker code, fingerprints, or cache state alone never authorize a Gaea run. If the guard blocks a dirty Gaea route, the generation build failed: report the exception and do not clear the guard or retry without that explicit approval. `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT` retains its broader independent meaning; never clear either caller-provided guard.

```powershell
$HadGaeaGuard = Test-Path Env:BT_DATAPACKER_FORBID_GAEA_EXPORT
$PreviousGaeaGuard = $env:BT_DATAPACKER_FORBID_GAEA_EXPORT
try
{
	$env:BT_DATAPACKER_FORBID_GAEA_EXPORT = '1'
	# Invoke the first foreground WorktreeCli game build with RunDataPacker=true and
	# $GenerationDataProperties here; require success.
}
finally
{
	if ($HadGaeaGuard) { $env:BT_DATAPACKER_FORBID_GAEA_EXPORT = $PreviousGaeaGuard }
	else { Remove-Item Env:BT_DATAPACKER_FORBID_GAEA_EXPORT -ErrorAction SilentlyContinue }
}
```

After that build, require the complete nonempty Local output set and recheck that primary output is unchanged. Set `$RunDataPacker = 'false'` for the other target and every subsequent build.

Without generation authorization, require the same complete nonempty Local output set and explicit confirmation that it was prepared after the current relevant worktree changes. If either check is absent, stop with `Local data is missing or stale; Local generation authorization required` and do not build, export, or fall back. The remedy is authorization for the generation build above, which seeds absent worktree output from primary on its own — never a hand-run DataPacker export or user-prepared output directory.

Recheck the primary required-file snapshot after each Local game build; any primary write fails the workflow. After generation and after the last game build, snapshot the complete selected Local output for the harness. Never copy worktree DataPacker/data source changes into `$PRIMARY` to test them.

## Explicit Microsoft PREfast verification mode

Use this mode only when an approved plan explicitly requires Microsoft PREfast verification. Retain every ordinary game-build protection above: the session-claim discipline (wrapper or transient), immutable prebuilt WorktreeCli, worktree provisioning and lifecycle validation, WorktreeCli target serialization, synchronous foreground execution, data-mode selection, canonical `@DataProperties`, complete-data checks, and selected/primary identity snapshots. Do not invoke MSBuild or `/analyze` outside WorktreeCli.

Release analysis has two paths, selected by `RunCodeAnalysis`, and both fail the build on a diagnostic:

- **Ordinary Release agent builds already enforce.** With `/p:RunCodeAnalysis=false`, `EnablePREfast=true` in each Release `ClCompile` block still applies — it sits outside the toolchain's `RunMsvcAnalysis` gate — so cl runs `/analyze` writing to the console under `/WX`, against the compiler's default rules and no rule set. Never remove `EnablePREfast` from a Release configuration to quiet a warning; that silently retires this gate.
- **This mode adds the rule set.** `RunCodeAnalysis=true` applies `/analyze:quiet`, routes per-TU results through `*.nativecodeanalysis.xml`, and re-emits them after link via the `NativeCodeAnalysis` MSBuild task. It is the only path that applies `BrokenEngineAnalysis.ruleset`, so it is the only path that reports the C26xxx Core Guidelines codes. Note both Release configurations set `RunCodeAnalysis=true` themselves, so Visual Studio Release builds take this path too — it is not exclusive to this mode, and a change to the rule set or its allow list changes IDE Release builds as well.

Because those diagnostics never reach cl's console, `TreatWarningAsError` cannot see them; `CodeAnalysisTreatWarningsAsErrors=true` in both Release configurations is what makes the task fail the build. Report **"analysis executed"** and **"policy passed"** as separate facts — a zero exit alone establishes only the second. `RunNativeCodeAnalysis` is an incremental target, so a green incremental run can mean "skipped as up-to-date"; require a rebuild, or positive evidence that this run regenerated the merged `.nativecodeanalysis.xml`, before reporting that analysis ran.

Force Clang-Tidy off and Microsoft code analysis on only for the Release target commands below. Do not override the projects' warnings-as-errors settings, the rule set, or `CodeAnalysisTreatWarningsAsErrors`, and never pass `CodeAnalysisNeverReportRuleErrors` — it disables error promotion silently:

```powershell
# BrokenEngineSandbox client Release PREfast, then server Release PREfast.
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
```

`EnableMicrosoftCodeAnalysis=true` is belt-and-braces here: the toolchain forces analysis off only on an explicit `false`, and the Release configurations set it nowhere, so passing it changes nothing today. Keep it so an upstream default change cannot silently disable the mode.

A policy failure surfaces as a **nonzero MSBuild exit after the link step, with the executable already produced** — analysis runs through `AfterBuildLinkTargets`. Existing binaries after a failed run are expected, not a partial success. A failing run also does not write `*.lastcodeanalysissucceeded`, so the failure correctly re-reports on the next build until it is fixed. Report the matched diagnostics from the structured `diagnostics` array, noting `diagnosticsTruncated: true` and pointing at the retained log when the cap elides the rest.

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

Only `.cpp` inputs already present in the target project are valid. After a header edit, list the affected project-member `.cpp` files. Add new files to the vcxproj/filters before compiling. Shader changes require Local output generated through the authorized first-build path above or prepared explicitly before the client build.

## Report results

- For a delegated call, return the complete results inline after applying the execution/result discipline above. Keep overall/per-project status, data mode/path, and decisive blockers visible.
- Read every reported field from the captured `broken-engine-build-result/v1` JSON, never from scraped terminal text.
- Final status per project: `status` plus `exitCode` and `failureKind`.
- Every `severity: error` diagnostic's `raw` line verbatim, plus all `messages` entries; note `diagnosticsTruncated: true` and point at the retained log for the remainder.
- `severity: warning` diagnostics' `raw` lines verbatim only for files involved in the change.
- The exact `retainedLog.path` for each build, and `complete: false` as a failure.
- LNK1168 or EXE LNK2019 can mean a client/server process still holds the executable; report it rather than diagnosing unless asked.
- A prior killed build's `unsuccessfulbuild` marker clears on the next successful run; rerun instead of deleting tlogs.
- A lock timeout means another WorktreeCli build still owns that target. Retry after it finishes; never delete `.claude/build-locks/` manually.
- For game builds, report `DataBuildMode`, the `RunDataPacker` value for every build, canonical `GameDataDirectory`, canonical `GeneratedDataIncludeRoot`, the selected-data identity snapshot, and the primary identity snapshot when Local. Report every mode-selection trigger, the Local prepared-data confirmation or generation-authorization trigger, whether the Gaea guard was applied (or the exact explicit Gaea-regeneration authorization), and the post-generation selected/primary snapshots. A harness run must use this exact mode/path; it must not infer or substitute one.

End with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed or skipped required build, or none>
```
