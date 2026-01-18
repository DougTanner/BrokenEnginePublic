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
- Follow KISS, YAGNI, DRY at all times
	- Don't repeat yourself
	- Keep it simple, stupid
	- You aren't gonna need it

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)
0. The user will use plan mode to create a planning document (or load a plan from a file)
1. Make the code changes using the planning document
2. Use a subagent (task tool) to search the codebase and update all locations in the code affected by this modified code
3. Use a subagent (task tool) to invoke the code-review skill (evaluate advice for validity, query user if unsure)
4. Use a subagent (task tool) to invoke the code-style-review skill
5. Use a subagent (task tool) to invoke the update-claude-docs skill

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace) - [CLAUDE.md](Common/CLAUDE.md)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - [CLAUDE.md](DataPacker/Source/CLAUDE.md)
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace) - [CLAUDE.md](Engine/Source/CLAUDE.md)
- `/Projects/` - Game implementations (`game::` namespace) - [CLAUDE.md](Projects/BrokenEngineSandbox/Source/CLAUDE.md)
- `/ThirdParty/` - External libraries (DO NOT modify)
- `/Documents/` - Style guide (`C++StyleGuide.txt`) and architecture overview (`Overview.txt`)

## Key Patterns
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
- **Base classes**: Use game versions, not Base versions (e.g., `Camera.h` not `CameraBase.h`)
- **Frame system**: SOA collections, dual-buffered updates (Interpolate/PostRender phases)
- **Engine -> Game**: Engine code includes `Game.h` and accesses game functionality via `game::gpGame` (not GameBase directly). Never create globals for Base classes - always use the game-derived version
- **Coding style**: See `/Documents/C++StyleGuide.txt`
