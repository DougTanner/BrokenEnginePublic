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

### 2. Build the Project

**IMPORTANT: Build duration** — Builds can run a long time. DataPacker's Island export, and any BrokenEngineSandbox build that triggers the DataPacker pre-build, can exceed the Bash tool's 10-minute hard timeout cap. If MSBuild is killed mid-build it leaves an `unsuccessfulbuild` marker in the tlog directory, which forces a full rebuild next time — creating a cycle of never-completing builds.

**Always run these builds with `run_in_background: true`** (no time cap, harness notifies on completion). Do not poll, do not sleep, do not chain shorter timeouts — just wait for the completion notification. Treat the build as taking effectively infinite time.

**IMPORTANT: clang-tidy disable** — every build command below passes `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`. The bundled VS2026 `clang-tidy.exe` crashes reproducibly with `0xC0000005` (access violation) on this codebase, blocking the actual C++ compile. Do not strip these flags.

**ThirdParty** (only rebuild if a link error references a missing ThirdParty `.lib` or the user explicitly asks — normal workflow skips this; a full ThirdParty rebuild across three configurations can exceed 30 minutes):
```bash
# run_in_background: true for each invocation
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Profile /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Release /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

**DataPacker** (Release only):
```bash
# run_in_background: true
bash "$ROOT/.claude/msbuild.sh" "$ROOT/DataPacker/Platforms/VisualStudio2026/DataPacker.sln" /p:Configuration=Release /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

**BrokenEngineSandbox** — **Build client and server sequentially** (not in parallel) — they share a DataPacker pre-build step that causes full rebuilds if run concurrently. Default configuration is Debug; the user can request Profile or Release.

Client:
```bash
# run_in_background: true
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

Server:
```bash
# run_in_background: true
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.sln" /p:Configuration=Debug /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

### 2b. Selective File Compile (`--files` mode)

For fast-iteration on a small set of files, `msbuild.sh` supports a `--files` mode that deletes the targeted `.obj` files before invoking MSBuild, forcing just those files to recompile:

```bash
# run_in_background: true. Configuration is required with --files.
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
