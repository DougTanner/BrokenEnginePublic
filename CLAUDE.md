# Broken Engine - Claude Code Instructions

A data-oriented C++23 Vulkan game engine optimized for fast-paced 3D action games.

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## IMPORTANT Directives
- DO NOT run any Git commands
- DO NOT build the Visual Studio projects/solutions
- DO NOT add error handling or validation - assume parameters to functions are valid
- DO NOT add unit tests

## C++ Code Change Process
IMPORTANT: The Task tool should be used to spawn subagents as much as possible, to keep the main context concise
0. The user will use plan mode to create a planning document
1. Use a subagent to make the code changes using the user's request, the planning document, and relevant file paths
2. Use a subagent to search the codebase and update all locations in the code affected by this modified code
3. Use a subagent to invoke the code-review skill (evaluate advice for validity, query user if unsure)
4. Use a subagent to invoke the code-style-review skill
5. Use a subagent to invoke the update-claude-docs skill

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace)
- `/Projects/` - Game implementations (`game::` namespace)
- `/ThirdParty/` - External libraries (DO NOT modify)
- `/Documents/` - Style guide and documentation
- Each subdirectory has its own CLAUDE.md with detailed documentation

## Key Patterns
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
- **Base classes**: Use game versions, not Base versions (e.g., `Camera.h` not `CameraBase.h`)
- **Frame system**: SOA collections, dual-buffered updates (Interpolate/PostRender phases)
- **Coding style**: See `/Documents/C++StyleGuide.txt`
