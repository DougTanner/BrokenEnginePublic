# Broken Engine - Claude Code Instructions

## Environment
- **IDE**: Visual Studio 2022
- **Language**: C++23
- **Platform**: Windows 10+

## IMPORTANT Directives
- YOU MUST follow the C++ code style guide at `/Documents/C++StyleGuide.txt`
- YOU MUST add comments before each block when adding new code
- YOU MUST search the codebase and update all usage locations after modifying code
- YOU MUST update CLAUDE.md files when making code changes
- YOU MUST update CLAUDE.md files when you discover undocumented functionality
- Do not interact with Git
- Do not add error handling or validation - assume parameters are valid
- Don't test anything, only write the code

## Workflow
1. **Adding Features**: Modify code → Update usage locations → Update relevant CLAUDE.md files
2. **Asset Changes**: Edit in `/Engine/Data/` or `/Projects/*/Data/` → DataPacker auto-runs on build
3. **Testing**: No test framework - assume all inputs are valid
4. **Error Handling**: Fatal errors only (use exceptions sparingly)

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
