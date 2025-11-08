# Broken Engine - Claude Code Instructions

## Environment
- **IDE**: Visual Studio 2022
- **Language**: C++23
- **Graphics API**: Vulkan 1.1
- **Platform**: Windows 10+

## IMPORTANT Directives
- DO NOT run any Git commands
- YOU MUST follow the C++ code style guide: @Documents/C++StyleGuide.txt
- DO NOT add error handling or validation - assume parameters to functions are valid
- DO NOT add unit tests

## IMPORTANT C++ Code Change Process

1. Make the code changes that the user requested
2. Search the codebase and update all locations in the code affected by this modified code
3. Send the modified code to a subagent for review (evaluate advice for validity, then automatically make changes):
	- Do the changes solve the user's request?
	- Are there bugs in the code?
	- Is there any duplicated code that can be refactored into functions?
	- Can the code be simplified or cleaned up? Are these the minimal changes to solve the problem?
4. Use a subagent to run the /code-style-review slash command (.claude/commands/code-style-review.md) to trigger a code style review
5. Use a subagent to update CLAUDE.md files in the same directory to sync them with the changes
	- DO NOT mention changes or fixes or reference what was previously there, the CLAUDE.md file should only reflect what is CURRENTLY in the code

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
