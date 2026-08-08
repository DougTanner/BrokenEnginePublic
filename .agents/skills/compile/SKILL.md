---
name: compile
description: Builds Broken Engine projects through WorktreeCli's serialized MSBuild driver and governs immutable prebuilt AgentTools bootstrap/maintenance policy. Use whenever you need to build, rebuild, compile, or check for compile/link errors in ThirdParty, DataPacker, AgentHarness, WorktreeCli, or BrokenEngineSandbox (client or server).
allowed-tools: [PowerShell]
---

# Build

Run solely inside one delegated `builder`; separate-role requirements return to
the manager.

Builds through the current checkout's WorktreeCli executable. `WorktreeCli build` serializes writers per target basename inside the current worktree, waits up to 660 seconds, preserves native Windows argument boundaries, and owns MSBuild through a kill-on-close Job Object.

## Structured build result

`WorktreeCli build` writes exactly one schema-versioned `broken-engine-build-result/v1` JSON object to stdout; human progress goes to stderr. The JSON — not scraped terminal text — is the authoritative result. Capture stdout, parse it, and read:

- `status` (`success`/`fail`), `failureKind` (`none`/`tool`/`msbuild`), and `exitCode` — the process exit code keeps its existing meaning (MSBuild's exit code once launched; `1` for tool failures including a retained-log failure after a successful build).
- `target`/`worktreeRoot` normalized identities, `arguments`, `selectedFiles`, `invalidatedObjects`.
- `lock` outcome (`acquired`/`timeout`/`failed`) with the lock path and waited seconds.
- `msbuild` discovery/launch state and MSBuild's own exit code.
- `retainedLog` — the complete combined MSBuild stdout+stderr stream in observed read order, untruncated, below the invoking worktree's ignored `Temp/AgentBuildLogs/`. `complete: false` or a missing log is a build-result failure, never an omitted side effect.
- `diagnostics` — structured MSBuild error/warning entries (`severity`, `code`, `file`, `line`, `column`, `project`, `message`, `raw`), capped with `diagnosticsTruncated: true` when the raw log holds more; `messages` carries tool failures and unmatched fatal lines.
- `elapsedMilliseconds` and `startedAt`.

## AgentTools trigger

The authoritative executables are `$ROOT\Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` and `$ROOT\Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe`. Routine work consumes these immutable primary Outputs and never builds into or writes through them:

```powershell
& "$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot $ROOT
if ($LASTEXITCODE -ne 0) { throw "Worktree provisioning failed: $LASTEXITCODE" }
$WorktreeCli = Join-Path $ROOT 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
$AgentHarness = Join-Path $ROOT 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
if (-not (Test-Path -LiteralPath $WorktreeCli -PathType Leaf) -or -not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) { throw 'AgentTools output is incomplete.' }
```

If either executable is missing, stop: wrappers own bootstrap. When the changed
set contains a non-Markdown path under `Tools/WorktreeCli/`,
`Tools/AgentHarness/`, or `Tools/ToolCommon/`, the rebuilt tools are promoted
through `/finalize-changes`' AgentTools promotion — waiting until there is no
activity, plus backup/rollback under the shared mutex — which also owns bootstrap policy. This
compile run only builds; it never promotes or copies tools. An agent changing
shared tool infrastructure pauses and warns the user only if the change could
cause problems for other live worktrees.

## Determine what to build

Resolve identities, the changed-path set, and the data-mode directories once, before selecting data mode or targets, by running the repository-owned read-only script. In Codex's PowerShell 7 terminal, from the caller-supplied/current checkout:

```powershell
$Script = Join-Path (git rev-parse --show-toplevel).Trim() '.agents/skills/compile/scripts/Resolve-CompileContext.ps1'
# Append -RepositoryRoot/-PrimaryCheckout/-Baseline only when the caller explicitly supplied that
# input, and -IncludeDevEnvDir only for an authorized Local generation build. An empty or unset
# value counts as not supplied: omit the parameter so the env-hint/derived-default fallback applies.
$ScriptArguments = @()
if ($SuppliedBaseline) { $ScriptArguments += @('-Baseline', $SuppliedBaseline) }
$Context = pwsh -NoProfile -ExecutionPolicy Bypass -File $Script @ScriptArguments | ConvertFrom-Json
```

In Claude Code's Git Bash terminal, convert the script path first:

```bash
script="$(cygpath -w "$(git rev-parse --show-toplevel)/.agents/skills/compile/scripts/Resolve-CompileContext.ps1")"
# Same rule: append -Baseline "$supplied_baseline" (and -RepositoryRoot/-PrimaryCheckout) only when
# that input was explicitly supplied.
pwsh -NoProfile -ExecutionPolicy Bypass -File "$script"
```

The script writes exactly one `broken-engine-compile-context/v1` JSON object to stdout; human diagnostics go to stderr. Each input takes an explicitly supplied parameter first, then the wrapper environment hint (`BROKEN_ENGINE_WORKTREE_PATH`, `BROKEN_ENGINE_PRIMARY_CHECKOUT`, `BROKEN_ENGINE_BASELINE`), then the derived default. Environment values are wrapper-provided identity hints, not permission to move a supplied baseline. Read from the result:

- `status`/`code`/`message` with exit `0` pass, `2` structured blocked, `1` internal error. Any nonzero exit stops the build: report the exact `code` and `message`.
- `repositoryRoot` (`$ROOT`), `primaryCheckout` (`$PRIMARY`), and `baseline` (`$BASELINE`, the resolved commit) — a supplied session baseline is authoritative and is never advanced to a later `HEAD`.
- `changedPaths`/`changedPathCount` — the complete changed-path set: the single baseline diff (committed, staged, and unstaged tracked changes) plus untracked files, separator-normalized. `changedPathsTruncated`, `triggerMatchesTruncated`, `deletionOnlyCandidatesTruncated`, and code `output.capacity-exceeded` are blocking conditions, never a partial answer to work from.
- `triggerMatches` — each changed path with the Local path trigger it matched.
- `deletionOnlyCandidates` — trigger-matching changed paths whose baseline diff status is a deletion, with rename sides excluded. Evidence for the deletion-only exception in [references/runtime-data-mode.md](references/runtime-data-mode.md), never its decision.
- `dataBuildMode` with `dataBuildModeDerivation` `path-rules-only`, plus `gameDataDirectory` and `generatedDataIncludeRoot`.
- `devEnvDir` — present only when `-IncludeDevEnvDir` was passed.

Never reconstruct these operations inline: no hand-typed changed-path `git diff`/`git ls-files` commands, no hand-resolved `$ROOT`/`$PRIMARY`/`$BASELINE`, and no inline `vswhere`/`DevEnvDir` discovery. A blocked result is a stop, not a cue to redo its work by hand.

- Default: BrokenEngineSandbox client Debug.
- If any changed file is shared (`Common/`, `Engine/`, or non-exclusive game code), build both client and server.
- If the session's approved plan or acceptance table includes an agent-harness scenario, build both client and server in the same request regardless of changed-file affinity — the harness launches both executables and hard-stops when either is missing. A delegator requesting the build states this trigger.
- ThirdParty builds only on explicit request in agent-driven `/compile`; missing source or library links are provisioning failures and a routine `/compile` never rebuilds ThirdParty itself. Wrapper bootstrap does incremental-rebuild ThirdParty at every session start (see Bootstrap AgentTools), so a routine `/compile` normally finds it already current.
- DataPacker builds Release only. WorktreeCli still supplies the normal worktree-local target serialization. Worktree session start seeds the worktree's DataPacker Release Output by verified copy from the primary bootstrap prebuild when the worktree's clean `DataPacker/`/`Common/`/`ThirdParty/` trees match the stamp, and otherwise builds it locally (`.agents/scripts/Build-WorktreeDataPacker.ps1`), so `DataPacker.exe` is already present at session start; a session that changes DataPacker-relevant sources performs a full local rebuild on its first DataPacker build. DataPacker's `"BrokenEngineDataPacker"` mutex and the AgentTools bootstrap mutex are the two PC-global build coordination points; add no others (the seed copy reuses the bootstrap mutex for its prebuild).

Before any DataPacker, client, or server build, invoke `$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1 -RepositoryRoot $ROOT` and stop on failure. Validated stable primary submodule trees plus shared immutable ThirdParty, WorktreeCli, and AgentHarness Output directories are the only exceptions to worktree-local build artifacts. When this session holds the harness lock, send `quit` with its own owner token and wait for its own retained exact PIDs before building — a live executable locks its image. When a target executable is live under a process this session cannot prove it owns, stop and report contention; never quit or stop it (`/agent-harness`, Ownership and takeover). Do not discover this as a link error.

For routine work, build the checkout supplied by the caller. An isolated session worktree remains appropriate for queue operations, concurrent work, or a landing gate, but is not a prerequisite for a targeted build. Keep existing build serialization and the gated AgentTools promotion path; do not share mutable build output between checkouts (the session-start DataPacker seed is a one-time verified copy the worktree then owns and may rebuild over, not shared output).

For a delegated call, return the complete build result inline as Report results
below requires. A `builder` executing this skill runs the build itself and never
dispatches another agent.

DataPacker's mutex coordinates across worktrees and its shared chunks live under `%LOCALAPPDATA%\BrokenEngine\DataPackerCache\<Project>`; do not add another PC-global DataPacker lock or a checkout-local cache copy (the session-start seed copies the built `DataPacker.exe` once into the worktree's own Output — a verified artifact seed, not a shared chunk cache). Gaea raw and split intermediates use the single mutable `%LOCALAPPDATA%\BrokenEngine\DataPackerCache\<Project>\Gaea\Islands` cache; source-tree island leaves retain only tracked BC outputs.

### Execution and result discipline

Run each `WorktreeCli build` synchronously in the foreground and remain in-turn until its process exit code and single JSON result are captured. In Claude Code's Git Bash, invoke it through `pwsh`: MSYS argument conversion rewrites the `/p:` switches into paths, and MSBuild then fails with MSB1008 ("Only one project can be specified"). Give the call the maximum available execution timeout so the 660-second target-lock wait is not preempted. Never use `Start-Job`, a trailing `&`, a fire-and-forget watcher, or end a delegated turn while a build is running. If the host call times out while the build continues, re-invoke the identical command; target serialization and incremental tlogs carry it to completion. When a blocking call is unavailable, poll the same invocation to completion in-turn.

Parse and report only the structured result described above after every requested target has returned. Ordinary builds do not run DataPacker, Gaea, or texture export; the authorized Local-generation path is the sole exception. Keep `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; explicit PREfast verification is the sole exception to `RunCodeAnalysis=false`. VS2026 clang-tidy crashes on this codebase.

## Select runtime data mode

Every build targeting a BrokenEngineSandbox project — client or server, full or selective, whichever source files a selective compile lists — must read [references/runtime-data-mode.md](references/runtime-data-mode.md) and select the runtime data mode before building; that reference is mandatory reading in that case, not optional background, and it defines the `$DataBuildMode` and `@DataProperties` values the commands below pass and the oracle identities the report requires. Only builds targeting a non-game project (ThirdParty, DataPacker, WorktreeCli, AgentHarness) read neither reference.

## Explicit Microsoft PREfast verification mode

PREfast verification runs only when an approved plan explicitly requires it, and in that case reading [references/prefast-mode.md](references/prefast-mode.md) before building is mandatory, not optional background; otherwise that reference stays unloaded, and never infer PREfast authorization from a routine compile, rebuild, or link-error check.

## Full-build commands

The AgentTools candidate commands use a session-local `OutDir` because the
default Output is the shared immutable primary Output held by the running
WorktreeCli driver; a default-output WorktreeCli build therefore fails with a
structural LNK1104. Run these candidate commands from PowerShell 7; the
existing MSYS `/p:` warning is in [Execution and result discipline](#execution-and-result-discipline).

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

# AgentTools: Release candidates under the session-local ignored Temp tree.
$AgentToolsCandidate = Join-Path $ROOT 'Temp\AgentToolsCandidate\'
& $WorktreeCli build "$ROOT\Tools\WorktreeCli\Platforms\VisualStudio2026\WorktreeCli.sln" '/p:Configuration=Release' '/p:Platform=x64' "/p:OutDir=$AgentToolsCandidate" '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\Tools\AgentHarness\Platforms\VisualStudio2026\AgentHarness.sln" '/p:Configuration=Release' '/p:Platform=x64' "/p:OutDir=$AgentToolsCandidate" '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

```

These commands produce `$ROOT\Temp\AgentToolsCandidate\WorktreeCli.exe` and
`$ROOT\Temp\AgentToolsCandidate\AgentHarness.exe` for AgentTools promotion.

## Selective file compile

Use `--files` to invalidate specific project-member `.cpp` objects before a normal project build. Target a `.vcxproj`, supply both Configuration and Platform, and separate the source list from the target with `--`:

```powershell
& $WorktreeCli build --files "$ROOT\Engine\Source\Example.cpp" "$ROOT\Projects\BrokenEngineSandbox\Source\Example.cpp" -- `
	"$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj" `
	'/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
```

Only `.cpp` inputs already present in the target project are valid. After a header edit, list the affected project-member `.cpp` files. Add new files to the vcxproj/filters before compiling. Shader changes require Local output generated through the authorized first-build path in [references/runtime-data-mode.md](references/runtime-data-mode.md) or prepared explicitly before the client build.

## Report results

- For a delegated call, return the complete results inline after applying the execution/result discipline above. Keep overall/per-project status, data mode/path, and decisive blockers visible.
- Include every build's captured `broken-engine-build-result/v1` envelope verbatim in the handoff, and read every reported field from it, never from scraped terminal text.
- Final status per project: `status` plus `exitCode` and `failureKind`.
- Every `severity: error` diagnostic's `raw` line verbatim, plus all `messages` entries; note `diagnosticsTruncated: true` and point at the retained log for the remainder.
- `severity: warning` diagnostics' `raw` lines verbatim only for files involved in the change.
- The exact `retainedLog.path` for each build, and `complete: false` as a failure.
- A WorktreeCli default-output `LNK1104` is structural because the running driver holds the shared primary Output; use the candidate commands in [Full-build commands](#full-build-commands). Other `LNK1104`, `LNK1168`, or EXE `LNK2019` failures can mean a live target process still holds the executable; report them rather than diagnosing unless asked.
- A prior killed build's `unsuccessfulbuild` marker clears on the next successful run; rerun instead of deleting tlogs.
- A lock timeout means another WorktreeCli build still owns that target. Retry after it finishes; never delete `.claude/build-locks/` manually.
- For game builds, report `DataBuildMode`, the `RunDataPacker` value for every build, normalized `GameDataDirectory`, normalized `GeneratedDataIncludeRoot`, and each selected oracle's exact receipt path, SHA-256, Data path, mode, baseline, and aggregate digest. In Local mode also report the independent oracle for the primary Shared data. Report every mode-selection trigger, the Local prepared-data or generation-authorization trigger, authorized content-delta outcome, whether the Gaea guard was applied (or the exact explicit Gaea-regeneration authorization), and all post-build oracle verification results. A harness run must consume these exact identities; it must not infer, substitute, or compare Shared and Local for equality.

End with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed or skipped required build, or none>
```
