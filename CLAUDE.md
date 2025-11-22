# Broken Engine - Claude Code Instructions

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.1
- **Platform**: Windows 10+

## IMPORTANT Directives
- DO NOT run any Git commands
- DO NOT build the Visual Studio projects/solutions
- DO NOT add error handling or validation - assume parameters to functions are valid
- DO NOT add unit tests

## IMPORTANT: C++ Code Change Process

0. The user will use plan mode to create a planning document
1. Make the code changes that the user requested
2. Search the codebase and update all locations in the code affected by this modified code
3. Use a subagent to invoke the code-review skill (evaluate advice for validity, query user if unsure)
4. Use a subagent to invoke the code-style-review skill
5. Use a subagent to invoke the update-claude-docs skill

## Architecture Overview
- **DataPacker**: 
- **Engine**: 
- **Projects**: Game implementations using the engine

## Directory Structure
- `/Common/` - Shared utilities and data formats (`common::` namespace)
- `/DataPacker/` - Pre-processes assets into optimized binary formats (`.pack` `.manifest` files)
- `/Engine/` - Core runtime systems: graphics, audio, input, game state (`engine::` namespace)
- `/Projects/` - Game implementations (`game::` namespace)
- `/ThirdParty/` - Outside libraries, DO NOT modify
- **Note**: Each major directory has its own CLAUDE.md with subsystem details

## Key Patterns
- **Managers**: Singletons accessed via globals (`gpGraphics`, `gpAudioManager`, etc.)
- **Memory**: RAII everywhere - no manual memory management
- **DirectX Math**: Prefer aligned versions (Float4A not Float4)
- Do not use or include 'Base' versions, ex: use Camera.h NOT CameraBase.h
- **Data-Oriented Frame System**: Game state uses Structure-of-Arrays (SOA) collections with 64-byte cache-line alignment. Dual-buffered updates (Interpolate phase for rendering, PostRender phase for logic) run deterministically with phase-based memory reallocation, enabling smooth interpolated rendering while maintaining replay determinism.
