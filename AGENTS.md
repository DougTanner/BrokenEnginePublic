# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. The client and server run a deterministic simulation, user input (client simulation change requests) are rare so we prioritize CPU/GPU smoothness over client -> server -> client input latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager: delegate aggressively to keep the context small, except trivial edits
- Subagent instructions: CONCISE but COMPLETE — objective, scope, file paths, output format. Provide them only with the instructions and context they need.

### When to use each model

Note: If you are ChatGPT Codex, Fable -> Sol, Opus -> Terra, Sonnet -> Luna

Fable is the top tier for judgment roles (manager/planner/reviewer); don't move mechanical roles up to it or these roles down. IMPORTANT: If Fable is unavailable or its limit has been reached, Claude Code uses /codex-review for delegated reviewer/auditor roles, then Opus if Codex is also unavailable; other Fable roles fall back directly to Opus. Codex uses Opus (Terra).

- Code/Web search: Sonnet — must never summarize; return direct quotes, file:line references, or links for the main context to analyze
- Builds: Sonnet — invoke `/compile`; return status + error/warning lines verbatim
- Large-file/log filtering: Sonnet — return matching lines verbatim
- Planning: Fable
- New Code: Opus
- Code edits: Sonnet
- Style review: Sonnet
- Documentation: Opus
- Code/session review: paired Fable + Opus (step 5: /repo-code-review + /adversarial-review; step 11: two /session-audit); step-11 fix re-review: Opus

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)

Exception for one-line changes: Make the edit yourself, then do step 4
Each subagent reports files changed + functions/regions touched (one line each), ending with residuals — incomplete items, skipped fixes, findings not acted on — or "none"; main session accumulates and passes to all later steps (subagents can't see other subagents' session), and re-dispatches residuals or routes them to step 12, never drops them silently. The residuals footer is appended after any skill-defined output format and takes precedence over a skill's "output only the template" phrasing.

0. The user will use plan mode to create a planning document (or load a plan from a file)
	- Plans MAY include a Verification section of agent-harness steps (launch, drive, query, screenshot — see the agent-harness skill); optional, so trivial refactors don't gold-plate
1. Have one Fable subagent invoke /plan-audit on the plan file. The audit is findings-only; the main session validates its findings and carries accepted flaws and improvements into step 2.
2. Invoke /external-grill-plan to interview the user about the plan and the accepted audit findings. **When the user has responded to the grill, DO NOT stop or summarize — immediately continue to step 3 in the same turn.**
3. Have Opus subagents make the code changes from the planning document, for large plans split across multiple Opus subagents with disjoint file sets. If mid-implementation the plan contradicts actual code (wrong structural assumption, step can't work as written), stop that item and report it as a residual — subagents never improvise past the contradiction. After the code is written the subagent's session then invokes /external-self-audit; main session passes its handed-off items to the step 5 and step 11 reviewers as focus areas; sweep-exhaustiveness items also go to step 4
4. Use a Sonnet subagent to invoke the /update-affected-code skill — pass the changed-file list, touched functions/regions, the plan document (or intent summary), and the step 3 sweep-exhaustiveness handoffs
5. Two concurrent reviewers on the same inputs (changed files, touched regions, plan, accumulated residuals/focus areas): a Fable subagent invokes the /repo-code-review skill; an Opus subagent invokes the /adversarial-review skill — independent contexts are the point. Main session merges and dedupes both finding sets (agreement is strong signal; disagreements judged on evidence, never on which model reported it), resolves API-verification requests via Sonnet WebFetch subagents, evaluates validity, queries the user if unsure, then dispatches accepted fixes to an Opus subagent. If review flags any files for `/reduce-file`, route them through step 12. If shader files changed this session, another Opus subagent invokes the /glsl-review skill concurrently; its findings route identically
6. Use a Sonnet subagent to invoke the /code-style-review skill
7. Use an Opus subagent to invoke the /update-claude-docs skill — and the /update-architecture-diagrams skill if its trigger applies
8. A Sonnet subagent invokes the /update-vcxproj skill (verify mode, fixing FAILs in place via add mode) on all files changed this session — reports pass/fixed/NOTE per file. This step owns vcxproj membership/filter mechanics
9. A Sonnet subagent invokes the /compile skill. On errors: Opus subagent fixes
10. Whenever the change has an observable runtime surface, an Opus subagent invokes the /agent-harness skill to verify it live: run the plan's Verification section if present, else derive minimal launch/drive/query checks from the plan's acceptance criteria; report PASS/FAIL per criterion with evidence lines verbatim. Skip only when nothing is runtime-observable (docs/comment-only changes). FAILs: Opus subagent diagnoses per Diagnosis Discipline before any fix
11. After all previous steps complete, per logical file group (per subsystem or per plan-step slice, code separate from docs) two concurrent subagents — one Fable, one Opus — each invoke the /session-audit skill on identical inputs. /session-audit reports findings only; main session dedupes both sets, then dispatches new Opus subagents to validate then fix accepted findings (structural issues → step 12). After fixes land: a Sonnet subagent runs the /compile selective build on fixed .cpp files, and one Opus subagent re-reviews only the fixed regions against the accepted findings — one pass, no second cycle; anything still open routes to step 12
12. For problems and residuals from any step that weren't auto-fixed (architectural decisions, larger issues out-of-scope of the current plan), have an Opus subagent create plan files in `Documents/Plans/`
13. Invoke /finalize-changes.

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out Opus subagents to validate each, compare pros/cons, then pick simplest good solution.
- **Architectural decisions** (new system shape, public API, data layout, threading model): stop and ask the user. Concisely present: (a) the problem, (b) proposed solutions, (c) pros and cons of each.

## Directives
- Follow KISS, YAGNI, DRY — before writing logic that may already exist, grep; call or extract a shared helper, never paste a copy. Exception: mirrored patterns (client/server pairs, per-collection boilerplate) stay parallel
- Before step 13, use only read-only Git commands outside an existing session worktree. Never push.
- Claude Skills `.claude\skills` and AGENTS.md files are used only by AI agents and must be kept CONCISE
- Each directory's agent memory lives in `AGENTS.md`; its sibling `CLAUDE.md` is a one-line `@AGENTS.md` import stub for Claude Code loading — edit `AGENTS.md`, never the stub
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code or recovering gracefully. Resolution ladder: repo-code-review skill §2c.
- Do not add unit tests
- **Don't touch unrelated code**: only modify files, functions, and lines directly tied to the current task. Do not refactor, rename, reformat, or restyle adjacent code. Surface incidental findings — mention trivial observations in chat; for bugs or other important issues, route through C++ Code Change Process step 12 (a follow-up plan in `Documents/Plans/`). Remove only imports/usings/variables that *your* edits made unused.
- **Response style**: Stay concise — no pleasantries, hedging, or restating the request. Terse ≠ incomplete: cut the words around the facts, never the facts — when the user (or output style) asks for explanation, provide it fully. Never compress errors, irreversible-action confirmations, or order-sensitive step sequences. In code and commit messages: drop articles where natural, fragments fine, technical terms unchanged. Pattern: [thing] [action] [reason]

## Directory Structure
- `/Common/` - Shared utilities (`common::` namespace); `Common.h` is the single aggregation header (included by `Pch.h`) - [AGENTS.md](Common/AGENTS.md)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - [AGENTS.md](DataPacker/Source/AGENTS.md)
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, save the few deliberate exceptions noted at the subsystem hubs - [AGENTS.md](Engine/Source/AGENTS.md)
- `/Projects/` - Game implementations (`game::` namespace) - [AGENTS.md](Projects/BrokenEngineSandbox/Source/AGENTS.md)
- `/ThirdParty/` - External libraries (do not modify) - [AGENTS.md](ThirdParty/AGENTS.md)
- `/Documents/` - Style guide (`C++StyleGuide.txt`), Mermaid architecture diagrams (`Architecture/`), and the plan queues (`Plans/`, `Features/`) - [AGENTS.md](Documents/AGENTS.md)

## Static Analysis
- `.editorconfig` (repo root) — formatting (Allman, tabs, spacing, include sort). VS applies on save / Ctrl+K, Ctrl+D.
- `.clang-tidy` (repo root) — enforced checks mapped to `Documents/C++StyleGuide.txt`. Enablement/exclusion mechanics: [VisualStudio2026/AGENTS.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md); deferred checks and details: comment block at the top of the file.

## Client/Server Targets

Build commands and details: `/compile` skill.

Same source, two executables:
- **client** (graphics, audio, input) — vcxproj defines `BT_CLIENT`
- **server** (headless physics) — vcxproj defines `BT_SERVER`

Rules:
- Gate client-only code with `#ifdef BT_CLIENT` at the narrowest practical scope. Single-build source files (whole file compiles only on one side) must carry a whole-file `#if defined(BT_CLIENT)`/`BT_SERVER` wrap — headers keep `#pragma once` outside the guard — in addition to matching vcxproj membership; the `Engine.h` `BT_CLIENT`/`BT_SERVER` spans remain as include grouping only, not the affinity mechanism. Exception: a file included unconditionally from both builds (e.g. via a shared PCH) but consumed only client/server-side stays unwrapped and is documented at its leaf as a deliberate exception.
- Files fully wrapped in `#if defined(BT_CLIENT)` must only appear in the client vcxproj; same for `BT_SERVER`. See [VisualStudio2026/AGENTS.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md).
- Collections with client-only fields use the `SharedMembers()`/`ClientMembers()` pattern — see [Collections/AGENTS.md](Engine/Source/Frame/Collections/AGENTS.md)

## Agent Interaction Harness

Drive both executables from an agent for verification. Launch args: `--agent-port N`, `--log-file path`, `--windowed WxH`. `AgentCli.exe` sends length-prefixed JSON to 127.0.0.1 (server 27100, client 27101 by convention). Command families:
- lifecycle/logs: `ping`, `quit`, `get_logs`, `set_log_level`
- server sim control: `status`, `pause`, `timescale`, `reset`
- save/load/replay: `save`, `load`, `replay_record`, `replay_play`
- server queries + StatusChange injection: `query_frame`, `query_players`, `query_collection`, `inject_status_changes`, `spawn_players`
- client capture + window: `screenshot`, `dump_render_target`, `resize`, `fullscreen`, `window_state`
- UI drive: `describe_ui`, `click`, `hover`, `set_slider`, `key`, `mouse`
- scene: `describe_scene`

Full usage (JSON schemas, workflows, caveats) in the agent-harness skill.

## Key Patterns
- **Log levels**: `kVerbose` — per-frame / high-frequency. `kDebug` — one-time (startup, connect). `kInfo` — state transitions, important one-shots (default threshold). `kWarning` — investigate (timeouts, desync); may spam. `kError` — failures; always logged. Per-category thresholds are runtime-adjustable (default `kInfo`, via the agent `set_log_level` command) atop a per-project compile-time floor that eliminates below-floor calls; mechanics in [Common/AGENTS.md](Common/AGENTS.md) Logging.
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
	- **XMVECTOR W invariant**: Positions W=1.0; directions / velocities / normals / offsets W=0.0; color alpha defaults 1.0 (opaque)
	- **Function form, not operators**: `XMVectorAdd`/`Subtract`/`Multiply`/`Divide`/`Scale`/`Negate` — never `vec + vec`, `f * vec`, `-vec`.
- **Base classes**: Include/use game versions, not Base versions — `Camera.h` not `CameraBase.h`, `game::gpGame` not `GameBase` directly
- **Engine reading game-layer state is by design** — the engine may assume **any** `game::` type or symbol exists, and Game must implement whatever the engine assumes; do not file plans to "inject" or "decouple" these. The real violation is the reverse: engine *types* naming game concepts. Details: [Engine/Source/AGENTS.md](Engine/Source/AGENTS.md)
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`. See [Common/AGENTS.md](Common/AGENTS.md)
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See [Memory/AGENTS.md](Engine/Source/Memory/AGENTS.md)
- **LOG formatting**: in allocation-tracked builds only (Game and Engine are; offline DataPacker is not), never use allocating format specs (`{:.Nf}`, `{:e}`, width/precision like `{:>10}`) or the `std::format` family in `LOG(...)` — they trip the allocation tracker. Wrap floats with `common::Wb(value, precision)`, `XMVECTOR`s with `common::WbV2/V3/V4`; placeholder stays `{}`. Full rules: repo-code-review skill §2b.
- **Standard library / external headers**: new standard-library/third-party `#include`s go in `Common/ExternalHeaders.h` (gated by build defines), not in individual source files — rule and exception: [Common/AGENTS.md](Common/AGENTS.md)
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See [Common/AGENTS.md](Common/AGENTS.md)
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See [Common/AGENTS.md](Common/AGENTS.md)

## Diagnosis Discipline
- **Verify root cause before editing**: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- **If uncertain, say so** and re-investigate — never fabricate justifications when challenged.
- **Authority order when sources disagree** about intended behavior: explicit user statement > plan document (post-grill) > AGENTS.md/docs/comments > current code behavior. Never silently make one side match another — surface the contradiction as a residual (footer line) naming both sides and which was trusted.
- **Never remove working features as part of a 'fix'** without explicit confirmation.
