# Broken Engine

A C++23 Vulkan game engine client/server with data pre-packer, using data-oriented design. The camera views the world from kilometers above, looking down on islands scattered across an ocean. Units appear small on screen, similar to an RTS perspective.

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)
0. The user will use plan mode to create a planning document (or load a plan from a file)
	- DO NOT add a 'Verification' section
0.5. Invoke /external-grill-plan to interview the user about the plan — resolve ambiguities, gather missing information, and ensure plan completeness before implementation. **When the grill completes, DO NOT stop or summarize — immediately continue to step 1 in the same turn.**
1. Make the code changes using the planning document
2. Any new files created should be added to the appropriate filter in any relevant .vcproj files
3. Build the affected projects and verify there are no errors (see Build section below)
4. Use a Opus subagent to search the codebase and update all locations in the code affected by this modified code
5. Use a Opus subagent to invoke the code-review skill (evaluate advice for validity, query user if unsure). If review flags any files for `/reduce-file`, invoke it on them
6. Use a Opus subagent to invoke the code-style-review skill
7. Use a Opus subagent to invoke the update-claude-docs skill
8. After all previous steps (1-8) have completed, have an Opus subagent do a full audit/review of all the files changed in this session
9. Inform the user if the subagent in step #8 found any problems and how severe they were (interrogate user about what to fix and how)

## IMPORTANT Directives
- Follow KISS, YAGNI, DRY at all times
- If there is any ambiguity or multiple possible paths forward, stop and interrogate the user for input (present pros and cons of each)
- DO NOT run any Git commands
- DO NOT add error handling or validation - assume parameters to functions are valid
- DO NOT add unit tests
- Response style: no filler, no pleasantries, no hedging, no restating the request. Drop articles where natural, fragments fine. Technical terms and code unchanged. Prefer pattern: [thing] [action] [reason]

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

The codebase produces two executables from the same source: a **client** (full game with graphics, audio, input) and a **server** (headless physics simulation). The vcxproj defines either `BT_CLIENT` or `BT_SERVER`; use `#ifdef BT_CLIENT` / `#endif` to gate client-only code at the narrowest practical scope. Files fully wrapped in `#if defined(BT_CLIENT)` must only be in the client vcxproj; files fully wrapped in `#if defined(BT_SERVER)` must only be in the server vcxproj. See [VisualStudio2026/CLAUDE.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md) for detailed vcxproj inclusion rules. Collections with client-only fields use a three-method pattern: `SharedMembers()` (fields for both builds), `ClientMembers()` (client-only fields, `#ifdef BT_CLIENT`), and `Members()` which uses `std::tuple_cat` to combine them (client) or returns just `SharedMembers()` (server). See child CLAUDE.md files for subsystem-specific details.

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
- **Log levels**: `kVerbose` — per-frame or high-frequency events (reconciliation ticks, subscription churn, status changes). `kDebug` — one-time events like startup, setup, connect/disconnect. `kInfo` — app state transitions and high-importance one-time events (default threshold for most categories). `kWarning` — something to investigate (timeouts, overflows, clock desync, precursor-to-error); may spam. `kError` — failures; always logged regardless of threshold or category
