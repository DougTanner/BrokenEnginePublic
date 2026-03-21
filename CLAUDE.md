# Broken Engine

A C++23 Vulkan game engine client/server with data pre-packer, using data-oriented design.

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)
0. The user will use plan mode to create a planning document (or load a plan from a file)
	- DO NOT add a 'Verification' section
0.5. Invoke /external-grill-plan to interview the user about the plan — resolve ambiguities, gather missing information, and ensure plan completeness before implementation
1. Make the code changes using the planning document
2. Any new files created should be added to the appropriate filter in any relevant .vcproj files
3. Build the affected projects and verify there are no errors (see Build section above)
4. Use a Sonnet subagent (task tool) to search the codebase and update all locations in the code affected by this modified code
5. Use a Sonnet subagent (task tool) to invoke the code-review skill (evaluate advice for validity, query user if unsure). If review flags any files for `/reduce-file`, invoke it on them
6. Use a Sonnet subagent (task tool) to invoke the code-style-review skill
7. Use a Sonnet subagent (task tool) to invoke the update-claude-docs skill

## IMPORTANT Directives
- DO NOT run any Git commands
- DO NOT add error handling or validation - assume parameters to functions are valid
- DO NOT add unit tests
- Follow KISS, YAGNI, DRY at all times
- **File size limits**: Code files (.h/.cpp) should aim to be under 500-1000 lines
	- **Over 500 lines**: Look for opportunities to refactor logic into utility headers or helper files
	- **Over 1000 lines**: The file should be split into multiple .h/.cpp files (a human user must verify the split plan before implementation)
	- **Struct splitting**: Structs with static methods (e.g., SOA collections) can be split across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). Classes must NOT be split this way — extract independent classes instead. See `/reduce-file` skill
- **Function size**: Aim for 50-100 lines max per function. Soft guideline - some functions are legitimately large

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace); `Common.h` is the single aggregation header (included by `Pch.h`) - [CLAUDE.md](Common/CLAUDE.md)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - [CLAUDE.md](DataPacker/Source/CLAUDE.md)
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards - [CLAUDE.md](Engine/Source/CLAUDE.md)
- `/Projects/` - Game implementations (`game::` namespace) - [CLAUDE.md](Projects/BrokenEngineSandbox/Source/CLAUDE.md)
- `/ThirdParty/` - External libraries (DO NOT modify)
- `/Documents/` - Style guide (`C++StyleGuide.txt`) and architecture overview (`Overview.txt`)

## Build
Use the `/compile` skill for build commands and details. Always use `timeout: 600000` (10 minutes) on all build invocations.
Linker errors (LNK errors) can be ignored — the client or server executable may be running, which locks the file and prevents linking.

## Client/Server Builds

The codebase produces two executables from the same source: a **client** (full game with graphics, audio, input) and a **server** (headless physics simulation). The vcxproj defines either `BT_CLIENT` or `BT_SERVER`; use `#ifdef BT_CLIENT` / `#endif` to gate client-only code at the narrowest practical scope. Collections with client-only fields use a three-method pattern: `SharedMembers()` (fields for both builds), `ClientMembers()` (client-only fields, `#ifdef BT_CLIENT`), and `Members()` which uses `std::tuple_cat` to combine them (client) or returns just `SharedMembers()` (server). See child CLAUDE.md files for subsystem-specific details.

## Key Patterns
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
- **Base classes**: Use game versions, not Base versions (e.g., `Camera.h` not `CameraBase.h`)
- **Engine -> Game**: Engine code includes `Game.h` and accesses game functionality via `game::gpGame` (not GameBase directly). Never create globals for Base classes - always use the game-derived version
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temporary allocations instead of local `std::vector` or `std::string`. Supports nested push/pop and can always be grown if needed. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()` — prefer workbuffer or static storage. When unavoidable, use `ScopedSuppressAllocationTracking` and add a `// Heap:` comment explaining why. See [Memory/CLAUDE.md](Engine/Source/Memory/CLAUDE.md)
- **Standard library headers**: `#include <header>` additions go in `Common/ExternalHeaders.h`, not in individual source files
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See [Common/CLAUDE.md](Common/CLAUDE.md)
