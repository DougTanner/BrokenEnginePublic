# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean of islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. The client and server run a deterministic simulation, user input (client simulation change requests) are rare so we prioritize CPU/GPU smoothness over client -> server -> client input latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel; entity counts are uncapped — design data-parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager: delegate aggressively to keep the context small, except trivial edits
- Subagent instructions: CONCISE but COMPLETE — objective, scope, file paths, output format; add explicit non-scope when adjacent code could read as in-scope (sibling patterns, sites owned by another step or dispatch). Under-specification causes duplicated/out-of-scope work, but don't provide more than they need.

### When to use each model

Fable is the top tier for judgment roles (manager/planner/reviewer); don't move mechanical roles up to it or these roles down, except if Fable is not available fallback to Opus.

- Code/Web search: Haiku — must never summarize; return direct quotes, file:line references, or links for the main context to analyze
- Builds: Haiku — invoke `/compile`; return status + error/warning lines verbatim (main-session dispatch; implementing/fixing subagents build inline)
- Large-file/log filtering (game logs, LogDifferences output): Haiku — return matching lines verbatim
- Planning: Fable (fallback to Opus)
- New Code: Opus
- Code edits: Sonnet
- Style review: Sonnet
- Documentation: Opus
- Code/session review: Fable (fallback to Opus)

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)
Exception: for one-line changes, make the edit, then do step 3 and a selective `/compile`.
Each subagent reports files changed + functions/regions touched (one line each), ending with residuals — incomplete items, skipped fixes, findings not acted on — or "none"; main session accumulates and passes to all later steps (fresh contexts can't see other contexts' edit history), and re-dispatches residuals or routes them to step 10, never drops them silently. The residuals footer is appended after any skill-defined output format and takes precedence over a skill's "output only the template" phrasing.
Fix-dispatch subagents (steps 4 and 9) verify each finding still holds in current code before editing; a finding that doesn't hold returns REFUTED with evidence instead of an edit. A fix that changes semantics or one instance of a mirrored pattern must check counterpart sites (update-affected-code §3) and update them or report them as residuals.
0. The user will use plan mode to create a planning document (or load a plan from a file)
	- DO NOT add a 'Verification' section — agents cannot run/play the game, so manual-test steps are noise
1. Invoke /external-grill-plan to interview the user about the plan. **When the grill completes, DO NOT stop or summarize — immediately continue to step 2 in the same turn.**
2. An Opus subagent makes the code changes from the planning document, adds new files via the /update-vcxproj skill, builds changed files (selective `/compile`), and fixes compile errors before returning. If mid-implementation the plan contradicts actual code (wrong structural assumption, step can't work as written), stop that item and report it as a residual — never improvise past the contradiction. For large plans, split across multiple Opus subagents with disjoint file sets — each owns the full step 2 contract (including 2b) for its slice
	- 2b. Same subagent then invokes /external-self-audit; main session passes its handed-off items to the step 4 and step 9 reviewers as focus areas; sweep-exhaustiveness items also go to step 3
3. Use a Sonnet subagent to invoke the /update-affected-code skill — pass the changed-file list, touched functions/regions, the plan document (or intent summary), and the step 2b sweep-exhaustiveness handoffs
4. Use a Fable (fallback to Opus) subagent to invoke the /repo-code-review skill; the subagent returns findings, uncertain items, and API-verification requests — main session resolves verification requests via Haiku WebFetch subagents, evaluates validity, queries the user if unsure, then dispatches accepted fixes to a Sonnet subagent (Opus if a fix needs new code). If review flags any files for `/reduce-file`, route them through step 10. If shader files changed this session, a second Fable (fallback to Opus) subagent invokes the /glsl-review skill concurrently; its findings route identically
	- Steps 5, 6, and 7 have disjoint write sets (code style / CLAUDE.md docs / vcxproj verify) — once step 4's fixes land, dispatch all three concurrently in one message; step 8 is the barrier and builds step 5's fixes with the rest (step 9's code-vs-docs audit backstops rare doc staleness from step 5 renames)
5. Use a Sonnet subagent to invoke the /code-style-review skill
6. Use an Opus subagent to invoke the /update-claude-docs skill — and the /update-architecture-diagrams skill when its trigger applies
7. Haiku subagent invokes the /update-vcxproj skill (verify mode, fixing FAILs in place via add mode) on all files changed this session — reports pass/fixed/NOTE per file. This step owns vcxproj membership/filter mechanics
8. A Haiku subagent invokes the /compile skill. On errors: Sonnet subagent fixes (Opus if new code), rebuild; fixed files join the accumulated list
9. After all previous steps complete, Fable (fallback to Opus) subagents invoke the /session-audit skill on all files changed this session — fine-grained logical groups (per subsystem or per plan-step slice), code separate from docs. Findings only; main dispatches accepted fixes to Sonnet (Opus if new code) — keep fixes small, structural → step 10. Fixing subagent style-checks its edits and rebuilds via selective `/compile`; shader-touching fixes instead get a client build (DataPacker compiles shaders) and a glsl-review pass
10. For problems and residuals from any step that weren't auto-fixed (architectural decisions, larger issues out-of-scope of the current plan), have a Fable (fallback to Opus) subagent create plan files in `Documents/Plans/`

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out Opus subagents to validate each, compare pros/cons, then pick simplest good solution.
- **Architectural decisions** (new system shape, public API, data layout, threading model): stop and ask the user. Concisely present: (a) the problem, (b) proposed solutions, (c) pros and cons of each.

## Directives
- Follow KISS, YAGNI, DRY
- You may run ONLY read-only Git commands
- Claude Skills `.claude\skills` and CLAUDE.md files are used only by AI agents and must be kept CONCISE
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code or recovering gracefully. Resolution ladder: repo-code-review skill §2c.
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

## Static Analysis
- `.editorconfig` (repo root) — formatting (Allman, tabs, spacing, include sort). VS applies on save / Ctrl+K, Ctrl+D.
- `.clang-tidy` (repo root) — enforced checks mapped to `Documents/C++StyleGuide.txt`. Enablement/exclusion mechanics: [VisualStudio2026/CLAUDE.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md); deferred checks and details: comment block at the top of the file.

## Client/Server Targets

Build commands and details: `/compile` skill.

Same source, two executables:
- **client** (graphics, audio, input) — vcxproj defines `BT_CLIENT`
- **server** (headless physics) — vcxproj defines `BT_SERVER`

Rules:
- Gate client-only code with `#ifdef BT_CLIENT` at the narrowest practical scope — or, for whole files/headers, via client-vcxproj membership plus the `Engine.h` `BT_CLIENT` aggregation span (the established complementary mechanism — such files carry no `#if` wrap; examples: [Engine/Source/CLAUDE.md](Engine/Source/CLAUDE.md)).
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
- **Engine reading game-layer state is by design** — the engine may assume **any** `game::` type or symbol exists, and Game must implement whatever the engine assumes; do not file plans to "inject" or "decouple" these. The real violation is the reverse: engine *types* naming game concepts. Details: [Engine/Source/CLAUDE.md](Engine/Source/CLAUDE.md)
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See [Memory/CLAUDE.md](Engine/Source/Memory/CLAUDE.md)
- **LOG formatting**: in allocation-tracked builds only (Game and Engine are; offline DataPacker is not), never use allocating format specs (`{:.Nf}`, `{:e}`, width/precision like `{:>10}`) or the `std::format` family in `LOG(...)` — they trip the allocation tracker. Wrap floats with `common::Wb(value, precision)`, `XMVECTOR`s with `common::WbV2/V3/V4`; placeholder stays `{}`. Full rules: repo-code-review skill §2b.
- **Standard library / external headers**: new standard-library/third-party `#include`s go in `Common/ExternalHeaders.h` (gated by build defines), not in individual source files — rule and exception: [Common/CLAUDE.md](Common/CLAUDE.md)
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See [Common/CLAUDE.md](Common/CLAUDE.md)
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See [Common/CLAUDE.md](Common/CLAUDE.md)

## Diagnosis Discipline
- **Verify root cause before editing**: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- **If uncertain, say so** and re-investigate — never fabricate justifications when challenged.
- **Never remove working features as part of a 'fix'** without explicit confirmation.
