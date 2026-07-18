# Fresh-Machine Setup

Ordered bootstrap for a new Windows machine, from empty disk to a working agent worktree session. Each step's checks fail loudly, but nothing else sequences them — follow the order below. Install details (versions, winget commands, optional maintainer preferences) live in [README.md](../README.md); this document owns the sequence and the worktree-specific steps.

## 1. Enable Windows Developer Mode (before cloning)

Settings -> System -> For developers -> Developer Mode -> On. This grants the privilege Git needs to create symlinks. Without it, symlinked paths — notably `.claude/skills`, which exposes the tracked `.agents/skills` directory to Claude Code — check out as plain text files, and the session wrappers refuse to start.

Recovery for a clone made without Developer Mode: enable it, open a new terminal, then run `git config core.symlinks true` followed by `git checkout -- .claude/skills` in that clone.

## 2. Install prerequisites

Per [README.md](../README.md): Visual Studio 2026 Community (Desktop development with C++, Game development with C++, Windows 11 SDK), the Vulkan SDK, Git for Windows, PowerShell 7 (`pwsh`), Windows Terminal, and the agent CLIs (Claude Code and/or Codex CLI).

## 3. Clone with symlinks and submodules

```
git -c core.symlinks=true clone --recurse-submodules <repository-url>
```

This clone is the **primary checkout**. Agent sessions run in linked worktrees that share its immutable build outputs; the steps below prepare those outputs once.

## 4. Build primary ThirdParty in all three configurations

Worktree provisioning requires `ThirdParty.Debug.lib`, `ThirdParty.Profile.lib`, and `ThirdParty.Release.lib` under `ThirdParty/Prebuilts/Platforms/VisualStudio2026/Output/`. Build `ThirdParty/Prebuilts/Platforms/VisualStudio2026/ThirdParty.sln` (x64) in Debug, Profile, and Release — from Visual Studio, or from MSBuild with `/p:Configuration=<config> /p:Platform=x64`.

If provisioning later reports a submodule **pin mismatch**, the worktree predates a primary submodule update — rebase the worktree onto the primary tip so the pins agree (the error message names the command).

## 5. Export primary game data (Shared data mode)

Agent worktree builds default to Shared data mode, which consumes the primary checkout's exported data at `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data/`. Produce it once by building `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln` (x64, any configuration) in the primary checkout; its pre-build events build DataPacker and export the data. Re-run this export whenever data-affecting primary changes land.

## 6. First wrapper run (one-time ledger initialization)

The session wrappers coordinate through a WorktreeCli session ledger that must be initialized exactly once per machine, after explicitly confirming no legacy pre-protocol agent sessions are still open:

- Claude Code, from Git Bash at the primary checkout root: `./.claude/claude-worktree.sh --legacy-sessions-closed`
- Codex CLI, from PowerShell 7 at the primary checkout root: `.\.codex\codex-worktree.ps1 -LegacySessionsClosed`

Every later session omits the flag. The wrapper claims a session, builds the AgentTools executables (WorktreeCli, AgentHarness) automatically on first use, creates a UUID-named worktree, provisions links to the primary ThirdParty/tool outputs, verifies the worktree's `.claude/skills` link resolves, and launches the agent CLI inside the worktree. Do not bypass the wrapper.

Also once per machine, seed the plan queue store: run `plan order init` against the primary checkout. The plan queue is **machine-local state** under `%LOCALAPPDATA%\BrokenEngineLocks\plan-queue-state\`, not a tracked repository file, so a fresh machine starts with an empty queue (this is the accepted-risk design — plan *files* remain in the repo; only the scored row index is machine-local). `init` seeds from tracked `Order.md` files if any still exist, otherwise it writes empty header-only tables.
