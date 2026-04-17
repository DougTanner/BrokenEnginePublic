---
name: compile
description: Builds Broken Engine projects using the MSBuild lock wrapper. Use this skill when you need to compile ThirdParty, DataPacker, or BrokenEngineSandbox.
allowed-tools: [Bash]
user-invocable: true
---

# Build

Builds Broken Engine projects via `.claude/msbuild.sh`, a lock wrapper that serializes MSBuild access per-solution to prevent concurrent builds from corrupting tlog files. The wrapper handles locking, stale lock cleanup, path normalization, and `MSYS_NO_PATHCONV=1` automatically.

## Instructions

### 1. Determine What to Build

Check the conversation history or user request to determine which project(s) need building. If the user just says "build" without specifying, build **BrokenEngineSandbox client** (Debug).

Use the repo root as an absolute path — the harness provides the cwd as an absolute path in the environment block, or fall back to `pwd`. All paths below use `$ROOT` as shorthand for this absolute path.

### 2. Build the Project

**IMPORTANT: Build timeout** — MSBuild compiles 70+ C++ files and can exceed Claude Code's default 2-minute bash timeout. Always use `timeout: 600000` (10 minutes) on all build invocations. If MSBuild is killed mid-build it leaves an `unsuccessfulbuild` marker in the tlog directory, which forces a full rebuild next time — creating a cycle of never-completing builds.

**ThirdParty** (only rebuild if a link error references a missing ThirdParty `.lib` or the user explicitly asks — normal workflow skips this; a full ThirdParty rebuild across three configurations can exceed 30 minutes):
```bash
# timeout: 600000 for each invocation
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Profile /p:Platform=x64 /verbosity:minimal
bash "$ROOT/.claude/msbuild.sh" "$ROOT/ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln" /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
```

**DataPacker** (Release only):
```bash
# timeout: 600000
bash "$ROOT/.claude/msbuild.sh" "$ROOT/DataPacker/Platforms/VisualStudio2026/DataPacker.sln" /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
```

**BrokenEngineSandbox** — **Build client and server sequentially** (not in parallel) — they share a DataPacker pre-build step that causes full rebuilds if run concurrently. Default configuration is Debug; the user can request Profile or Release.

Client:
```bash
# timeout: 600000
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
```

Server:
```bash
# timeout: 600000
bash "$ROOT/.claude/msbuild.sh" "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.sln" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
```

### 2b. Selective File Compile (`--files` mode)

For fast-iteration on a small set of files, `msbuild.sh` supports a `--files` mode that deletes the targeted `.obj` files before invoking MSBuild, forcing just those files to recompile:

```bash
# timeout: 600000. Configuration is required with --files.
bash "$ROOT/.claude/msbuild.sh" --files <path1.cpp> <path2.cpp> -- \
  "$ROOT/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj" \
  /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
```

Use when verifying a single-file change rather than a full solution rebuild.

### 3. Report Results

- If the build succeeds, report success.
- If the build fails, show the error output and suggest fixes, with two exceptions:
  - **LNK errors (LNK1168, LNK2019 on the EXE)**: the client or server executable may be running and holding the `.exe` locked. `CLAUDE.md` says these can be ignored — do not chase them unless the user asks. Ask the user whether the exe is running first.
  - **`unsuccessfulbuild` marker leftover**: a prior killed build leaves this marker; the next successful build clears it automatically. Do not hand-delete tlog files — just re-run the build.
