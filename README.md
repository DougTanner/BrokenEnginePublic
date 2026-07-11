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

- **Enable Windows Developer Mode _before_ cloning** (Settings -> System -> For developers -> Developer Mode -> On). This grants the privilege Git needs to create symlinks. Without it, symlinked files check out as plain text files containing the link target instead of working links — notably `.agents/skills`, which points Codex at Claude Code's shared `.claude/skills`, so the shared skills fail to load.
- Repository should be cloned with `--recurse-submodules` and symlink support enabled so Codex and Claude Code share the same skills:
	- With Developer Mode on, clone with `git -c core.symlinks=true clone --recurse-submodules <repository-url>`
	- Or use "git submodule init" & "git submodule update" after cloning
	- If you already cloned without Developer Mode, enable it, open a new terminal (so the new privilege takes effect), then re-create the links with `git checkout -- .agents/skills`
- Consider setting "git config --global core.safecrlf false" to supress warnings about automatic endline conversions
	- "LF will be replaced by CRLF the next time Git touches it"

## Compile

- You can verify that everything is set up correctly by opening BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/DataPacker.sln
	- Compile it in Release

- Open BrokenEnginePublic/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln
    - Build in either Debug or Profile or Release (Build -> Build Solution)
	    - The first time you compile, a pre-build event will build the Data Packer at BrokenEnginePublic/DataPacker/Platforms/VisualStudio2026/Output/DataPacker.exe
	    - Any time data is changed, a pre-build event will run the Data Packer to export and package the data to BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data.bin
	- Run with Visual Studio (Debug -> Start Debugging)
