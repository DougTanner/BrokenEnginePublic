# Broken Engine

A client/server game engine using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. Client/server simulation is deterministic; rare user input favors CPU/GPU smoothness over round-trip latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- Visual Studio 2026, C++23, Vulkan 1.2, Windows 10+
- Agent shells: Claude Code - Git Bash; Codex CLI - PowerShell 7. Call `pwsh` explicitly for PowerShell 7 scripts.
- Scripts: PowerShell 7 by default; Python only where a Python-only runtime forces it — full rule in `/external-skill-creator`.

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager; subagents execute work to keep main context clean
- Change Workflow dispatches are user-requested by standing repository policy: even if the host tool normally waits for the user to ask before delegating, these dispatches do not wait for that — dispatch without a per-session request
- Subagents must not spawn subagents — enforced by `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH=1` and by `disallowedTools: Agent` in each role definition. Only main-session skills request delegation; a subagent needing delegated work returns the requirement to its caller instead of dispatching it
- Give subagents only the instructions and context their task needs; they return a concise, clearly defined response. Delegation inherits no conversation context by default — `fork_turns: "none"` on Codex, a fresh self-contained prompt on Claude — and every delegation carries the single task brief in `.agents/references/subagent-reporting.md`.
- Judge whether a worker is still running only from host status and explicit progress or partial handoffs. Review scope, interruption, and recovery: `.agents/references/subagent-reporting.md`
- Workers return concise inline handoffs to the manager, who routes them; return large evidence as an existing file or log path plus the selector needed to recover it, and create a file under `Temp/` only when the owning workflow requires one
- Return one inline acceptance table only when a landing gate applies (defined in the Change Workflow definitions below). Whether the worker is still running, interruption, and table format: `.agents/references/subagent-reporting.md`
- Isolated worktrees are required only for executable Plan selection, for creating, changing, or deleting a Plan claim, for shared build/bootstrap coordination, or for landing. Ordinary work uses the user-supplied checkout and preserves unrelated changes.
- Retained worktrees are removed only by the manual `/cleanup-worktrees` skill (removes wrapper worktrees 48+ hours old) or explicit user direction — never recreate its effect with raw Git or filesystem commands.

### Delegation roles

This table is the authoritative spawned-agent routing policy; role definitions, Codex TOMLs, and the headless script enforce it. Skills name a role and describe the work. Definitions: `.claude/agents/<role>.md`. Codex resolves a role through the Model column — `.codex/agents/` is model-named.

| `subagent_type` | Model | Effort | Work |
| --- | --- | --- | --- |
| `planner` | Fable | medium | Plans, design, approach options |
| `reviewer` | Sol (see below); Opus fallback only with explicit user authorization | medium | Every review and audit; adversarial review that tries to disprove the change |
| `implementer` | Opus | medium | Preparation, implementation, propagation, docs, plans, harness, finalization |
| `researcher` | Opus | medium | Research requiring judgment |
| `locator` | Sonnet | xhigh | Exploration, search, log filtering, spec fetch, claim verification — returns file:line, quotes, or links, never summaries |
| `builder` | Sonnet | xhigh | `/compile`, which owns the return contract; returns each build's `broken-engine-build-result/v1` envelope plus decisive errors and warnings verbatim |
| `mechanic` | Sonnet | xhigh | Checklist edits — `/code-style-review`, `/update-vcxproj` |

- Delegate by `subagent_type`; an ad-hoc `model:` cannot lock in effort. A documented host-unavailability fallback to `general-purpose` may pass `model:` and runs without a locked-in effort
- Host built-in agent types (`Explore`, `Plan`, `general-purpose`) never substitute for a role, including inside plan mode — route the work through the table above. The documented host-unavailability fallback is the only exception
- Host plan mode never substitutes for Change Workflow steps: a plan produced there still gets Step 2's `/plan-audit` (and the Tier-3 additions) before implementation
- Every delegated review or audit is the `reviewer` role. In Claude Code it dispatches through `/codex-review` (Codex/Sol) — calling that skill is itself the delegated reviewer running, so no separate reviewer dispatch is needed, and that skill owns routing and fallbacks. Manually triggered parent/manager orchestrators such as `/next-plan-review` remain in the invoking parent and dispatch their own reviewer. `codex-review` is the sole skill that may name a model. Manager decides which Sol findings to accept, based on whether the failure is real and reachable and whether it matters; rejecting speculative findings or findings that ask for unnecessary extra work is the default, with no added review rounds.

ChatGPT Codex: Fable -> gpt-5.6-sol max; Sol -> gpt-5.6-sol medium; Opus and Sonnet -> gpt-5.6-luna max.

## IMPORTANT: Change Workflow (YOU MUST follow this when changing anything tracked in this repository)

This workflow governs every tracked artifact — C++, shaders, PowerShell and other scripts, skills, plans, and documentation. An artifact type a step does not name is a case this workflow does not assign to anyone: resolve it with the user, never by treating the step as inapplicable.

The user's request is implementation authority for Tier 1 and Tier 2 changes; agents classify the work, make the smallest complete change, run checks matching the size of the change, and report changed files, decisive checks, and residuals. Do not require a user approval round-trip, wrapper session, or report file unless a landing gate applies.

Definitions:

- Execution card — pre-implementation record of goal, out-of-scope boundary, tier trigger, affected interfaces/invariants, acceptance checks, and roles.
- Landing gate — the `/verify-changes` acceptance review plus the `/finalize-changes` landing flow with one explicit user confirmation; applies whenever primary will be changed: landing a session's work, a separately requested primary commit, or executable Plan completion or rejection. Shared AgentTools promotion and Tier-3 integration always land through it.
- Executable Plan — tracked `Documents/Plans/**/*.md` with byte-zero `broken-engine-plan/v1` metadata; selection and marker rules: `Documents/Plans/AGENTS.md`. `Documents/Features` is manual.
- Wrapper session — session started through `.claude/claude-worktree.sh` or `.codex/codex-worktree.ps1`, owning an isolated worktree. A retained wrapper session reattaches only through the same wrapper with its explicit reattach worktree input — `--reattach-worktree <path>` for Claude, `-ReattachWorktree <path>` for Codex; never adopt an arbitrary worktree.
- Primary — the shared main checkout and its main branch that finished session work lands into.
- Tracked artifact — any file Git tracks in this repository: code, shaders, scripts, skills, plans, and documentation.
- Step and stage — a step is one of the eight numbered Change Workflow steps below; a stage is one approved unit of session work that can complete or land independently.
- Handoff — the short structured result a subagent returns to its manager.
- Residual — a known leftover problem reported at the end of a task instead of fixed inside it.

### Risk tiers

Classify the whole change at the highest applicable tier before implementing:

- Tier 1 — mechanical: documentation, style, project membership, or local behavior-preserving work with no public signature or invariant exposure.
- Tier 2 — scoped behavior: one subsystem's runtime or tool behavior, excluding determinism/CRC, wire/protocol, serialization or data layout, save/replay compatibility, threading, trust boundaries, build/bootstrap coordination, and independently owned cross-subsystem integration.
- Tier 3 — invariant/integration: any surface excluded from Tier 2 above, build/bootstrap coordination that can block other sessions, or a change spanning independently owned subsystems.

A reviewer may escalate the tier when the changed bytes expose a higher-risk surface. Resolve meaningful ambiguity with the user before proceeding.

### Steps

1. Approve and classify. From user intent — plus an `implementer`'s repository preparation whenever the work is Tier 2+ or needs repository evidence to classify — main locks in the objective, approved stage decisions, tier and triggers, roles, acceptance checks, and execution card. Tier 1 and Tier 2 authorize implementation; Tier 3, Plan claim, and landing also require an execution card.
2. Plan review. Tier 2+ starts from an `implementer`-prepared plan; Tier 1 skips this. Main dispatches a `reviewer` for `/plan-audit` and reports a blocker if that role is unavailable. For Tier 3, an `implementer` owns `/external-grill-plan` repository evidence and short written decision summaries, updated round by round; a `locator` resolves external-claim requests; main only decides and interviews from those handoffs, then presents the resolved plan for approval. Brief and iteration contract: `.agents/skills/next-plan/references/tier3-workflow.md`. Plans may include optional `/agent-harness` verification.
3. Implement and propagate. Main splits the work into disjoint slices where possible and dispatches one `implementer` each in parallel, each making the smallest complete assigned change and returning notes on which other code sites the change may affect; then an `implementer` runs `/update-affected-code` after any C++ or GLSL change. Review-fix exceptions belong to `/resolve-findings`.
4. Run targeted pre-review checks. `Implementer`s run applicable static checks. Main routes every `Build required` handoff to a `builder` before covered work advances; full builds and runtime or harness scenarios remain acceptance-table work.
5. Review and resolve correctness. Main dispatches exactly one fresh `reviewer` per changed artifact type, scoped to changed bytes and the documented rules the change actually touches: C++ → `/repo-code-review`; shaders → `/glsl-review`; Tier-1 non-C++ → direct coherence; other Tier-2+ artifacts → a coherence review by a reviewer with no prior involvement in the change, who also fixes and self-verifies sub-semantic issues (meaning-preserving wording, formatting) in that pass when the way that reviewer was dispatched permits edits; whatever it cannot fix routes with the semantic findings. For Tier 2+, main also dispatches one fresh `reviewer` for `/scope-review` over the whole change diff — once per review round, never per artifact type — in parallel with those correctness reviews, which do not cover scope authorization or unnecessary extra work. Main decides once and routes accepted fixes to a separate `implementer`; only affected regions are re-reviewed and retested, and a second round requires a reproducible blocker. Tier 3 adds `/adversarial-review`; any tier may use it for one concrete unresolved reachable hypothesis.
6. Apply the cleanup steps this change triggers: a `mechanic` runs `/code-style-review` for changed C++ and `/update-vcxproj` for changes to file membership or to which executable a whole file belongs to; a fresh `reviewer` runs `/validate-skill` for changed `.agents/skills/*/SKILL.md`; an `implementer` runs `/update-claude-docs` after C++ or GLSL changes. Documentation inspects affected AGENTS.md scope but may need no edit.
7. Verify the acceptance table. Main dispatches a fresh read-only `reviewer` to map every approved criterion and invariant to evidence that settles the question on its own, including each duplicate check's independent signal; a stage landing in the same session satisfies this through Step 8's landing pass, so this step's reviewer applies only to a stage completing without landing. Tier 1 uses static, schema, link, validator, and changed-C++ compilation checks; Tier 2 adds the smallest observable scenario; Tier 3 adds exposed invariant or integration checks. Passing completes only the current stage. An `implementer` routes proven out-of-scope leftovers through `/create-follow-up-plans`.
8. Verify and land. At a landing gate, `/finalize-changes` prepares final Plan state when a claimed Plan completes, squashes the session work into one commit, and rebases it onto the current primary tip; main then dispatches one fresh read-only `/verify-changes` reviewer on the final prepared diff to map every approved criterion, invariant, and required check to evidence that settles the question on its own in that reviewed diff, and presents the landing summary. Exactly one explicit user confirmation authorizes changing primary. After confirmation the finalizer takes the landing lock — a lease, so it expires on its own if the holder dies — advances primary by compare-and-swap with rollback on failure, releases the lock, and deletes the machine-local claim. A meaningful diff change after review or confirmation — a conflict resolution, changed session bytes, or changed semantics — re-runs review of the affected regions and re-asks the confirmation; a clean identical rebase does not. Foreign primary movement that leaves the session patch byte-identical triggers focused re-review and rebuild only of regions reachable from the session's changed code, without re-asking an already-given confirmation; movement with no such reachability obligates nothing. `/session-audit` runs only on explicit user request. WorktreeCli alone parses or changes scheduler state. The confirmation contract lives in `/finalize-changes`; acceptance review belongs to `/verify-changes`. Landing completes only a repository stage; the session ends only when every stage is complete or explicitly deferred, with a tracked follow-up Plan where required.

For a claimed executable Plan, preparation that proves the work Tier 1, decision-complete, and current may continue straight into implementation without an approval pause; everything else presents for approval first. The landing confirmation always applies. `/next-plan` owns selection and claim lifecycle.

### Convergence

Once a stage's required checks pass, stop changing it: advance to the next stage, approval gate, blocker, or session end without adding untriggered tests, reviews, or process steps. This never shortens a valid build, lock, or harness deadline, and never licenses skipping a step this workflow does route — an unclear trigger is an ambiguity to surface, not grounds to call a check untriggered.

## Directives

- Minimum sufficient change: request and approved plan are target and ceiling — smallest complete change satisfying acceptance criteria and invariants; no speculative features, abstractions, configuration, extension points, or cleanup. Update related sites only when omission would make them incorrect; ignore polish.
- KISS, YAGNI, DRY: reuse existing mechanisms. Extract helpers only for current duplication, never for hypothetical use. Mirrored patterns stay parallel.
- Add backward compatibility only after explicit user consent. Without it, keep one current format, path, or behavior and remove obsolete compatibility code.
- Error handling at trust boundaries only: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- No useless ASSERTs: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code, or recovering gracefully. `/repo-code-review` lists the preferred fixes in order, from best to last resort.
- Comment the non-obvious, not the mechanism: never explain a language feature or house pattern the declaration already shows. Comment what the code cannot say — an invariant, a required ordering, a consequence. Review: `/code-style-review`.
- One term per concept: use the established repository term; define a genuinely new term once at its owning doc and reference it elsewhere; prefer plain words over formal ones.
- Do not add unit tests
- Bundled scripts as documented: run a repository script exactly as its skill documents it — never wrap, reimplement, or work around one. A script that cannot be run as documented is a bug: stop and report it to the parent session or user.

### User Interaction

- IMPORTANT: Every question or decision request aimed at the user must be answerable from the current message without hidden reasoning or remembered scrollback. Provide the FULL context needed to understand as rendered message text the user is guaranteed to see — text emitted before a question-tool call may never be displayed, so present first and ask only after the context is visible — and explain what the answer changes or blocks. When relevant, give options, trade-offs, and a recommendation. The user has NOT read the source code or plan file.
- AVOID jargon, the user is NOT a domain expert, use plain language (dumb it down).
- Explain fully when asked; use headings and bullet points so a longer explanation stays skimmable.

### Resolving Ambiguity

- Trivial choices (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- Non-trivial ties (two viable approaches, neither architectural): fan out `researcher` subagents to validate each, compare pros/cons, then pick the simplest good solution.
- Architectural decisions (new system shape, public API, data layout, threading model): stop and ask the user, presenting the problem, proposed solutions, and pros/cons of each.

### Diagnosis Discipline

- Verify root cause before editing: confirm it from close code inspection or evidence, never from "I know what the bug is". `/external-diagnose-bug` owns the method.
- Authority order when sources disagree about intended behavior: explicit user statement > final approved plan plus changes the user approved after the plan > AGENTS.md/docs/comments > current code behavior. Never silently make one side match another — surface the contradiction as a residual (footer line) naming both sides and which was trusted.

## Directory Structure

- `/Common/` — shared utilities (`common::`); `Common.h` is the single aggregation header (included by `Pch.h`)
- `/DataPacker/` — asset preprocessor producing `.pack`/`.manifest` files
- `/Engine/` — runtime: graphics, audio, input, frame state (`engine::`); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, except for the few deliberate exceptions noted at the subsystem hubs
- `/Projects/` — game implementations (`game::`)
- `/Tools/AgentHarness/` — loopback client/server command transport
- `/Tools/ToolCommon/` — shared Windows and coordination support compiled into both tools
- `/Tools/WorktreeCli/` — repository build, landing, and plan coordination
- `/ThirdParty/` — external libraries (do not modify)
- `/Documents/` — style guide (`C++StyleGuide.txt`), architecture diagrams and network protocol (`Architecture/`), executable plans (`Plans/`), manual feature plans (`Features/`)

## Static Analysis

- `.editorconfig` — formatting: Allman, tabs, spacing, include sort.
- `.clang-tidy` — checks mapped to `Documents/C++StyleGuide.txt`; `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` owns enablement and exclusions.

## Client/Server Targets

Same source, two executables: client (graphics, audio, input) defines `BT_CLIENT`; server (headless physics) defines `BT_SERVER`. Guard client/server-only code at the narrowest practical scope; which executable a whole file belongs to must match its project membership. `/update-vcxproj` and `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` own exact membership, filter, and exception rules. Build commands: `/compile`.

## Key Patterns

- Live verification: invoke `/agent-harness` for all harness operations — launching and driving the client/server, sim setup, UI input, state queries, screenshots, logs, replay determinism checks.
- Log levels: `kVerbose` — per-frame / high-frequency. `kDebug` — one-time (startup, connect). `kInfo` — state transitions, important one-shots (default threshold). `kWarning` — investigate (timeouts, desync); may spam. `kError` — failures; always logged. Runtime-threshold and compile-floor mechanics: `Common/Log/AGENTS.md`.
- Managers: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- DirectX Math: Prefer aligned versions (`Float4A` not `Float4`)
	- XMVECTOR W invariant: Positions W=1.0; directions / velocities / normals / offsets W=0.0; color alpha defaults 1.0 (opaque)
	- Function form, not operators: `XMVectorAdd`/`Subtract`/`Multiply`/`Divide`/`Scale`/`Negate` — never `vec + vec`, `f * vec`, `-vec`.
- Base classes: Include/use game versions, not Base versions — `Camera.h` not `CameraBase.h`, `game::gpGame` not `GameBase` directly
- Workbuffer: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`.
- Allocation tracking: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See `Engine/Source/Memory/AGENTS.md`
- LOG formatting: logging in allocation-tracked Game/Engine code must remain allocation-free; /repo-code-review owns accepted formatting and wrapper details
- Standard library / external headers: PCH-backed `#include`s go in `Common/ExternalHeaders.h`; PCH-less AgentTools use `Tools/ToolCommon/ToolCliCommon.h`. Rules and exceptions: `Common/AGENTS.md` and `Tools/ToolCommon/AGENTS.md`
- Flags over booleans: Use `common::Flags<EnumType>` instead of multiple `bool` variables.
- Multithreading: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work.
