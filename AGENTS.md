# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. Client/server simulation is deterministic; rare user input favors CPU/GPU smoothness over round-trip latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+
- **Agent shells**: Claude Code runs in Git Bash; Codex CLI runs in PowerShell 7. Claude calls `pwsh` explicitly for PowerShell 7 scripts.

## IMPORTANT: Context management and agent selection

### Subagents

- The main agent may edit directly. Delegate only when independent investigation, a fresh review, or parallel work materially improves the result.
- Give delegates concise, self-contained scope and return concise findings, changed files, checks, and residuals inline for Tier 1 and Tier 2 work.
- Delegates return concise inline handoffs. Create one file-backed final acceptance ledger only for a queue mutation, reconciliation, or primary landing; its format is defined in [.agents/references/subagent-reporting.md](.agents/references/subagent-reporting.md).
- Isolated worktrees and session claims are required only for queue selection or mutation, shared build/bootstrap coordination, or landing. Ordinary work uses the checkout the user supplied and preserves unrelated changes.

### When to use each model

If you are ChatGPT Codex and the active surface exposes named custom-agent selection, use these project profiles; their `.codex/agents/*.toml` files own configured model names:

- Fable role -> `fable` agent
- Opus role -> `opus` agent
- Sonnet role -> `sonnet` agent

Configuration is not runtime attestation. Record profile/model identity only when host metadata proves it; otherwise omit diversity claims. Fresh contexts are for independent review or a focused retry. The `codex-review` explicit Sol headless pin is a documented exception.

- One orchestration wait/poll call and the interval between user updates are each at most 60 seconds. Poll expiry means only that no update arrived; it is not job failure.

Fable is the top tier for judgment roles (manager/planner/reviewer); don't move mechanical roles up to it or these roles down.

- Code/Web search: Sonnet — return direct quotes, file:line references, or links
- Builds: Sonnet — invoke `/compile` and return status plus decisive errors or warnings
- Large-file/log filtering: Sonnet — return the relevant matching lines
- Planning: Fable
- New Code: Opus
- Code edits: Sonnet
- Style review: Sonnet
- Documentation: Opus
- Primary domain correctness review: Fable
- Bounded adversarial review: Opus — only for Tier 3 or a concrete unresolved failure hypothesis from the primary review
- Conditional session audit: Fable — only when a Reconcile, audit when triggered, and finalize stage trigger applies

## C++ Change Workflow

The default workflow is fast and task-driven. The user's request is implementation authority for Tier 1 and Tier 2 changes; agents classify the work, make the smallest complete change, run proportionate checks, and report changed files, decisive checks, and residuals. Do not require a plan audit, grilling interview, user approval round-trip, wrapper session, report artifact, final manifest, or landing workflow unless a final-evidence gate applies.

### Risk tiers

Before implementation, classify the whole change at the highest applicable tier:

- **Tier 1 — mechanical:** documentation, style, project membership, or local behavior-preserving work with no public signature or invariant exposure.
- **Tier 2 — scoped behavior:** one subsystem's runtime or tool behavior, excluding determinism/CRC, wire/protocol, serialization or data layout, save/replay compatibility, threading, trust boundaries, build/bootstrap coordination, and independently owned cross-subsystem integration.
- **Tier 3 — invariant/integration:** any Tier-2 exclusion, build/bootstrap coordination that can block other sessions, or a change spanning independently owned subsystems.

A reviewer may escalate the tier when the changed bytes expose a higher-risk surface. Resolve material ambiguity with the user before proceeding.

### 1. Approve and classify

At session start, fix the process baseline. Record the complete user objective, every approved stage and deliverable with its disposition, risk tier/triggers, roles, and one check per criterion or grounded invariant. For a final-evidence gate, persist this execution control before implementation and bind its hash in the final ledger. Tier 1 and Tier 2 authorize implementation; Tier 3, queue, reconciliation, and landing work also gets a short execution card. Run `/plan-audit` and `/external-grill-plan` only for material ambiguity.

### 2. Implement and propagate

Implement the smallest complete change and update only required affected sites. Invoke `/update-affected-code` only for a sweep handoff or a changed signature, identity, semantics, layout, client/server guard scope, or required mirror.

### 3. Run targeted pre-review checks

Run the smallest applicable static checks and affected-target compilation before review. Full builds and runtime or harness scenarios are acceptance-matrix decisions.

### 4. Review and resolve correctness

Run one fresh domain review: `/repo-code-review` for changed C++, `/glsl-review` for shaders, and direct coherence for Tier-1 non-C++. Run `/adversarial-review` only for Tier 3 or one concrete unresolved reachable hypothesis. Adjudicate evidence once; accepted fixes re-review and retest only affected regions, and a second wave needs a reproducible blocker.

### 5. Apply conditional hygiene

Run `/code-style-review` only for changed C++, `/update-claude-docs` only for durable instruction or invariant drift, and `/update-vcxproj` only for added or removed files or whole-file client/server affinity changes.

### 6. Verify the acceptance matrix

Map every approved criterion and declared invariant to a decisive check, naming any duplicate's independent signal. Tier 1 uses static, schema, link, and validator checks plus C++ compilation; Tier 2 adds the smallest observable scenario; Tier 3 adds only exposed invariant or integration checks. A passing acceptance matrix completes only the current stage.

### 7. Reconcile, audit when triggered, and finalize

Use the final-evidence path for queue mutation or completion, reconciliation, requested primary commit or landing, shared build/bootstrap work, or Tier-3 integration. Those gates create the immutable ledger and `/verify-changes` manifest. Run one `/session-audit` only for late semantic fixes, reconciliation changes or invalidated assumptions, Tier-3 cross-file integration, or contract-significant regions unseen by review.

Queue work normally uses a registered session worktree. An explicitly user-authorized `primary-commit` may adopt clean primary: the receipt proves the pre-commit mutation, then `/finalize-changes` validates committed primary and releases the row claim. A session landing still needs final approval before advancing its parent. WorktreeCli is the sole row parser and mutator; validate before queue selection or mutation, after queue-changing landing, and after primary completion.

Landing and claim release complete only a repository stage. Continue the next recorded stage in the same session or present its approval gate. The session is objective-terminal only when every stage is complete or each unfinished stage was explicitly deferred by the user and has a verified receipt in the live plan queue.

### Convergence

Once the current stage's required checks pass, stop changing it. Advance the recorded objective to its next stage, approval gate, blocker, or objective-terminal state. Do not add exploratory tests, duplicate reviews, or process steps without a concrete trigger. A valid build, lock, or harness deadline is not shortened by this iteration budget.

## Directives

- **Minimum sufficient change:** Treat request and approved plan as target and ceiling. Make smallest complete change satisfying acceptance criteria and invariants; preserve other behavior. Every planned component and check must follow from that scope or an existing repository contract; assumptions and possible future needs do not create scope. No speculative features, abstractions, configuration, extension points, or cleanup.
- **Necessary propagation only:** Update related sites only when omission would make them incorrect or violate an approved criterion/invariant. Similarity or possible benefit does not create scope. Route only proven, non-trivial pre-existing or out-of-scope incidental defects through `/create-follow-up-plans`; ignore polish.
- **KISS, YAGNI, DRY:** Reuse existing mechanisms. Extract helpers only for current duplication; never abstract for hypothetical use. Mirrored patterns stay parallel.
- Add backward compatibility only after explicit user consent. Without that consent, keep one current format, path, or behavior and remove obsolete compatibility code.
- **Error handling at trust boundaries only**: assume function parameters from within the codebase are valid — no defensive validation between our own functions. Do validate anything opaque to the current code unit: network input, file reads, OS/third-party API results.
- **No useless ASSERTs**: an ASSERT that throws one line before the code would crash anyway adds false safety — remove it; prefer making the condition impossible in calling code or recovering gracefully. Resolution ladder: repo-code-review skill §2c.
- Do not add unit tests

## Resolving Ambiguity

- **Trivial choices** (naming, small implementation details, equivalent approaches): pick the simplest and proceed.
- **Non-trivial ties** (two viable approaches, neither architectural): fan out Opus subagents to validate each, compare pros/cons, then pick simplest good solution.
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
- `/Documents/` - Style guide (`C++StyleGuide.txt`), Mermaid architecture diagrams and network protocol reference (`Architecture/`), and the plan queues (`Plans/`, `Features/`) - `Documents/AGENTS.md`

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
- **Standard library / external headers**: new standard-library/third-party `#include`s go in `Common/ExternalHeaders.h` (gated by build defines), not in individual source files — rule and exception: `Common/AGENTS.md`
- **Flags over booleans**: Use `common::Flags<EnumType>` instead of multiple `bool` variables. See `Common/AGENTS.md`
- **Multithreading**: Use `common::gpMultithreading->Dispatch()` or `common::PersistentWorker` for data-parallel work. See `Common/AGENTS.md`
