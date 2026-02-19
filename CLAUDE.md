# Broken Engine - Claude Code Instructions

A data-oriented C++23 Vulkan game engine optimized for fast-paced 3D action games.

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+
- **Shell**: Bash (Git Bash or similar)

## Shell Commands
The shell environment is bash, not PowerShell. Use Unix-style commands with forward slashes in paths:
- **Copy files**: `cp "source" "destination"`
- **Move files**: `mv "source" "destination"`
- **Delete files**: `rm "path"` or `rm "file1" "file2" "file3"`
- **List files**: `ls "path"`
- **Create directory**: `mkdir -p "path"`

Use forward slashes (`C:/Users/...`) or properly escaped backslashes in paths.

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
2. Any new files created should be added to the appropriate filter in any relevant .vcproj files
3. Use a subagent (task tool) to search the codebase and update all locations in the code affected by this modified code
4. Use a subagent (task tool) to invoke the code-review skill (evaluate advice for validity, query user if unsure)
5. Use a subagent (task tool) to invoke the code-style-review skill
6. Use a subagent (task tool) to invoke the update-claude-docs skill

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
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temporary allocations instead of local `std::vector` or `std::string`. Call `Push()` then `Append()`/`PushBack<T>()` to build, `View()` for string results, `Span<T>()` for typed arrays. Always call `Pop()` when done. Supports nesting up to 8 levels deep via push/pop stack -- inner `Push()`/`Pop()` pairs do not disturb outer consumers' data. If an allocation would exceed the default workbuffer size, the workbuffer sizes can always be increased — size is never a reason to avoid using the workbuffer.
- **Standard library headers**: New `#include <header>` additions go in `Common/ExternalHeaders.h`, not in individual source files
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple related `bool` variables. Define a `uint64_t`-backed `enum class` with bit values and a `using` alias (e.g., `using FooFlags_t = common::Flags<FooFlags>`). Use `Set()`, `Clear()`, `operator&` for testing, and `Empty()` to check if no flags are set
- **Coding style**: See `/Documents/C++StyleGuide.txt`
- **Multithreading**: Use `common::gpMultithreading->Dispatch(iCount, processRange)` for data-parallel work that splits a range across a worker pool plus the main thread. The `processRange` lambda receives `(int64_t iStart, int64_t iEnd)` and must write to non-overlapping output slots (no shared mutable state). Each worker has its own `ThreadLocal` with a 64KB workbuffer. Use `PersistentWorker` directly only for dedicated long-lived async tasks (render, submit, present). Never use `std::async`/`std::future` for per-frame parallel work — the thread creation overhead is too high.
