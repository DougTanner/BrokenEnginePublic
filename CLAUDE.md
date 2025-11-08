# Broken Engine - Claude Code Instructions

## Environment
- **IDE**: Visual Studio 2022
- **Language**: C++23
- **Platform**: Windows 10+

## IMPORTANT Directives
- YOU MUST follow the C++ code style guide at `/Documents/C++StyleGuide.txt`
- YOU MUST search the codebase and update all usage locations after modifying code
- YOU MUST update CLAUDE.md files when making code changes
- YOU MUST update CLAUDE.md files when you discover undocumented functionality
- DO NOT run any Git commands
- DO NOT split function calls across multiple lines - keep all function arguments on the same line as the function name
	- Except for lambdas and structs with designated initializers -> { goes on next line
- When adding multiple lines of code that are related add a single line comment before them explaining what they do
	- Also add comments if the purpose of any code is not obvious from the immediate context
	- DO NOT leave comments explaining what code has been removed or what bugs have been fixed
- DO NOT add error handling or validation - assume parameters are valid
- DO NOT add tests, only write the code

## Architecture Overview
- **DataPacker**: Pre-processes assets into optimized binary formats (`.pack` files)
- **Engine**: Core runtime systems (graphics, audio, input, game state)
- **Projects**: Game implementations using the engine
- **Common**: Shared utilities and data formats

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace)
- `/DataPacker/` - Asset preprocessing tool
- `/Engine/` - Core engine (`engine::` namespace)
- `/Projects/` - Game implementations (`game::` namespace)
- `/ThirdParty/` - Outside libraries, these are not to be modified
- **Note**: Each major directory has its own CLAUDE.md with subsystem details
- Ignore: `LICENSE.md`, `README.md`, `.git/`, `.vs/`, `Build/`

## Key Patterns
- **Managers**: Singletons accessed via globals (`gpGraphics`, `gpAudioManager`, etc.)
- **Memory**: RAII everywhere - no manual memory management
- **DirectX Math**: Prefer aligned versions (Float4A not Float4)
