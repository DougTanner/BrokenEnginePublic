# Broken Teapot Studios Inc. - Broken Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Language](https://img.shields.io/badge/language-C++23-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B23)
[![Graphics API](https://img.shields.io/badge/language-vulkan-red.svg)](https://vulkan.lunarg.com/sdk/home)

Broken Engine is an open source C++23 Vulkan game engine for Windows. It uses data-oriented design, scalable parallelism, and a pre-compiled Vulkan command buffer for efficient CPU usage. The included Data Packer pre-processes files; pre-compiling shaders, packing .gltf and .obj files into efficient vertex buffers, re-encoding images files into BC4 and BC7 compressed textures.

The game Kinetic Storm runs on Broken Engine and is currently available on Steam: https://store.steampowered.com/app/2154430/Kinetic_Storm/?utm_source=github

Setting up a new machine? [Documents/FreshMachineSetup.md](Documents/FreshMachineSetup.md) sequences the full bootstrap (Developer Mode → clone → primary ThirdParty builds → data export → first wrapper run); the sections below hold the per-tool details.

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
- Optionally run `git config --global core.safecrlf false` to suppress warnings about automatic line-ending conversions.
	- Example: `LF will be replaced by CRLF the next time Git touches it`

## Windows Shell Tools

- Git for Windows and PowerShell 7 are required for the documented AI coding workflow. Git for Windows includes Git Bash.
	```powershell
	winget install --id Git.Git --exact --source winget
	winget install --id Microsoft.PowerShell --exact --source winget
	```
- Windows Terminal is the recommended host:
	```powershell
	winget install --id Microsoft.WindowsTerminal --exact --source winget
	```
- Close and reopen Windows Terminal after installation. Use its discovered Git Bash profile for Claude Code and PowerShell profile for Codex CLI.
- From Git Bash, verify both required tools are available:
	```bash
	git --version
	pwsh --version
	```
	PowerShell 7 (`pwsh`), not Windows PowerShell 5.1 (`powershell`), is required by the shared repository helpers.

### Optional Current Maintainer Shell Tweaks

- Windows Terminal auto-discovers Git Bash and PowerShell. The current custom Git Bash profile launches `C:\Program Files\Git\bin\bash.exe --login -i`, preserves the caller's current directory, and leaves `startingDirectory` unset instead of fixing it to one location.
- The current Git Bash `~/.bashrc` appends each command to history and reports the current directory to Windows Terminal with OSC 9;9:
	```bash
	shopt -s histappend
	PROMPT_COMMAND='history -a; printf "\e]9;9;%s\e\\" "$(cygpath -w "$PWD")"'
	```
	If `PROMPT_COMMAND` already exists, merge these operations into it rather than overwriting it blindly.
- A PowerShell 7 profile can optionally dot-source the repository helper so `Invoke-CodexWorktree` is available in each session (replace `<clone>` with the clone path):
	```powershell
	. '<clone>\.codex\codex-worktree.ps1'
	```
	Direct repository-relative invocation with `.\.codex\codex-worktree.ps1` remains preferred. The current local prompt also emits OSC 9;9 for Windows Terminal current-directory tracking; its full prompt override, history injection, and cosmetic settings are personal and intentionally omitted here.

## AI Coding CLIs

The suggested launch commands below bypass permission prompts and other safeguards. Use them only in a repository and environment where that level of access is intentional.

Use separate Windows Terminal profiles for the two clients: run Claude Code from Git Bash, and run Codex CLI from PowerShell 7. The repository wrappers and command examples use the native syntax of those shells; `pwsh` must also be available to Git Bash for shared PowerShell helpers.

### Claude Code

- Install [Claude Code](https://code.claude.com/docs/en/installation) from PowerShell:
	```powershell
	irm https://claude.ai/install.ps1 | iex
	```
- Close and reopen the terminal, then verify the installation with `claude --version`.
- Git for Windows is required for this documented native-Windows workflow. From Git Bash at the repository root, use the provisioned-worktree wrapper:
	```bash
	./.claude/claude-worktree.sh
	```
- The tracked `.claude/settings.json` sets `worktree.baseRef` to `head`, so Claude-created worktrees branch from the commit currently checked out in the session worktree; no per-user setting is required.
- The shared host admits the session before bootstrap, creates and validates the report directory and stable primary dependency links, then tracks Claude for the claim's complete lifetime. On first protocol rollout, explicitly confirm all legacy sessions are closed before initializing coordination state. Do not bypass the wrapper.
- After that explicit one-time confirmation, initialize from Git Bash with `./.claude/claude-worktree.sh --legacy-sessions-closed`.

#### Optional Current Maintainer Preferences

These user-level Claude Code settings favor explicit planning and long manual context management. Merge them into `~/.claude/settings.json` only if those tradeoffs suit your workflow:

```json
{
  "permissions": {
    "defaultMode": "plan"
  },
  "model": "opus[1m]",
  "effortLevel": "xhigh",
  "promptSuggestionEnabled": false,
  "autoMemoryEnabled": false,
  "verbose": true,
  "autoCompactEnabled": false,
  "useAutoModeDuringPlan": false
}
```

The wrapper already supplies the dangerous permission bypass; no additional setting is needed to suppress that confirmation.

### Codex CLI

- Install [Codex CLI](https://learn.chatgpt.com/docs/codex/cli) from PowerShell:
	```powershell
	pwsh -ExecutionPolicy Bypass -c "irm https://chatgpt.com/codex/install.ps1 | iex"
	```
- Close and reopen the terminal, then verify the installation with `codex --version`.
- Run `codex login status` and confirm it reports a ChatGPT login. For the subscription-backed Claude-to-Codex fallback, do not set `OPENAI_API_KEY`; doing so switches the fallback to metered API billing.
- From a PowerShell 7 tab inside Windows Terminal, opened at the repository root, load the repository helper and launch Codex in a UUID-named worktree:
	```powershell
	.\.codex\codex-worktree.ps1
	```
- After the same explicit one-time confirmation, initialize from PowerShell with `.\.codex\codex-worktree.ps1 -LegacySessionsClosed`.
- The wrapper creates branch `codex/<uuid>`, stores the worktree under `~/.codex/worktrees/<repository>/<uuid>`, validates the same links and report directory, and launches Codex with `--dangerously-bypass-approvals-and-sandbox`. Both wrappers wait up to 660 seconds while WorktreeCli maintenance is exclusive, track the client with kill-on-host-close lifetime, propagate its exit code, and preserve partial artifacts on provisioning failure.
- **Fable-unavailable fallback (Claude Code → Codex/Sol):** per the global model-fallback rule in `AGENTS.md`, when Fable is unavailable or over limit Claude Code runs delegated reviewer/auditor roles on Codex/Sol headless via `codex exec` — helper `.codex/codex-review.ps1`, driven by the `/codex-review` skill — instead of Fable, falling back to Opus if Codex is also unavailable. Codex bills the ChatGPT subscription, not metered API credits. Claude Code only: under the Fable→Sol mapping Codex is already Sol, so Codex never calls this.

#### Optional Current Maintainer Configuration

The following tested `~/.codex/config.toml` profile uses medium reasoning for normal work, xhigh reasoning in plan mode, and a large manual context window. Model availability can change, so this is a preference rather than a CLI version requirement:

```toml
model = "gpt-5.6-sol"
model_context_window = 1000000
model_auto_compact_token_limit = 900000
model_reasoning_effort = "medium"
plan_mode_reasoning_effort = "xhigh"
service_tier = "default"
```

Optional workflow-focused desktop preferences, excluding personal font sizing:

```toml
[desktop]
conversationDetailMode = "STEPS_COMMANDS"
ambient-suggestions-enabled = false
followUpQueueMode = "queue"
show-context-window-usage = true
notifications-turn-mode = "unfocused"
```

For a minimal engine-work capability profile, merge these entries into the same file. Do not duplicate a TOML table that already exists:

```toml
[plugins."visualize@openai-bundled"]
enabled = false
[plugins."google-calendar@openai-curated"]
enabled = false
[plugins."slack@openai-curated"]
enabled = false
[plugins."browser@openai-bundled"]
enabled = false
[plugins."documents@openai-primary-runtime"]
enabled = false
[plugins."pdf@openai-primary-runtime"]
enabled = false
[plugins."spreadsheets@openai-primary-runtime"]
enabled = false
[plugins."presentations@openai-primary-runtime"]
enabled = false
[plugins."template-creator@openai-primary-runtime"]
enabled = false
[plugins."sites@openai-bundled"]
enabled = false
[plugins."openai-templates@openai-curated"]
enabled = false
[plugins."sites@openai-bundled".mcp_servers.sites-design-picker]
enabled = false

[features]
js_repl = false

[mcp_servers.openaiDeveloperDocs]
url = "https://developers.openai.com/mcp"
```

Keep the runtime-generated `node_repl` command and environment values intact; edit its existing `[mcp_servers.node_repl]` block to add `enabled = false`. Restart Codex and open a new thread after capability changes, then verify the active state with `codex plugin list` and `codex mcp list`. The profile disables Visualize, Google Calendar, Slack, Browser, Documents, PDF, Spreadsheets, Presentations, Template Creator, Sites, and OpenAI Templates, plus `js_repl`, `node_repl`, and `sites-design-picker`; it retains `openaiDeveloperDocs`.

## Compile

- You can verify that everything is set up correctly by opening BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/DataPacker.sln
	- Compile it in Release

- Open BrokenEnginePublic/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln
    - Build in either Debug or Profile or Release (Build -> Build Solution)
	    - The first time you compile, a pre-build event will build the Data Packer at BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/Output/DataPacker.exe
	    - Any time data is changed, a pre-build event will run the Data Packer to export and package the data to BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data.bin
	- Run with Visual Studio (Debug -> Start Debugging)
