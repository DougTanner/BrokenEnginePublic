---
name: external-architecture-review
description: >-
  Review a code area for architectural shape: dependencies and layering,
  simulation and threading invariants, client/server and data shape, module
  cohesion, AI-generation residue, and ThirdParty replacement opportunities.
  Use only when the user explicitly requests an architecture review or an
  explicit parent workflow such as external-deep-analysis chains to it. Do not
  select it for routine code changes or general code questions.
allowed-tools: [Read, Grep, Glob, Agent, PowerShell]
---

# Architecture Review

Run a findings-only, multi-perspective review from the main invoking context. This skill owns cross-file shape: dependency structure, module depth, coupling, phase and thread alignment, and inter-module contracts. Hand function complexity, nesting, hot-path allocation, bool-to-flags, and file-size mechanics to `external-refactor-clean`. Exclude security auditing.

## Scope and Authorities

Require a review scope path. If absent, ask for it. Resolve the scope before dispatch:

- Exact file: review only that file. Follow dependencies and callers as evidence, but report a finding only when the defect is rooted in the target file or its contract.
- Directory, non-recursive (default): enumerate only files directly in that directory. Do not silently include descendants.
- Directory, recursive: include descendants only when the caller explicitly requests recursion.

Build the scoped-file manifest by running `pwsh -NoProfile -File .agents/scripts/Get-AnalysisManifest.ps1 -Path <scope>`, adding `-Recurse` only for an explicitly recursive scope and no extension filter, so every file type in scope, including shaders, is listed. The result gives every in-scope file its repository-relative path and its ordered root-to-file `AGENTS.md` authority chain; never reconstruct that enumeration or authority walk inline. On blocked (exit 2), narrow the scope and rerun; on error (exit 1), report the blocker. Evidence may cross the boundary to prove a scoped finding; incidental defects outside it are residuals, not findings.

Read the reported authority documents before dispatch. Preload every reviewer with those authority paths and the scoped-file manifest; require them to read the authorities before source. Findings cite the controlling `AGENTS.md` path and section or line when a repository rule supplies the judgment. Checklists are heuristics, never authority.

## Main-Context Reviewer Rounds

The main invoking context owns dispatch and synthesis. If this workflow is entered from a delegated context that repository policy forbids from delegating, return a main-context dispatch requirement; do not replace the five independent lenses with an inline approximation.

Inspect available depth and concurrency. Dispatch one `reviewer` role per lens below, in rounds no larger than the currently free slots. Never ask a reviewer to delegate. Refill slots only as prior reviewers finish, and do not merge lenses merely to fit a host limit. Every prompt includes the review scope, exact scope mode and manifest, authority paths, lens checklist, finding schema, external-claim routing, and findings-only boundary.

### Lens A — Dependencies and Layering

- Map direct and transitive includes; identify hubs, large closures, cycles, missing direct includes, and unused includes. Treat macro, template, and transitive use cautiously.
- Find exported symbols with no production callers; distinguish test-only reachability.
- Check layer integrity. `Common` must not depend on `Engine` or `Projects`; `Projects` must not reach through documented engine boundaries. Engine reads of game globals or types allowed by applicable authorities are not violations.

### Lens B — Simulation and Threading

- Trace determinism risks: RNG ordering, parallel floating-point reorder, CRC-participating state, `SharedMembers()` parity, and phase-crossing reads or writes.
- Check dispatch ownership, shared state changes, worker lifetime, and Update/PostRender/Interpolate alignment against documented thread and frame rules.

### Lens C — Client/Server and Data Shape

- Check `BT_CLIENT`/`BT_SERVER` separation and mirrored build behavior.
- Check collection member registration, shared/client/member partitioning, interpolation versus PostRender placement, and cohesion.
- When scoped code is shader-facing, compare CPU and shader constants, layouts, and shared definitions.

### Lens D — Cohesion and Generation Residue

- Treat depth as a property of the interface: it is everything callers and verification must know. Prefer small interfaces that hide substantial complexity; investigate shallow indirection, god-managers, and excessive cross-manager knowledge.
- Apply the deletion test to a suspected shallow module by tracing where its complexity goes. Complexity redistributed into named callers is evidence the module earns its keep; complexity that disappears may indicate pass-through, but is investigation evidence only.
- Count adapters only when optional variation or swappability justifies the seam: one adapter is hypothetical, while two current concrete adapters normally demonstrate actual variation. Authority-required trust, platform, build-affinity, ThirdParty, producer/consumer, CPU/GPU, client/server, and other invariant contracts are exempt from adapter counting, but still require structural-impact and locality/leverage evidence.
- Do not recommend a new seam, adapter, test-only extraction, or testing infrastructure solely for testability.
- Find cosmetic or bypassed abstractions, abandoned sibling patterns, substantial cross-file duplication, dead modules, and producer/consumer seams with mismatched contracts.
- For each scoped function, check whether an existing shared helper in the same layer already owns that responsibility: search sibling and shared modules for helpers with overlapping field sets or terms. A re-implemented shared helper is duplication evidence even when clone detection reports no textual clone group.
- Require concrete structural impact plus demonstrated locality and leverage: concentrate change, knowledge, or verification and increase capability per interface knowledge. Do not infer a defect merely from stylistic difference or the history of AI generation.

### Lens E — ThirdParty Replacement

- Read `ThirdParty/AGENTS.md`, list existing ThirdParty packages, and avoid proposing an already-covered dependency unless extending it removes separate in-house code.
- Find cohesive, reusable in-house clusters with no engine-specific reason to exist. Skip engine pipelines, gameplay, collections, managers, and Vulkan/shader glue.
- Consider only replacements likely to remove more than 500 `bt-token-v1`; measure the inclusive candidate range with `.agents/scripts/Measure-Tokens.ps1`.
- For each candidate, name the library, removable paths/ranges, integration and dependency risks, and the license proposition requiring verification. The local license allow list in `ThirdParty/AGENTS.md` remains controlling authority.

## Finding and Claim Contracts

Every reviewer reports each finding in this exact shape. Lenses A, B, C, and E omit the Lens D evidence field entirely; Lens D alone includes it after `Evidence`.

```markdown
- [severity/category] `path:line` — `symbol or contract`
  - Evidence: repository-observed fact; controlling authority citation when applicable
  - Lens D evidence: Interface surface: <caller/verification knowledge>; Deletion: <where complexity goes>; Seam: <actual variants or controlling invariant>; Locality/leverage: <concrete payoff>
  - Impact: concrete architectural or invariant consequence
  - Correction: smallest structural correction or investigation needed
  - Confidence: HIGH | MEDIUM | LOW
```

Lens-D tests are investigation heuristics only: they do not automatically establish findings or mandate abstraction or testing infrastructure without complete applicable evidence and concrete structural impact.

Use precise symbols and evidence, not thematic summaries. Unused includes and dead-code candidates must retain confidence labels. Report no-finding conclusions explicitly for assigned lens checks.

Reviewers do not establish non-obvious external API, specification, license, maintenance, or ThirdParty behavior from memory. They return an External Claim Verification Request containing the exact proposition, dependent finding, applicable repository version/configuration, why it matters, and an official candidate source when known.

After reviewer rounds complete, route every such request through `verify-external-claims` before consolidation. Apply its verdict: retain `VERIFIED` evidence, remove or correct a `REFUTED` dependent finding, and move `UNRESOLVED` claims to residuals rather than presenting them as confirmed recommendations.

## Consolidate and Report

Deduplicate by root cause, preserve the strongest evidence, and cross-reference systemic findings without inflating their count. Any prioritized Lens-D recommendation preserves its interface, deletion, seam, and locality/leverage evidence and payoff. Choose one most impactful architectural improvement and name its modules, correction, and reason. Do not invent findings to populate a section.

Return `## Architecture Review: <target>` followed by these `###` sections, with
these exact names, in this order:

Scope and Authorities (scope mode, file count, `AGENTS.md` paths); Overview;
Dependencies and Layering; Simulation and Threading; Client/Server and Data
Shape; Cohesion and Generation Residue; ThirdParty Replacement Opportunities
(with removable size and license evidence); Cross-Cutting Concerns; Handoff to
external-refactor-clean (one concise scope note, never line-level findings);
Prioritized Recommendations (ranked, each with rationale and rough effort);
`### Architecture Health: <HEALTHY | MINOR CONCERNS | NEEDS ATTENTION |
CRITICAL>` plus a brief justification; External-Claim Residuals.
