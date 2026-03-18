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

Resolve the repo root from the current working directory. All paths below use `$ROOT` as shorthand for this absolute path.

### 2. Build the Project

**IMPORTANT: Build timeout** — MSBuild compiles 70+ C++ files and can exceed Claude Code's default 2-minute bash timeout. Always use `timeout: 600000` (10 minutes) on all build invocations. If MSBuild is killed mid-build it leaves an `unsuccessfulbuild` marker in the tlog directory, which forces a full rebuild next time — creating a cycle of never-completing builds.

**ThirdParty** (must be built first if libs are missing/stale — build all three configurations):
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

### 3. Report Results

- If the build succeeds, report success
- If the build fails, show the error output and suggest fixes
