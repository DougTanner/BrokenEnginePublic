# Broken Engine

A C++23 Vulkan game engine client/server with data pre-packer, using data-oriented design. Top-down RTS-scale camera: kilometers above an ocean of islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z.

## Environment
- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out Opus subagents to validate each, compare pros/cons, then pick simplest good solution.
- **Architectural decisions** (new system shape, public API, data layout, threading model): stop and ask the user. Concisely present: (a) the problem, (b) proposed solutions, (c) pros and cons of each.

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)
Exception: for one-line changes, make the edit and do only step 3.
0. The user will use plan mode to create a planning document (or load a plan from a file)
	- DO NOT add a 'Verification' section — agents cannot run/play the game, so manual-test steps are noise
1. Invoke /external-grill-plan to interview the user about the plan — resolve ambiguities, gather missing information, and ensure plan completeness before implementation. **When the grill completes, DO NOT stop or summarize — immediately continue to step 2 in the same turn.**
2. Make the code changes using the planning document
	- 2b. Invoke /external-self-audit — the main session lists what it is least confident about in the changes (capped, evidence-required), investigates each item to root cause, fixes what's wrong, and passes remaining uncheckable items to the step 4 and step 9 reviewers as explicit focus areas
3. Use a Sonnet subagent to search the codebase and update all locations affected by the modified code
4. Use a Fable subagent to invoke the repo-code-review skill (evaluate advice for validity, query user if unsure). If review flags any files for `/reduce-file`, route them through step 10 (follow-up plan in `Documents/Plans/`) — do not run the split inline
5. Use an Sonnet subagent to invoke the code-style-review skill
6. Use a Fable subagent to invoke the update-claude-docs skill
7. Add any new files to the appropriate filter in the relevant .vcxproj files
8. Build the affected projects and verify there are no errors (see Build below)
9. After all previous steps complete, have Fable subagents do a full audit/review of all files changed this session — spread files across subagents by logical grouping
10. For problems from step 9 that weren't auto-fixed (architectural decisions, larger issues out-of-scope of the current plan), have an Opus subagent create plan files in `Documents/Plans/`

## Directives
- Follow KISS, YAGNI, DRY
- You may run ONLY read-only Git commands
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it. In order of preference: (1) make the condition impossible in calling code, (2) recover/handle gracefully if possible, (3) plain not-null ASSERTs — delete and let the null dereference crash, (4) if the linter still complains, handle case-by-case. Details: repo-code-review skill §2c.
- Do not add unit tests
- **Don't touch unrelated code**: only modify files, functions, and lines directly tied to the current task. Do not refactor, rename, reformat, or restyle adjacent code. Surface incidental findings — mention trivial observations in chat; for bugs or other important issues, route through C++ Code Change Process step 10 (a follow-up plan in `Documents/Plans/`). Remove only imports/usings/variables that *your* edits made unused.
- **Response style**: Stay concise — no pleasantries, hedging, or restating the request. But when the user (or output style) asks for explanation, provide the information fully. Concise ≠ omitting requested content. In code and commit messages: drop articles where natural, fragments fine, technical terms unchanged. Pattern: [thing] [action] [reason]

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace); `Common.h` is the single aggregation header (included by `Pch.h`) - [CLAUDE.md](Common/CLAUDE.md)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - [CLAUDE.md](DataPacker/Source/CLAUDE.md)
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, save the few deliberate exceptions noted at the subsystem hubs - [CLAUDE.md](Engine/Source/CLAUDE.md)
- `/Projects/` - Game implementations (`game::` namespace) - [CLAUDE.md](Projects/BrokenEngineSandbox/Source/CLAUDE.md)
- `/ThirdParty/` - External libraries (do not modify) - [CLAUDE.md](ThirdParty/CLAUDE.md)
- `/Documents/` - Style guide (`C++StyleGuide.txt`), Mermaid architecture diagrams (`Architecture/`), and the plan queues (`Plans/`, `Features/`) - [CLAUDE.md](Documents/CLAUDE.md)

## Build
Use the `/compile` skill for build commands and details.
Linker errors (LNK errors) can be ignored — the executable may be running (locking the file and preventing linking).

## Static Analysis
- `.editorconfig` (repo root) — formatting (Allman, tabs, spacing, include sort). VS applies on save / Ctrl+K, Ctrl+D.
- `.clang-tidy` (repo root) — enforced checks mapped to `Documents/C++StyleGuide.txt`; the `clang-analyzer-*` suite also runs (VS appends it after the config's Checks, so analyzer exclusions live in each vcxproj's `<ClangTidyChecks>`, not in `.clang-tidy`). Opt-in per project: *Properties → Code Analysis → Enable Clang-Tidy*. Deferred checks and details: comment block at the top of the file.

## Client/Server Builds

Same source, two executables:
- **client** (graphics, audio, input) — vcxproj defines `BT_CLIENT`
- **server** (headless physics) — vcxproj defines `BT_SERVER`

Rules:
- Gate client-only code with `#ifdef BT_CLIENT` at the narrowest practical scope — or, for whole files/headers, via client-vcxproj membership plus the `Engine.h` `BT_CLIENT` aggregation span (the established complementary mechanism; e.g. `AnimationData.*`, `OneShotCommandBuffer.*`, `Screenshot.*`, `Graphics.h`, `GraphicsUtils.h` carry no `#if` wrap).
- Files fully wrapped in `#if defined(BT_CLIENT)` must only appear in the client vcxproj; same for `BT_SERVER`. See [VisualStudio2026/CLAUDE.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md).
- Collections with client-only fields use the `SharedMembers()`/`ClientMembers()` pattern — see [Collections/CLAUDE.md](Engine/Source/Frame/Collections/CLAUDE.md)

## Key Patterns
- **Log levels**: `kVerbose` — per-frame / high-frequency. `kDebug` — one-time (startup, connect). `kInfo` — state transitions, important one-shots (default threshold). `kWarning` — investigate (timeouts, desync); may spam. `kError` — failures; always logged
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
	- **XMVECTOR W invariant**: Positions W=1.0; directions / velocities / normals / offsets W=0.0; color alpha defaults 1.0 (opaque)
	- **Function form, not operators**: `XMVectorAdd`/`Subtract`/`Multiply`/`Divide`/`Scale`/`Negate` — never `vec + vec`, `f * vec`, `-vec`.
- **Base classes**: Include/use game versions, not Base versions — `Camera.h` not `CameraBase.h`, `game::gpGame` not `GameBase` directly
- **Engine reading game-layer state is by design** — the engine may assume **any** type or symbol is declared by Game, and **Game is required to implement anything the engine assumes**. So engine code referencing any `game::` type, its static members/constants, or game-layer compile-time symbols (e.g. `game::gp*` runtime globals; `keNetworkSimulation` in game `Pch.h`; the game CPU-timer enum the Profile FPS overlay reads by name) — including direct calls to `game::` static functions — is **not** a layer violation; do not file plans to "inject" or "decouple" these. The reverse direction — engine *types* that name game concepts (e.g. an `engine::PacketType` enumerator only the game uses) — remains a real violation. Details: [Engine/Source/CLAUDE.md](Engine/Source/CLAUDE.md)
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See [Memory/CLAUDE.md](Engine/Source/Memory/CLAUDE.md)
- **LOG formatting**: in allocation-tracked builds only (Game and Engine are; offline DataPacker is not), never use float format specs (`{:.Nf}`, `{:e}`, etc.) in `LOG(...)` — they heap-allocate and trip the allocation tracker. Wrap floats with `common::Wb(value, precision)`, `XMVECTOR`s with `common::WbV2/V3/V4`; placeholder stays `{}`. Full rules: repo-code-review skill §2b.
- **Standard library / external headers**: `#include` additions for standard-library and third-party *consumption* headers go in `Common/ExternalHeaders.h` (gated by `BT_CLIENT`/`BT_ENGINE`/`BT_SERVER`/`BT_DATA_PACKER` as appropriate), not in individual source files. The exception is library *implementation* units — the single-TU `*_IMPLEMENTATION` includes in the `ThirdParty/Prebuilts/Source/` unity `.cpp`s — which stay local by necessity
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See [Common/CLAUDE.md](Common/CLAUDE.md)

## Diagnosis Discipline
- **Verify root cause before editing**: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- **If uncertain, say so** and re-investigate — never fabricate justifications when challenged.
- **Never remove working features as part of a 'fix'** without explicit confirmation.
