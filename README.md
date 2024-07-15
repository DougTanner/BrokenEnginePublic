# Broken Teapot Studios - Broken Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Language](https://img.shields.io/badge/language-C++20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

Broken Engine is an open source C++20 Vulkan game engine for Windows. It uses data-oriented design, scalable parallelism, and a pre-compiled Vulkan command buffer for efficient CPU usage. The included Data Packer pre-processes files; pre-compiling shaders, packing .gltf and .obj files into efficient vertex buffers, re-encoding images files into BC4 and BC7 compressed textures, and processing .wav files into ADPCM audio.

The game Kinetic Storm runs on Broken Engine and is currently in Early Access on Steam: https://store.steampowered.com/app/2154430/Kinetic_Storm/?utm_source=github

## Prerequisites

- Vulkan SDK - 1.3.275.0 - https://vulkan.lunarg.com/sdk/home#windows
	- Default components

- Visual Studio 2022 Community - 17.10.5 - https://visualstudio.microsoft.com/vs/community/
	- Workloads
		- Gaming - Game development with C++
	- Individual componenets
		- Windows 10 SDK (10.0.19041.0)
	- Optional components can also be installed later from "Tools" -> "Get Tools and Features..."

## Git

- Repository should be cloned with --recurse-submodules
    - Or use "git submodule init" & "git submodule update" after cloning

## Compile

- You can verify that everything is set up correctly by opening BrokenEnginePublic/DataPacker/Platforms/VisualStudio2022/DataPacker.sln
	- Compile it in Release

- Open BrokenEnginePublic/Projects/BrokenEngineSandbox/Platforms/VisualStudio2022/BrokenEngineSandbox.sln
    - Build in either Debug or Profile or Release (Build -> Build Solution)
	    - The first time you compile, a pre-build event will build the Data Packer at BrokenEnginePublic/DataPacker/Platforms/VisualStudio2022/Output/DataPacker.exe
	    - Any time data is changed, a pre-build event will run the Data Packer to export and package the data to BrokenEngineSandbox/Platforms/VisualStudio2022/Output/Data.bin
	- Run with Visual Studio (Debug -> Start Debugging)
