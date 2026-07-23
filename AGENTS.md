# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. Client/server simulation is deterministic; rare user input favors CPU/GPU smoothness over round-trip latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+
- **Agent shells**: Claude Code runs in Git Bash; Codex CLI runs in PowerShell 7. Claude calls `pwsh` explicitly for PowerShell 7 scripts.
- **Fresh-machine bootstrap**: ordered setup sequence in [Documents/FreshMachineSetup.md](Documents/FreshMachineSetup.md)

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager; subagents execute work to keep main context clean
- Subagents cannot spawn subagents. Only main-session skills request delegation; a subagent needing delegated work returns the requirement to its caller instead of dispatching it
- Give subagents only the instructions and context their task needs; they return a concise, clearly defined response
- Delegation prompts for review roles enumerate the exact files/regions in scope; an interrupted or re-scoped reviewer returns findings gathered so far immediately
- A wait boundary or elapsed time alone never proves a delegate is stuck — judge liveness by transcript/status evidence: recent distinct tool activity, narrowing searches, new evidence, or in-progress synthesis is forward progress; a loop requires repeated equivalent operations or unchanged failures without narrowing or new evidence. Before replacing a running reviewer, request findings gathered so far; a justified replacement continues from them (mechanics: [.agents/references/subagent-reporting.md](.agents/references/subagent-reporting.md))
- Subagent-to-subagent handoffs go through temporary files; return only the file paths to the parent session
- Return one inline acceptance table only when a final-evidence gate (defined in the Change Workflow below) applies; format: [.agents/references/subagent-reporting.md](.agents/references/subagent-reporting.md)
- Isolated worktrees and session claims are required only for executable Plan selection or claim mutation, shared build/bootstrap coordination, or landing. Ordinary work uses the user-supplied checkout and preserves unrelated changes.
- Live sessions hold a `git worktree lock`; retained worktrees are removed only by the manual `/cleanup-worktrees` skill or explicit user direction — never recreate its effect with raw Git or filesystem commands.

### Delegation roles

The only place a role's model and effort are written down; skills name a role and describe the work. Definitions: `.claude/agents/<role>.md`. Codex resolves a role through the Model column — `.codex/agents/` is model-named.

| `subagent_type` | Model | Effort | Work |
| --- | --- | --- | --- |
| `planner` | Fable | medium | Plans, designs, approach options |
| `reviewer` | Fable | medium | Every review and audit; adversarial falsification |
| `implementer` | Opus | xhigh | Code, docs, propagation, fix waves, plan authoring, harness runs |
| `researcher` | Opus | xhigh | Research requiring judgment |
| `locator` | Sonnet | xhigh | Search, log filtering, spec fetch, claim verification — returns file:line, quotes, or links, never summaries |
| `builder` | Sonnet | xhigh | `/compile`; returns status plus decisive errors and warnings verbatim |
| `mechanic` | Sonnet | xhigh | Checklist edits — `/code-style-review`, `/update-vcxproj` |

- Delegate by `subagent_type`; an ad-hoc `model:` cannot pin effort. A documented host-unavailability fallback to `general-purpose` may pass `model:` and runs unpinned
- Every review is `reviewer` — no higher-capability exception. Locating is `locator`, judgment is `researcher`, style review is mechanical so `mechanic`
- `codex-review` is the sole skill that may name a model — it routes around one being unavailable

ChatGPT Codex: Fable -> gpt-5.6-sol, Opus -> gpt-5.6-terra, Sonnet -> gpt-5.6-luna
	Temp Note: Codex does not currently expose model names for subagents, so we are using gpt-5.6-sol high (which currently is picked up by all subagents)
	
Claude Code: If Fable is not available, fall back to Opus.

## IMPORTANT: Change Workflow (YOU MUST follow this when changing anything tracked in this repository)

This workflow governs every tracked artifact — C++, shaders, PowerShell and other scripts, skills, plans, and documentation. Artifact-specific routing lives inside the individual steps; a change is never outside this workflow merely because it is not C++. Where a step names artifact types, an artifact it does not name is an unrouted case: resolve it per the ambiguity rule below, never by treating the step as inapplicable.

The user's request is implementation authority for Tier 1 and Tier 2 changes; agents classify the work, make the smallest complete change, run proportionate checks, and report changed files, decisive checks, and residuals. Do not require a user approval round-trip, wrapper session, report artifact, final manifest, or landing workflow unless a final-evidence gate applies.

Definitions used throughout this workflow:

- **Execution card** — the short pre-implementation record of goal, out-of-scope boundary, tier trigger, affected interfaces/invariants, acceptance checks, and roles.
- **Final-evidence gate** — the `/verify-changes` acceptance table plus `/finalize-changes` path required for executable Plan claim mutation or completion, reconciliation, requested primary commit or landing, a wrapper session completing its work (step 8), shared build/bootstrap work, or Tier-3 integration. Any `/next-plan` claim uses it regardless of tier — "Tier 1 and Tier 2 authorize implementation" governs approval, not finalization.
- **Objective-terminal** — the session state where every recorded stage is complete or explicitly user-deferred with a tracked follow-up Plan when one is required.
- **Reconciliation** — `/finalize-changes` rebasing the verified session branch onto the current primary tip; a conflict-free rebase needs no re-verification, and conflicts route through the overlap check. A session-worktree validation finding caused solely by executable Plans newly removed from primary (their files exist at the wrapper baseline but not on primary) is the expected post-advance condition: WorktreeCli reports it as a non-blocking `missing-plan-file` notice, every `ok: true` gate accepts it, and reconciliation resolves it — it is never a blocker requiring user direction or a mid-workflow rebase.
- **Executable Plan** — a tracked `Documents/Plans/**/*.md` file with byte-zero `broken-engine-plan/v1` metadata. WorktreeCli deterministically selects it by immutable `createdUtc` and canonical path; only short-lived PC-local claim records live outside Git. `Documents/Features` is manual.
- **Wrapper session** — a session started through `.claude/claude-worktree.sh` or `.codex/codex-worktree.ps1`, owning an isolated worktree and a live WorktreeCli session claim.
	- Retained wrapper sessions reattach only through the same wrapper with its explicit reattach worktree input (`-ReattachWorktree <path>` for Codex; `--reattach-worktree <path>` for Claude). The wrapper reads its private-Git receipt and restores the original session owner; missing, altered, moved, or legacy receipts fail closed. Never reconstruct receipt provenance, adopt an arbitrary worktree, or bypass the wrapper ledger/lock sequence.

### Risk tiers

Before implementation, classify the whole change at the highest applicable tier:

- **Tier 1 — mechanical:** documentation, style, project membership, or local behavior-preserving work with no public signature or invariant exposure.
- **Tier 2 — scoped behavior:** one subsystem's runtime or tool behavior, excluding determinism/CRC, wire/protocol, serialization or data layout, save/replay compatibility, threading, trust boundaries, build/bootstrap coordination, and independently owned cross-subsystem integration.
- **Tier 3 — invariant/integration:** any surface excluded from Tier 2 above, build/bootstrap coordination that can block other sessions, or a change spanning independently owned subsystems.

A reviewer may escalate the tier when the changed bytes expose a higher-risk surface. Resolve material ambiguity with the user before proceeding.

### 1. Approve and classify

At session start, pin the process baseline. Record the complete user objective, every approved stage and deliverable with its disposition, risk tier/triggers, roles, and one check per criterion or grounded invariant. Tier 1 and Tier 2 authorize implementation; Tier 3, executable Plan claim, reconciliation, and landing work also gets a short execution card.

### 2. Plan review

Tier 2+ starts from a plan: load a plan file or enter plan mode to create one; that plan is what gets reviewed. Plans may include a Verification section of agent-harness steps (launch, drive, query, screenshot — see `/agent-harness`); optional, so trivial refactors don't gold-plate. Tier 1 skips plan review. Tier 2: a `reviewer` runs `/plan-audit`. Tier 3: a `reviewer` runs `/plan-audit`, then main feeds accepted findings into `/external-grill-plan` — the interview needs the user-facing UI subagents lack.

### 3. Implement and propagate

`implementer` subagents implement the smallest complete change and return affected-site triggers; main runs `/update-affected-code` for any code change (not documentation, skills, or scripts), updating only required affected sites. Exception: in a review-fix wave, a single-function fix with no signature or contract change self-scans affected sites, returning only candidates outside its assigned scope for `/update-affected-code` (mechanics: `/resolve-findings`).

### 4. Run targeted pre-review checks

Run the smallest applicable static checks and affected-target compilation before review. A `Build required` line returned by any subagent is executed here through `builder`, before the work it covers advances. Full builds and runtime or harness scenarios are acceptance-matrix decisions.

### 5. Review and resolve correctness

Run one fresh domain review — changed C++: `/repo-code-review`; changed shaders: `/glsl-review`; Tier-1 non-C++: direct coherence; any other changed artifact at Tier 2+ (scripts, skills, plans, documentation): fresh-eyes coherence review by a `reviewer` against the changed bytes — that reviewer fixes and self-verifies sub-semantic issues (meaning-preserving wording, formatting) in the same pass; only semantic findings route through separate resolve/verify steps. Every changed artifact type gets exactly one domain review — a change spanning two types runs each type's review — and no artifact type falls through this list unreviewed. Tier 3 adds `/adversarial-review`, which any tier may also run for one concrete unresolved reachable hypothesis. Adjudicate evidence once; accepted fixes re-review and retest only affected regions, and a second wave needs a reproducible blocker.

### 6. Apply hygiene

Run `/code-style-review` only for changed C++, `/validate-skill` only for a changed `.agents/skills/*/SKILL.md`, `/update-claude-docs` after every C++ or GLSL change, and `/update-vcxproj` only for added or removed files or whole-file client/server affinity changes. The documentation pass always inspects the affected AGENTS.md scope but may make no edits when the existing guidance remains correct.

### 7. Verify the acceptance matrix

Map every approved criterion and declared invariant to a decisive check, naming any duplicate's independent signal. Tier 1 uses static, schema, link, and validator checks plus C++ compilation when C++ changed; Tier 2 adds the smallest observable scenario; Tier 3 adds only exposed invariant or integration checks. A passing acceptance matrix completes only the current stage. Route out-of-scope leftovers from the skill executions — work that still needs doing — through `/create-follow-up-plans` when the session holds a live Plan claim; otherwise present the proposed follow-up plans to the user.

### 8. Reconcile, audit when triggered, and finalize

When a final-evidence gate applies, produce the `/verify-changes` acceptance table. Run one `/session-audit` only for late semantic fixes, manual conflict resolutions or invalidated assumptions, Tier-3 cross-file integration, or contract-significant regions unseen by review.

Executable Plan work normally uses a registered session worktree. `/finalize-changes` owns commit, landing, and claim release — including the explicitly user-authorized `primary-commit` route — and a session landing still needs final approval before advancing its parent. WorktreeCli is the sole metadata scheduler parser and claim mutator; validate before Plan selection or mutation, after a landing that changes Plan files, and after primary completion.

A wrapper session lands by default: once every stage's checks pass, produce the `/verify-changes` acceptance table and proceed to `/finalize-changes` without waiting for a landing request — its landing confirmation is the user's land/defer/decline decision.

Landing and receipt-bound claim release complete only a repository stage. Continue the next recorded stage in the same session or present its approval gate. The session is objective-terminal only when every stage is complete or each unfinished stage was explicitly deferred by the user with a tracked follow-up Plan where needed.

### Convergence

Once a stage's required checks pass, stop changing it: advance to the next stage, approval gate, blocker, or objective-terminal state without adding untriggered tests, reviews, or process steps. This convergence rule never shortens a valid build, lock, or harness deadline.

Convergence applies only to steps this workflow routes and does not trigger. An artifact type or case a step does not route is an ambiguity, not an untriggered step: surface it and resolve it with the user before proceeding. Silence in a step is never permission to skip it, and never grounds to call a review or check "untriggered."

## Directives

- **Minimum sufficient change:** The request and approved plan are target and ceiling — make the smallest complete change satisfying acceptance criteria and invariants; no speculative features, abstractions, configuration, extension points, or cleanup. Update related sites only when omission would make them incorrect; ignore polish.
- **KISS, YAGNI, DRY:** Reuse existing mechanisms. Extract helpers only for current duplication; never abstract for hypothetical use. Mirrored patterns stay parallel.
- Add backward compatibility only after explicit user consent. Without that consent, keep one current format, path, or behavior and remove obsolete compatibility code.
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code or recovering gracefully. Resolution ladder: `/repo-code-review`.
- **Comment the non-obvious, not the mechanism**: never explain a language feature or established house pattern the declaration already shows. Comment what the code cannot say — an invariant, a required ordering, a consequence. Review: `/code-style-review`.
- Do not add unit tests
- **Response style**: Stay concise — no pleasantries, hedging, or restating the request. Terse ≠ incomplete: cut the words around the facts, never the facts — when the user (or output style) asks for explanation, provide it fully. Never compress errors, irreversible-action confirmations, or order-sensitive step sequences. In code and commit messages: drop articles where natural, fragments fine, technical terms unchanged. Pattern: [thing] [action] [reason]

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out `researcher` subagents to validate each, compare pros/cons, then pick simplest good solution.
- **Architectural decisions** (new system shape, public API, data layout, threading model): stop and ask the user. Concisely present: (a) the problem, (b) proposed solutions, (c) pros and cons of each.

## Diagnosis Discipline

- **Verify root cause before editing**: state the suspected root cause, then confirm it — either close code inspection shows the bug unambiguously and deterministically, or logs directly evidence it. "I know what the bug is" is not verification.
- **If uncertain, say so** and re-investigate — never fabricate justifications when challenged.
- **Authority order when sources disagree** about intended behavior: explicit user statement > final approved plan plus approved deltas > AGENTS.md/docs/comments > current code behavior. Never silently make one side match another — surface the contradiction as a residual (footer line) naming both sides and which was trusted.

## Directory Structure

- `/Common/` - Shared utilities (`common::` namespace); `Common.h` is the single aggregation header (included by `Pch.h`) - `Common/AGENTS.md`
- `/DataPacker/` - Asset preprocessor producing `.pack`/`.manifest` files - `DataPacker/Source/AGENTS.md`
- `/Engine/` - Runtime: graphics, audio, input, frame state (`engine::` namespace); `Engine.h` is the single aggregation header (included by `Pch.h`) with `#ifdef BT_CLIENT`/`BT_SERVER` guards, save the few deliberate exceptions noted at the subsystem hubs - `Engine/Source/AGENTS.md`
- `/Projects/` - Game implementations (`game::` namespace) - `Projects/BrokenEngineSandbox/Source/AGENTS.md`
- `/Tools/AgentHarness/` - Loopback client/server command transport and harness ownership - `Tools/AgentHarness/AGENTS.md`
- `/Tools/ToolCommon/` - Shared Windows and coordination support compiled into both tools - `Tools/ToolCommon/AGENTS.md`
- `/Tools/WorktreeCli/` - Repository build, landing, and plan coordination - `Tools/WorktreeCli/AGENTS.md`
- `/ThirdParty/` - External libraries (do not modify) - `ThirdParty/AGENTS.md`
- `/Documents/` - Style guide (`C++StyleGuide.txt`), Mermaid architecture diagrams and network protocol reference (`Architecture/`), executable refactor plans (`Plans/`), and manual feature plans (`Features/`) - `Documents/AGENTS.md`

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

- **Log levels**: `kVerbose` — per-frame / high-frequency. `kDebug` — one-time (startup, connect). `kInfo` — state transitions, important one-shots (default threshold). `kWarning` — investigate (timeouts, desync); may spam. `kError` — failures; always logged. Per-category thresholds are runtime-adjustable (default `kInfo`, via the agent `set_log_level` command) atop a per-project compile-time floor that eliminates below-floor calls; mechanics in `Common/AGENTS.md` Logging.
- **Managers**: Singletons via `gp*` globals (`gpGraphics`, `gpAudioManager`)
- **Memory**: RAII everywhere, no manual memory management
- **DirectX Math**: Prefer aligned versions (`Float4A` not `Float4`)
	- **XMVECTOR W invariant**: Positions W=1.0; directions / velocities / normals / offsets W=0.0; color alpha defaults 1.0 (opaque)
	- **Function form, not operators**: `XMVectorAdd`/`Subtract`/`Multiply`/`Divide`/`Scale`/`Negate` — never `vec + vec`, `f * vec`, `-vec`.
- **Base classes**: Include/use game versions, not Base versions — `Camera.h` not `CameraBase.h`, `game::gpGame` not `GameBase` directly
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temp allocations instead of local `std::vector`/`std::string`. See `Common/AGENTS.md`
- **Allocation tracking**: Heap allocations in the main loop trigger `DEBUG_BREAK()`. When unavoidable, wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. See [Memory/AGENTS.md](Engine/Source/Memory/AGENTS.md)
- **LOG formatting**: logging in allocation-tracked Game/Engine code must remain allocation-free; /repo-code-review owns accepted formatting and wrapper details
- **Standard library / external headers**: new standard-library/third-party `#include`s in PCH-backed code go in `Common/ExternalHeaders.h` (gated by build defines), not in individual source files. The PCH-less AgentTools centralize shared consumption headers in `Tools/ToolCommon/ToolCliCommon.h`; rules and exceptions: `Common/AGENTS.md` and `Tools/ToolCommon/AGENTS.md`
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See `Common/AGENTS.md`
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See `Common/AGENTS.md`
