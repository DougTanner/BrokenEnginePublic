# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. Client/server simulation is deterministic; rare user input favors CPU/GPU smoothness over round-trip latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- Visual Studio 2026, C++23, Vulkan 1.2, Windows 10+
- Agent shells: Claude Code runs in Git Bash; Codex CLI runs in PowerShell 7. Claude calls `pwsh` explicitly for PowerShell 7 scripts.
- Fresh-machine bootstrap: `Documents/FreshMachineSetup.md`

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager; subagents execute work to keep main context clean
- Change Workflow dispatches are user-requested by standing repository policy: a harness default withholding subagent dispatch until the user asks does not gate them — dispatch without a per-session request
- Subagents must not spawn subagents — enforced by `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH=1` and by `disallowedTools: Agent` in each role definition. Only main-session skills request delegation; a subagent needing delegated work returns the requirement to its caller instead of dispatching it
- Give subagents only the instructions and context their task needs; they return a concise, clearly defined response. Delegation inherits no conversation context by default — `fork_turns: "none"` on Codex, a fresh self-contained prompt on Claude — and every core delegation prompt carries the delegation-basis and context records. Exact record form and the bounded brief fields: `.agents/references/subagent-reporting.md`
- Review delegations enumerate the exact files/regions in scope; an interrupted or re-scoped reviewer returns findings gathered so far immediately. Judge liveness by transcript/status evidence, never elapsed time alone
- Workers return concise inline handoffs to the manager, who routes them; return large evidence as an existing artifact or log path plus the selector needed to recover it, and create a `Temp/` artifact only when the owning workflow requires one
- Return one inline acceptance table only when a final-evidence gate applies. Liveness, interruption, and table format: `.agents/references/subagent-reporting.md`
- Isolated worktrees are required only for executable Plan selection or claim mutation, shared build/bootstrap coordination, or landing. Ordinary work uses the user-supplied checkout and preserves unrelated changes.
- Retained worktrees are removed only by the manual `/cleanup-worktrees` skill (removes wrapper worktrees 48+ hours old) or explicit user direction — never recreate its effect with raw Git or filesystem commands.

### Delegation roles

Skills name a role and describe the work. Definitions: `.claude/agents/<role>.md`. Codex resolves a role through the Model column — `.codex/agents/` is model-named.

| `subagent_type` | Model | Effort | Work |
| --- | --- | --- | --- |
| `planner` | Opus | high | Plans, designs, approach options |
| `reviewer` | Sol (see below) with Opus fallback | high | Every review and audit; adversarial falsification |
| `implementer` | Opus | high | Code, docs, propagation, fix waves, plan authoring, harness runs |
| `researcher` | Opus | high | Research requiring judgment |
| `locator` | Sonnet | xhigh | Search, log filtering, spec fetch, claim verification — returns file:line, quotes, or links, never summaries |
| `builder` | Sonnet | xhigh | `/compile`; returns status plus decisive errors and warnings verbatim |
| `mechanic` | Sonnet | xhigh | Checklist edits — `/code-style-review`, `/update-vcxproj` |

- Delegate by `subagent_type`; an ad-hoc `model:` cannot pin effort. A documented host-unavailability fallback to `general-purpose` may pass `model:` and runs unpinned
- Every review is the `reviewer` role without exception. In Claude Code it dispatches through `/codex-review` (Codex/Sol) — that invocation IS the delegated-reviewer execution context, and that skill owns routing and fallbacks. `codex-review` is the sole skill that may name a model. Manager adjudicates Sol findings for concrete reachable failure and materiality; rejecting speculative or gold-plating findings is the default, with no added review rounds.

ChatGPT Codex: Sol -> gpt-5.6-sol, Opus -> gpt-5.6-terra, Sonnet -> gpt-5.6-luna

## IMPORTANT: Change Workflow (YOU MUST follow this when changing anything tracked in this repository)

This workflow governs every tracked artifact — C++, shaders, PowerShell and other scripts, skills, plans, and documentation. An artifact type a step does not name is an unrouted case: resolve it with the user, never by treating the step as inapplicable.

The user's request is implementation authority for Tier 1 and Tier 2 changes; agents classify the work, make the smallest complete change, run proportionate checks, and report changed files, decisive checks, and residuals. Do not require a user approval round-trip, wrapper session, report artifact, final manifest, or landing workflow unless a final-evidence gate applies.

Definitions:

- Execution card — pre-implementation record of goal, out-of-scope boundary, tier trigger, affected interfaces/invariants, acceptance checks, and roles.
- Final-evidence gate — the `/verify-changes` acceptance table plus `/finalize-changes` path. Required for executable Plan claim mutation or completion, reconciliation, requested primary commit or landing, a wrapper session completing its work, shared build/bootstrap work, or Tier-3 integration; any `/next-plan` claim uses it regardless of tier.
- Reconciliation — `/finalize-changes` rebasing the verified session branch onto the current primary tip; a conflict-free rebase needs no re-verification. Mechanics, conflicts, and the non-blocking `missing-plan-file` notice: `/finalize-changes` and `.agents/skills/next-plan/references/execution-gates.md`.
- Executable Plan — tracked `Documents/Plans/**/*.md` with byte-zero `broken-engine-plan/v1` metadata; selection and marker rules: `Documents/Plans/AGENTS.md`. `Documents/Features` is manual.
- Wrapper session — session started through `.claude/claude-worktree.sh` or `.codex/codex-worktree.ps1`, owning an isolated worktree identified by its private-Git receipt; receipt and reattach rules: `.agents/references/wrapper-sessions.md`.

### Risk tiers

Classify the whole change at the highest applicable tier before implementing:

- Tier 1 — mechanical: documentation, style, project membership, or local behavior-preserving work with no public signature or invariant exposure.
- Tier 2 — scoped behavior: one subsystem's runtime or tool behavior, excluding determinism/CRC, wire/protocol, serialization or data layout, save/replay compatibility, threading, trust boundaries, build/bootstrap coordination, and independently owned cross-subsystem integration.
- Tier 3 — invariant/integration: any surface excluded from Tier 2 above, build/bootstrap coordination that can block other sessions, or a change spanning independently owned subsystems.

A reviewer may escalate the tier when the changed bytes expose a higher-risk surface. Resolve material ambiguity with the user before proceeding.

### Steps

1. Approve and classify. Pin the process baseline at session start: user objective, each approved stage and deliverable with its disposition, risk tier and triggers, roles, and one check per criterion or grounded invariant. Tier 1 and Tier 2 authorize implementation; Tier 3, Plan claim, reconciliation, and landing also get an execution card.
2. Plan review. Tier 2+ starts from a plan — load one or enter plan mode; that plan is what gets reviewed. Tier 1 skips this. Tier 2: a `reviewer` runs `/plan-audit`. Tier 3: `/plan-audit`, then main feeds accepted findings into `/external-grill-plan` (the interview needs a user-facing UI subagents lack). Plans may include a Verification section of `/agent-harness` steps — optional, so trivial refactors don't gold-plate.
3. Implement and propagate. Split into disjoint slices where possible, one `implementer` each in parallel; each makes the smallest complete assigned change and returns affected-site triggers. Main runs `/update-affected-code` for any code change (not docs, skills, or scripts), updating only required sites. Exception: in a review-fix wave a single-function fix with no signature or contract change self-scans, returning only candidates outside its scope for `/update-affected-code` (mechanics: `/resolve-findings`).
4. Run targeted pre-review checks. Smallest applicable static checks plus affected-target compilation, before review. A `Build required` line from any subagent executes here through `builder`, before the work it covers advances. Full builds and runtime or harness scenarios are acceptance-matrix decisions.
5. Review and resolve correctness. Exactly one fresh domain review per changed artifact type, none skipped; a change spanning two types runs both. Review effort scales with the change: a reviewer reads the diff and the contracts it reaches, not the enclosing file or subsystem — one review always happens, but a two-line fix earns a two-line review. C++ → `/repo-code-review`; shaders → `/glsl-review`; Tier-1 non-C++ → direct coherence; anything else at Tier 2+ (scripts, skills, plans, docs) → fresh-eyes coherence review by a `reviewer` against the changed bytes, who also fixes and self-verifies sub-semantic issues (meaning-preserving wording, formatting) in that pass; only semantic findings route through separate resolve/verify. Tier 3 adds `/adversarial-review`; any tier may run it for one concrete unresolved reachable hypothesis. Adjudicate evidence once; accepted fixes re-review and retest only affected regions, and a second wave needs a reproducible blocker.
6. Apply hygiene, each only when its trigger fires: `/code-style-review` (changed C++), `/validate-skill` (changed `.agents/skills/*/SKILL.md`), `/update-claude-docs` (any C++ or GLSL change), `/update-vcxproj` (added or removed files, or whole-file client/server affinity changes). The documentation pass always inspects the affected AGENTS.md scope but may make no edits when existing guidance remains correct.
7. Verify the acceptance matrix. Map every approved criterion and declared invariant to a decisive check, naming any duplicate's independent signal. Tier 1: static, schema, link, and validator checks, plus C++ compilation when C++ changed. Tier 2 adds the smallest observable scenario; Tier 3 adds only exposed invariant or integration checks. Passing completes the current stage only. Route out-of-scope leftovers through `/create-follow-up-plans`; no Plan claim is needed to write one.
8. Reconcile, audit when triggered, and finalize. When a final-evidence gate applies, produce the `/verify-changes` acceptance table. Run one `/session-audit` only for late semantic fixes, manual conflict resolutions or invalidated assumptions, Tier-3 cross-file integration, or contract-significant regions unseen by review. Plan work normally uses a registered session worktree; `/finalize-changes` owns commit, landing, and claim release. WorktreeCli is the sole metadata scheduler parser and claim mutator — validate before Plan selection or mutation, after a landing that changes Plan files, and after primary completion. A wrapper session lands by default: when every stage's checks pass, produce the table and proceed to `/finalize-changes` without waiting for a landing request; its landing confirmation is the user's land/defer/decline decision. Landing completes only a repository stage — continue the next stage or present its approval gate. The session is finished only when every stage is complete or the user explicitly deferred it, with a tracked follow-up Plan where required.

### Convergence

Once a stage's required checks pass, stop changing it: advance to the next stage, approval gate, blocker, or session end without adding untriggered tests, reviews, or process steps. This never shortens a valid build, lock, or harness deadline, and never licenses skipping a step this workflow does route — an unclear trigger is an ambiguity to surface, not grounds to call a check untriggered.

## Directives

- Minimum sufficient change: request and approved plan are target and ceiling — smallest complete change satisfying acceptance criteria and invariants; no speculative features, abstractions, configuration, extension points, or cleanup. Update related sites only when omission would make them incorrect; ignore polish.
- KISS, YAGNI, DRY: reuse existing mechanisms. Extract helpers only for current duplication, never for hypothetical use. Mirrored patterns stay parallel.
- Add backward compatibility only after explicit user consent. Without it, keep one current format, path, or behavior and remove obsolete compatibility code.
- Error handling at trust boundaries only: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- No useless ASSERTs: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code, or recovering gracefully. Resolution ladder: `/repo-code-review`.
- Comment the non-obvious, not the mechanism: never explain a language feature or house pattern the declaration already shows. Comment what the code cannot say — an invariant, a required ordering, a consequence. Review: `/code-style-review`.
- Do not add unit tests
- Response style: concise — no pleasantries, hedging, or restating the request. Cut the words around the facts, never the facts; explain fully when asked. Never compress errors, irreversible-action confirmations, or order-sensitive step sequences. In code and commit messages: drop articles where natural, fragments fine, technical terms unchanged. Pattern: [thing] [action] [reason]

## Resolving Ambiguity

- Trivial choices (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- Non-trivial ties (two viable approaches, neither architectural): fan out `researcher` subagents to validate each, compare pros/cons, then pick the simplest good solution.
- Architectural decisions (new system shape, public API, data layout, threading model): stop and ask the user, presenting the problem, proposed solutions, and pros/cons of each.

## Diagnosis Discipline

- Verify root cause before editing: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- If uncertain, say so and re-investigate — never fabricate justifications when challenged.
- Authority order when sources disagree about intended behavior: explicit user statement > final approved plan plus approved deltas > AGENTS.md/docs/comments > current code behavior. Never silently make one side match another — surface the contradiction as a residual (footer line) naming both sides and which was trusted.

## Directory Structure

- `/Common/` — shared utilities (`common::`); `Common.h` is the single aggregation header (included by `Pch.h`)
- `/DataPacker/` — asset preprocessor producing `.pack`/`.manifest` files
- `/Engine/` — runtime: graphics, audio, input, frame state (`engine::`); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, save the few deliberate exceptions noted at the subsystem hubs
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

Same source, two executables: client (graphics, audio, input) defines `BT_CLIENT`; server (headless physics) defines `BT_SERVER`. Guard client/server-only code at the narrowest practical scope; whole-file affinity must match project membership. `/update-vcxproj` and `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` own exact membership, filter, and exception rules. Build commands: `/compile`.

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
