# Broken Engine

A C++23 Vulkan game engine client/server using data-oriented design, with data pre-packer (offline; runtime reads only `.pack` chunks). Top-down RTS-scale camera: kilometers above an ocean with islands, small units on screen. West is -x, East is +x, North is +y, South is -y, Up is +z, Down is -z. The client and server run a deterministic simulation, user input (client simulation change requests) are rare so we prioritize CPU/GPU smoothness over client -> server -> client input latency. The world is an unbounded sparse grid of cells, each simulated independently in parallel. Fixed sim tick rate; render free-runs via interpolation. PostRender state is bit-deterministic (`/fp:strict`, CRC-checked per tick); Interpolate/render and client-only visuals are not, and stay out of the CRC.

## Environment

- **IDE**: Visual Studio 2026
- **Language**: C++23
- **Graphics API**: Vulkan 1.2
- **Platform**: Windows 10+
- **Agent shells**: Claude Code runs in Git Bash; Codex CLI runs in PowerShell 7. Claude calls `pwsh` explicitly for PowerShell 7 scripts.

## IMPORTANT: Context management and agent selection

### Subagents

- Main session is manager: delegate all code writing; keep planning, evidence adjudication, user decisions, and process management in main session
- Subagent packets: CONCISE, MINIMAL but COMPLETE, and fresh — objective, scope, baseline, relevant residuals/handoffs, file paths, output format; use a fresh Claude prompt or Codex `fork_turns:"none"` without breaking the residual chain. Every distinct assignment gets a fresh context. Reuse is limited to the same assignment's focused correction/retest; repeat an identical prompt only when the prior attempt failed before producing a usable immutable report.
- Delegated process calls use caller-assigned absolute paths under `<session-worktree>/Temp/AgentReports/` and follow [.agents/references/subagent-reporting.md](.agents/references/subagent-reporting.md). Subagents write full evidence there and return only the compact indexed envelope; downstream packets carry the report path, SHA-256, indexed IDs, evidence locators, and dependencies instead of report bodies. Read only the cited bounded ranges with the reporting helper before adjudication, plus any mandatory manifest/PASS-ledger ranges required by the consuming safety gate.
- When a wrapper-created session is used, one top-level session owns its isolated worktree, live AgentCli session claim, fixed start commit, and `Temp/AgentReports/` directory for the full plan lifecycle; every subagent shares that checkout and baseline. The wrapper's five `BROKEN_ENGINE_*` provenance values are authoritative.

### When to use each model

If you are ChatGPT Codex and the active surface exposes named custom-agent selection, use these project profiles; their `.codex/agents/*.toml` files own configured model names:

- Fable role -> `fable` agent
- Opus role -> `opus` agent
- Sonnet role -> `sonnet` agent

Configuration is not runtime attestation. Mark a named mapping selected only from host/runner metadata that identifies the chosen profile, and mark an actual model only from host/runner metadata that identifies the runtime model. Agent prose, prompts, and project TOML prove neither. When the surface lacks named selection or trusted runtime metadata, disclose `unknown/unselectable`, do not claim a project profile was applied, and do not claim model diversity. Continue in a fresh role-prompted context when the process permits it. The `codex-review` explicit headless Sol pin is a surface-specific fallback exception and must be reported as such.

### Context lifecycle

- Compaction is a lifecycle boundary, not permission to reconstruct state from memory. On a main-context compaction signal, finish only the current atomic operation, write [`be-agent-lifecycle-handoff/v1`](.agents/references/agent-lifecycle-handoff.md), stop mutations, and resume with `/clear` or a fresh task in the same live wrapper/worktree. If a delegate compacts, terminate it at the next safe boundary and restart that same assignment once with the handoff and a new report path; audit existing edits before continuing.
- One orchestration wait/poll call and the interval between user updates are each at most 60 seconds. Poll expiry means only that no update arrived; it is not job failure. Preserve build, lock, shell, and other skill-owned deadlines. Terminate a delegate only after an explicit terminal/deadline condition and no usable immutable report.

Fable is the top tier for judgment roles (manager/planner/reviewer); don't move mechanical roles up to it or these roles down.

- Code/Web search: Sonnet — put direct quotes, file:line references, or links in the full report; the envelope indexes every decision-driving result without replacing evidence with a summary
- Builds: Sonnet — invoke `/compile`; put status plus verbatim error/relevant warning lines in the full report and return the compact routing envelope
- Large-file/log filtering: Sonnet — put matching lines verbatim in the full report and index the relevant groups in the envelope
- Planning: Fable
- New Code: Opus
- Code edits: Sonnet
- Style review: Sonnet
- Documentation: Opus
- Primary domain correctness review: Fable
- Bounded adversarial review: Opus — only for Tier 3 or a concrete unresolved failure hypothesis from the primary review
- Conditional session audit: Fable — only when a Reconcile, audit when triggered, and finalize stage trigger applies

## IMPORTANT: C++ Code Change Process (YOU MUST follow this process when making code changes)

This process activates only for a session whose fixed session-start baseline contains the commit that lands it. A session started from an earlier baseline, including the session implementing this process, finishes under the process captured at that baseline.

The main session owns execution decisions and accumulates every delegated report path, indexed residual, sweep handoff, reviewer focus area, approved delta, and verification result. Pass that evidence forward to every role that consumes it and route unresolved work explicitly; never drop it silently. Skip conditional roles whose triggers do not apply. Revisit an earlier stage only when changed bytes invalidate its evidence or a decisive check fails.

### Risk tiers

Before implementation, classify the whole approved change at the highest applicable tier:

- **Tier 1 — mechanical:** documentation, style, project membership, or local behavior-preserving work with no public signature or invariant exposure.
- **Tier 2 — scoped behavior:** one subsystem's runtime or tool behavior, excluding determinism/CRC, wire/protocol, serialization or data layout, save/replay compatibility, threading, trust boundaries, build/bootstrap coordination, and independently owned cross-subsystem integration.
- **Tier 3 — invariant/integration:** any Tier-2 exclusion, build/bootstrap coordination that can block other sessions, or a change spanning independently owned subsystems.

A reviewer may escalate the tier with cited evidence. Lowering the approved tier requires user approval. Risk changes update the remaining required roles and acceptance matrix before work continues.

### Seven named stages

1. **Approve and classify.** The user creates or loads a plan. Run `/plan-audit`, adjudicate its evidence, run `/external-grill-plan`, incorporate approved deltas, and obtain explicit approval before code changes. The manager then writes an execution-control record containing the fixed process baseline, selected tier and concrete triggers, required and conditional roles, and the initial acceptance-criterion-to-check matrix. Plans may include optional `/agent-harness` launch, drive, query, or screenshot checks when observable runtime behavior requires them.
2. **Implement and propagate.** Delegate approved disjoint slices through `/implement-plan` and preserve each self-audit, sweep handoff, focus area, and residual. Invoke `/update-affected-code` only when an implementation report emits a sweep handoff or the change affects a signature, identity, semantics, layout, client/server guard scope, or required mirrored pattern. Search and update only necessary affected sites; similarity alone does not expand scope.
3. **Run targeted pre-review checks.** Before correctness review, run the smallest affected-target compile and static checks that make the changed bytes reviewable. Use `/compile` for builds and `/resolve-findings` for confirmed failures. Do not substitute unrelated full builds or runtime exploration for the approved acceptance evidence.
4. **Review and resolve correctness.** Run one fresh domain correctness review over the logical change: `/repo-code-review` for C++, the applicable specialist review for another code domain, or a fresh domain-coherence review for Tier-1 non-C++ work. Add bounded `/adversarial-review` only for Tier 3 or one concrete unresolved failure hypothesis from the primary review; shader changes use `/glsl-review` for shader-specific evidence. The main adjudicates the union once by cited evidence, classifying Intent (`conformance | plan_delta`) and Scope (`non_structural | structural`). Different findings are complementary, not grounds for a consensus rerun. Verify external claims before dependent fixes; ask the user when authority remains unresolved. Send only accepted `conformance + non_structural` fixes through `/resolve-findings`, focused on affected regions and checks. An accepted in-scope structural acceptance failure remains a blocker pending user direction; route only proven pre-existing or out-of-scope structural residuals through `/create-follow-up-plans`, and obtain user approval for plan deltas. A second fix/retest pass requires a still-reproducible blocker and remains confined to the invalidated regions and checks.
5. **Apply conditional hygiene.** Invoke `/code-style-review` only when C++ changed. Invoke `/update-claude-docs` only when durable instructions or invariants changed or became false. Invoke `/update-vcxproj` only when files were added/removed or whole-file client/server affinity changed. Re-run only the targeted checks invalidated by resulting edits.
6. **Verify the acceptance matrix.** Invoke `/verify-changes` with the final approved plan, approved-delta summary, fixed baseline, execution-control record, and all accumulated reports and residuals. Map every approved criterion and declared invariant to at least one decisive check; justify every skip and name the independent signal for any intentionally duplicated check. Tier 1 uses static checks and affected-target compilation when C++ changed; Tier 2 adds the smallest observable scenario for each changed behavior; Tier 3 adds only the exposed invariant-specific client/server, replay/determinism, trust-boundary, coordination, or integration checks. Harness and full client/server builds run only when the matrix exposes those triggers. When every row passes, stop exploratory verification and issue the authoritative final-tree content manifest and PASS ledger required by finalization.
7. **Reconcile, audit when triggered, and finalize.** Enter `/finalize-changes`, which reconciles with current primary and reverifies changed or invalidated evidence before landing. After reconciliation, run one fresh `/session-audit` over the complete logical change only when there were late semantic fixes, reconciliation changed session bytes, Tier-3 cross-file integration requires a whole-change view, or a changed region received no correctness review. Adjudicate that audit once under the same Intent/Scope rules; resolve accepted non-structural conformance findings and rerun only invalidated compile/matrix checks. Keep accepted in-scope structural acceptance failures blocking; create follow-up plans only for proven pre-existing or out-of-scope structural residuals. Resume `/finalize-changes` only with a passing final tree. It always presents the final landing summary and requires explicit user sign-off before advancing primary; it owns landing, claim release, and completion reporting.

### Convergence rule

Once all approved acceptance criteria and declared invariants have PASS evidence on the current bytes, stop. Do not rerun reviewers for consensus, duplicate a check without a named independent signal, or broaden verification with unapproved exploratory scenarios. Loop back only for changed bytes or a decisive failure, and scope the loop to the invalidated evidence.

## Directives

- **Minimum sufficient change:** Treat request and approved plan as target and ceiling. Make smallest complete change satisfying acceptance criteria and invariants; preserve other behavior. No speculative features, abstractions, configuration, extension points, or cleanup.
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
