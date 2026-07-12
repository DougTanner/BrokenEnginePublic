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
- Before repository mutation, invoke /prepare-session-worktree. One top-level session owns one worktree and fixed start commit for its full plan lifecycle; every subagent shares that checkout and baseline

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
- Code/session review: paired Fable + Opus (step 4: /repo-code-review + /adversarial-review; step 10: two /session-audit); step-10 fix re-review: Opus

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)

Exception for one-line changes: Make the edit yourself, then do step 3
Main session accumulates every skill's residuals and handoffs, passes them to later steps, and routes unresolved work through step 11; never drop them silently

0. The user will use plan mode to create a planning document (or load a plan from a file)
	- Plans MAY include a Verification section of agent-harness steps (launch, drive, query, screenshot — see the agent-harness skill); optional, so trivial refactors don't gold-plate
1. Have one Fable subagent invoke /plan-audit on the plan file. The audit is findings-only; the main session validates its findings, then invokes /external-grill-plan with the accepted findings. **When the user has responded to the grill, DO NOT stop or summarize — immediately continue to step 2 in the same turn.**
2. Have Opus subagents invoke /implement-plan, splitting large plans across disjoint file sets. Pass its sweep handoffs to step 3 and review focus areas to steps 4 and 10
3. Have a Sonnet subagent invoke /update-affected-code with the plan and accumulated step 2 reports
4. Review and resolve changes:
	- **a)** Run concurrent /repo-code-review (Fable) and /adversarial-review (Opus) subagents on identical inputs; add /glsl-review (Opus) when shaders changed
	- **b)** Main session dedupes and adjudicates findings by evidence; delegate external claims to /verify-external-claims (Sonnet) and query the user when authoritative verification remains unresolved
	- **c)** Send accepted non-structural in-scope fixes to /resolve-findings (Opus); route structural findings through step 11
5. Use a Sonnet subagent to invoke the /code-style-review skill
6. Use an Opus subagent to invoke the /update-claude-docs skill
7. Have a Sonnet subagent invoke /update-vcxproj on every file changed this session
8. Have a Sonnet subagent invoke /compile; send failures to an Opus /resolve-findings subagent
9. For runtime-observable changes, have an Opus subagent invoke /agent-harness; send failures to an Opus /resolve-findings subagent. Skip only changes with no runtime surface
10. Audit the completed change per logical file group:
	- **a)** Run two concurrent /session-audit subagents (Fable + Opus) on identical inputs
	- **b)** Main session dedupes findings; send accepted non-structural in-scope fixes to /resolve-findings (Opus) and route structural findings through step 11
	- **c)** Run /compile (Sonnet) on fixed `.cpp` files, then one /resolve-findings verification pass (Opus) on fixed regions; do not cycle again
11. Have an Opus subagent invoke /create-follow-up-plans for unresolved structural or out-of-scope residuals
12. When working in a session worktree, invoke /finalize-changes for every repository mutation, including documentation, plans, skills, configuration, and other non-C++ changes.

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out Opus subagents to validate each, compare pros/cons, then pick simplest good solution.
- **Architectural decisions** (new system shape, public API, data layout, threading model): stop and ask the user. Concisely present: (a) the problem, (b) proposed solutions, (c) pros and cons of each.

## Directives

- Follow KISS, YAGNI, DRY — before writing logic that may already exist, grep; call or extract a shared helper, never paste a copy. Exception: mirrored patterns (client/server pairs, per-collection boilerplate) stay parallel
- Before step 12, do not mutate Git state outside the adopted session worktree; /finalize-changes owns commit, rebase, landing, locking, and cleanup. Never push
- Agent-memory changes invoke /update-claude-docs; edit `AGENTS.md`, never its `CLAUDE.md` import stub
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code or recovering gracefully. Resolution ladder: repo-code-review skill §2c.
- Do not add unit tests
- **Don't touch unrelated code**: change only the requested task scope; route important incidental findings through step 11
- **Response style**: Stay concise — no pleasantries, hedging, or restating the request. Terse ≠ incomplete: cut the words around the facts, never the facts — when the user (or output style) asks for explanation, provide it fully. Never compress errors, irreversible-action confirmations, or order-sensitive step sequences. In code and commit messages: drop articles where natural, fragments fine, technical terms unchanged. Pattern: [thing] [action] [reason]

## Directory Structure

- `/Common/` - Shared utilities (`common::` namespace); `Common.h` is the single aggregation header (included by `Pch.h`) - [AGENTS.md](Common/AGENTS.md)
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - [AGENTS.md](DataPacker/Source/AGENTS.md)
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, save the few deliberate exceptions noted at the subsystem hubs - [AGENTS.md](Engine/Source/AGENTS.md)
- `/Projects/` - Game implementations (`game::` namespace) - [AGENTS.md](Projects/BrokenEngineSandbox/Source/AGENTS.md)
- `/Tools/AgentCli/` - Standalone harness client and local workflow coordinator - [AGENTS.md](Tools/AgentCli/AGENTS.md)
- `/ThirdParty/` - External libraries (do not modify) - [AGENTS.md](ThirdParty/AGENTS.md)
- `/Documents/` - Style guide (`C++StyleGuide.txt`), Mermaid architecture diagrams and network protocol reference (`Architecture/`), and the plan queues (`Plans/`, `Features/`) - [AGENTS.md](Documents/AGENTS.md)

## Static Analysis

- `.editorconfig` (repo root) — formatting (Allman, tabs, spacing, include sort). VS applies on save / Ctrl+K, Ctrl+D.
- `.clang-tidy` (repo root) — enforced checks mapped to `Documents/C++StyleGuide.txt`; [VisualStudio2026/AGENTS.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md) owns project enablement and exclusions

## Client/Server Targets

Build commands and details: `/compile` skill.

Same source, two executables:
- **client** (graphics, audio, input) — vcxproj defines `BT_CLIENT`
- **server** (headless physics) — vcxproj defines `BT_SERVER`

Rules:
- Guard client/server-only code at the narrowest practical scope; whole-file affinity must match project membership. /update-vcxproj and [VisualStudio2026/AGENTS.md](Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md) own exact membership, filter, and exception rules

## Agent Interaction Harness

The agent interaction harness launches and controls the client/server for live verification, including simulation setup, UI input, state queries, screenshots, logs, and replay determinism checks. Invoke `/agent-harness` for all harness operations; the skill owns launch details, command schemas, workflows, evidence reporting, and caveats.

## Key Patterns

- **Log levels**: `kVerbose` — per-frame / high-frequency. `kDebug` — one-time (startup, connect). `kInfo` — state transitions, important one-shots (default threshold). `kWarning` — investigate (timeouts, desync); may spam. `kError` — failures; always logged. Per-category thresholds are runtime-adjustable (default `kInfo`, via the agent `set_log_level` command) atop a per-project compile-time floor that eliminates below-floor calls; mechanics in [Common/AGENTS.md](Common/AGENTS.md) Logging.
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
	- **XMVECTOR W invariant**: Positions W=1.0; directions / velocities / normals / offsets W=0.0; color alpha defaults 1.0 (opaque)
	- **Function form, not operators**: `XMVectorAdd`/`Subtract`/`Multiply`/`Divide`/`Scale`/`Negate` — never `vec + vec`, `f * vec`, `-vec`.
- **Base classes**: Include/use game versions, not Base versions — `Camera.h` not `CameraBase.h`, `game::gpGame` not `GameBase` directly
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`. See [Common/AGENTS.md](Common/AGENTS.md)
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See [Memory/AGENTS.md](Engine/Source/Memory/AGENTS.md)
- **LOG formatting**: logging in allocation-tracked Game/Engine code must remain allocation-free; /repo-code-review owns accepted formatting and wrapper details
- **Standard library / external headers**: new standard-library/third-party `#include`s go in `Common/ExternalHeaders.h` (gated by build defines), not in individual source files — rule and exception: [Common/AGENTS.md](Common/AGENTS.md)
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See [Common/AGENTS.md](Common/AGENTS.md)
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See [Common/AGENTS.md](Common/AGENTS.md)

## Diagnosis Discipline

- **Verify root cause before editing**: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- **If uncertain, say so** and re-investigate — never fabricate justifications when challenged.
- **Authority order when sources disagree** about intended behavior: explicit user statement > plan document (post-grill) > AGENTS.md/docs/comments > current code behavior. Never silently make one side match another — surface the contradiction as a residual (footer line) naming both sides and which was trusted.
