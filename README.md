# Broken Teapot Studios Inc. - Broken Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Language](https://img.shields.io/badge/language-C++23-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B23)
[![Graphics API](https://img.shields.io/badge/language-vulkan-red.svg)](https://vulkan.lunarg.com/sdk/home)

Broken Engine is an open source C++23 Vulkan game engine for Windows. It uses data-oriented design, scalable parallelism, and a pre-compiled Vulkan command buffer for efficient CPU usage. The included Data Packer pre-processes files; pre-compiling shaders, packing .gltf and .obj files into efficient vertex buffers, re-encoding images files into BC4 and BC7 compressed textures.

The game Kinetic Storm runs on Broken Engine and is currently available on Steam: https://store.steampowered.com/app/2154430/Kinetic_Storm/?utm_source=github

## Prerequisites

- Vulkan SDK - 1.4.328.1 - https://vulkan.lunarg.com/sdk/home#windows
	- Volk header, source, and library
	- Vulkan Memory Allocator header
	- **Runtime Requirement**: GPU driver with Vulkan 1.2 or higher support

- Visual Studio 2026 Community - https://visualstudio.microsoft.com/vs/community/
	- Workloads
		- Desktop & Mobile - Desktop development with C++
		- Gaming - Game development with C++
	- Individual componenets
		- Windows 11 SDK (10.0.26100.6901)
	- Optional components can also be installed later from "Tools" -> "Get Tools and Features..."

## Git

- **Enable Windows Developer Mode _before_ cloning** (Settings -> System -> For developers -> Developer Mode -> On). This grants the privilege Git needs to create symlinks. Without it, symlinked files check out as plain text files containing the link target instead of working links — notably `.claude/skills`, which points to the repository's canonical tracked `.agents/skills` directory at `../.agents/skills` and exposes those skills to Claude Code.
- Repository should be cloned with `--recurse-submodules` and symlink support enabled so Codex and Claude Code share the same skills:
	- With Developer Mode on, clone with `git -c core.symlinks=true clone --recurse-submodules <repository-url>`
	- Or use "git submodule init" & "git submodule update" after cloning
	- If you already cloned without Developer Mode, enable it, open a new terminal (so the new privilege takes effect), then enable symlinks for that clone with `git config core.symlinks true` before restoring the link with `git checkout -- .claude/skills`
- Consider setting "git config --global core.safecrlf false" to supress warnings about automatic endline conversions
	- "LF will be replaced by CRLF the next time Git touches it"

## AI Coding CLIs

The suggested launch commands below bypass permission prompts and other safeguards. Use them only in a repository and environment where that level of access is intentional.

Use separate Windows Terminal profiles for the two clients: run Claude Code from Git Bash (installed with Git for Windows), and run Codex CLI from PowerShell 7. The repository wrappers and command examples use the native syntax of those shells; `pwsh` must also be available to Git Bash for shared PowerShell helpers.

### Claude Code

- Install [Claude Code](https://code.claude.com/docs/en/installation) from PowerShell:
	```powershell
	irm https://claude.ai/install.ps1 | iex
	```
- Close and reopen the terminal, then verify the installation with `claude --version`.
- Git for Windows is recommended on native Windows. From Git Bash at the repository root, use the provisioned-worktree wrapper:
	```bash
	./.claude/claude-worktree.sh
	```
- The shared host admits the session before bootstrap, creates and validates the report directory and stable primary dependency links, then tracks Claude for the claim's complete lifetime. On first protocol rollout, explicitly confirm all legacy sessions are closed before initializing coordination state. Do not bypass the wrapper.
- After that explicit one-time confirmation, initialize from Git Bash with `./.claude/claude-worktree.sh --legacy-sessions-closed`.

### Codex CLI

- Install [Codex CLI](https://learn.chatgpt.com/docs/codex/cli) from PowerShell:
	```powershell
	powershell -ExecutionPolicy Bypass -c "irm https://chatgpt.com/codex/install.ps1 | iex"
	```
- Close and reopen the terminal, then verify the installation with `codex --version`.
- From a PowerShell 7 tab inside Windows Terminal, opened at the repository root, load the repository helper and launch Codex in a UUID-named worktree:
	```powershell
	.\.codex\codex-worktree.ps1
	```
- After the same explicit one-time confirmation, initialize from PowerShell with `.\.codex\codex-worktree.ps1 -LegacySessionsClosed`.
- The wrapper creates branch `codex/<uuid>`, stores the worktree under `~/.codex/worktrees/<repository>/<uuid>`, validates the same links and report directory, and launches Codex with `--dangerously-bypass-approvals-and-sandbox`. Both wrappers wait up to 660 seconds while AgentCli maintenance is exclusive, track the client with kill-on-host-close lifetime, propagate its exit code, and preserve partial artifacts on provisioning failure.
- **Fable-unavailable fallback (Claude Code → Codex/Sol):** per the global model-fallback rule in `AGENTS.md`, when Fable is unavailable or over limit Claude Code runs delegated reviewer/auditor roles on Codex/Sol headless via `codex exec` — helper `.codex/codex-review.ps1`, driven by the `/codex-review` skill — instead of Fable, falling back to Opus if Codex is also unavailable. Codex bills the ChatGPT subscription, not metered API credits. Claude Code only: under the Fable→Sol mapping Codex is already Sol, so Codex never calls this.

## Compile

- You can verify that everything is set up correctly by opening BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/DataPacker.sln
	- Compile it in Release

- Open BrokenEnginePublic/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln
    - Build in either Debug or Profile or Release (Build -> Build Solution)
	    - The first time you compile, a pre-build event will build the Data Packer at BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/Output/DataPacker.exe
	    - Any time data is changed, a pre-build event will run the Data Packer to export and package the data to BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data.bin
	- Run with Visual Studio (Debug -> Start Debugging)
