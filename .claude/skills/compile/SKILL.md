---
name: compile
description: Builds Broken Engine projects via the MSBuild lock wrapper — exact commands for full and selective (--files) builds. Use whenever you need to build, rebuild, compile, or check for compile/link errors in ThirdParty, DataPacker, or BrokenEngineSandbox (client or server).
allowed-tools: [Bash]
---

# Build

Builds Broken Engine projects via `.claude/msbuild.sh`, a lock wrapper that serializes MSBuild access per-solution to prevent concurrent builds from corrupting tlog files. The wrapper handles locking, stale lock cleanup, path normalization, and `MSYS_NO_PATHCONV=1` automatically.

## Instructions

### 1. Determine What to Build

If the request doesn't specify a project, build **BrokenEngineSandbox client** (Debug).

If given a changed-file list and any file is shared code (under `Common/`, `Engine/`, or game code not client/server-exclusive), build BOTH client and server.

All paths below use `$ROOT` for the absolute repo root (the cwd from the environment block, or `pwd`).

### Worktree Isolation

This skill does not create or remove worktrees. The caller follows root `AGENTS.md` and verifies `$ROOT` is the adopted or session-owned worktree recorded from the primary checkout's branch and HEAD baseline; never build in the primary checkout. Cleanup remains the caller's responsibility after the root lifecycle's landed, clean, and identity checks pass.

Each top-level session builds in its own worktree. `Build/`, `Output/`, and `.claude/build-locks/` remain local to that checkout; do not share or seed mutable C++ build outputs between worktrees. Accept the first cold C++/PCH build in a new worktree.

DataPacker already coordinates across worktrees: it serializes through the PC-global `"BrokenEngineDataPacker"` mutex and reuses `%TEMP%\DataPacker\<Project>`. Do not add another DataPacker lock or checkout-local cache copy. Gaea `Engine/Data/Islands/**/Intermediates/` remain worktree-local; their rare regeneration is an accepted cold-worktree cost.

### 2. Build the Project

**IMPORTANT: Build duration & foreground default** — Typical builds (incremental, selective, even ThirdParty rebuilds) finish in seconds to a few minutes. **Run builds in the FOREGROUND with the default 2-minute timeout** — blocking is reliable in every context (subagents especially: turn-ends and background notifications are flaky). The ONE exception that far exceeds it (upwards of an hour) is a build that triggers the full DataPacker data re-export — re-exporting all islands (slow Gaea export) and all textures (slow texture compression); use `run_in_background: true` only when that re-export is expected (data/texture-source changes, or a wiped/invalidated pack). If a foreground build is killed at the cap it leaves an `unsuccessfulbuild` marker in the tlog directory (forces a full rebuild next time) — recover by re-running that build in background, not foreground.

**When a background build runs inside a subagent** — a subagent's turn-end returns its last text to the caller as a final result, so never end a turn with an interim "build started/waiting" status line; that reads as your report. End the turn with nothing but `AWAITING BUILD`; when a completion notification re-invokes you, continue in that same turn (check results, start the next sequential build, or emit the full final report). **Caller side**: a compile-subagent result that says `AWAITING BUILD` (or lacks per-project status) means the build is still in flight — the completion re-invocation is unreliable, so verify build state directly (msbuild processes, obj/exe timestamps) and resume the agent with a "continue and report" message rather than re-dispatching or treating it as done.

**IMPORTANT: clang-tidy disable** — every build command below passes `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`. The bundled VS2026 `clang-tidy.exe` crashes reproducibly with `0xC0000005` (access violation) on this codebase, blocking the actual C++ compile. Do not strip these flags.

**ThirdParty** (only rebuild if a link error references a missing ThirdParty `.lib` or the user explicitly asks — normal workflow skips this):
```bash
# foreground, timeout 120000 (background only per the duration rule above)
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Profile /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Release /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

**DataPacker** (Release only):
```bash
# foreground, timeout 120000 (background only per the duration rule above)
bash "$ROOT/.claude/msbuild.sh" "$ROOT/DataPacker/Platforms/VisualStudio2026/DataPacker.sln" /p:Configuration=Release /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

**BrokenEngineSandbox** — **Build client and server sequentially** (not in parallel) — they share a DataPacker pre-build step that causes full rebuilds if run concurrently. Default configuration is Debug; the user can request Profile or Release.

Client:
```bash
# foreground, timeout 120000 (background only per the duration rule above)
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

Server:
```bash
# foreground, timeout 120000 (background only per the duration rule above)
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

**AgentCli** (standalone harness driver — config-suffixed output: `AgentCli.Debug.exe` / `AgentCli.exe` (Release); rebuild only when its sources change). Debug default. Separate solution, so no DataPacker pre-build:
```bash
# foreground, timeout 120000 (background only per the duration rule above)
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Tools/AgentCli/Platforms/VisualStudio2026/AgentCli.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

### 2b. Selective File Compile (`--files` mode)

For fast-iteration on a small set of files, `msbuild.sh` supports a `--files` mode that deletes the targeted `.obj` files before invoking MSBuild, forcing just those files to recompile:

```bash
# foreground, timeout 120000. Configuration is required with --files.
bash "$ROOT/.claude/msbuild.sh" --files <path1.cpp> <path2.cpp> -- \
  "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj" \
  /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

Use when verifying a single-file change rather than a full solution rebuild. Constraints: target the `.vcxproj` (not the `.sln`); only `.cpp` paths are accepted — after a header change, pass the `.cpp` files that include it; each file must already be a member of the target vcxproj — a `.cpp` not in the project is silently skipped, so add new files to the vcxproj/filters before building them.

Shader sources (`.vert`/`.frag`/`.comp`, shader headers) cannot be passed to `--files` (the wrapper rejects non-`.cpp`). Shaders are compiled by DataPacker, which runs as a custom build step in every BrokenEngineSandbox build — including `--files` builds. After a shader-only change, run any client build (a `--files` build listing an arbitrary already-member `.cpp` is the cheapest) and check the DataPacker step output for shader compile errors.

### 3. Report Results

Report compactly — never paste the full build log, never summarize diagnostics:
- Final status: success / fail.
- Every `error` line verbatim (includes file:line).
- `warning` lines verbatim, only from files involved in the change — steady-state warnings elsewhere are noise.
- Special cases (name the one that applied):
  - **LNK errors (LNK1168, LNK2019 on the EXE)**: the client or server executable may be running and holding the `.exe` locked, preventing linking. These can be ignored — do not chase them unless the user asks. Report the condition to the caller so the user can confirm whether the exe is running.
  - **`unsuccessfulbuild` marker leftover**: a prior killed build leaves this marker; the next successful build clears it automatically. Do not hand-delete tlog files — just re-run the build.
  - **`Timed out waiting for lock`**: another build of the same solution is genuinely still running (stale locks are cleaned automatically via PID check). Re-run after it finishes; do not hand-delete `.claude/build-locks/` files — that can clobber a live build's lock.
