---
name: external-refactor-clean
description: >-
  Analyze a C++ file or directory for evidence-backed in-function refactoring
  opportunities and AI-generated residue. Use when the user explicitly runs
  /external-refactor-clean, or when /external-deep-analysis invokes its
  in-function phase. Route oversized files to /reduce-file and class, module,
  layer, or dependency-shape concerns to /external-architecture-review.
allowed-tools: [Read, Grep, Glob, Bash, Agent, PowerShell]
---

# Refactor Clean

Analyze local function mechanics and report findings only. Do not edit source,
perform security review, or turn style preferences and numeric thresholds into
findings without behavioral or structural evidence.

## Resolve Scope and Authorities

Require one target path and resolve it before inspection:

- Exact file: analyze only that `.h` or `.cpp`; never add siblings.
- Directory: analyze directly contained `.h` and `.cpp` files. Include
  descendants only when the caller explicitly requests recursion.

Build the manifest with:

```powershell
pwsh -NoProfile -File .agents/scripts/Get-AnalysisManifest.ps1 -Path <target> -Extension .h,.cpp
```

Add `-Recurse` only when the caller explicitly requests recursion. In Claude
Code's Git Bash terminal, convert the script path first with `cygpath -w`, as in
`.agents/skills/cleanup-worktrees/SKILL.md`. The result gives every in-scope
file its repository-relative path, its ordered root-to-file `AGENTS.md`
authority chain, its `bt-token-v1` size, and a `reduceFileCandidate` flag; never
reconstruct that enumeration, authority walk, or per-file measurement inline. On
blocked (exit 2), narrow the scope and rerun; on error (exit 1), report the
blocker. Read every reported authority document and
`Documents/C++StyleGuide.txt` before inspection, and record the authority map in
the report. Heuristics below locate candidates; findings cite the controlling
authority.

The `bt-token-v1` value is normalized UTF-8 bytes divided by four and rounded
up. Measure a function directly with its inclusive range:

```powershell
pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path> -StartLine <first> -EndLine <last>
```

## Triage and Dispatch

Route every file the manifest flags as a `reduceFileCandidate` — headers over
5,000 bt-token-v1 and implementations over 10,000 — directly to
`run /reduce-file <path>`; do not inspect their functions here. These
thresholds are routing observations, not proof of a defect.

After triage, file counts above about 15 or aggregate size above about 80,000
bt-token-v1 indicate a useful review split, but do not alone require one. The
main invoking context may dispatch scoped rounds of `reviewer` roles according
to available capacity. Give each worker an explicit file manifest, authority
map, inspection rubric, and evidence/report contract. A worker never delegates
or starts another round. Each file belongs to one worker; the main context
deduplicates results. Smaller targets may be inspected inline.

## Inspect Local Mechanics

Trace functions, their local control flow, and reachable call sites needed to
prove a candidate. Inspect for:

- decomposable oversized functions, excessive control-flow nesting,
  unreachable statements, unused locals or parameters, and repeated local
  logic;
- heap work in a demonstrated tracked hot path, missing suppression around
  unavoidable heap work, or allocation-dependent logging;
- groups of local booleans better expressed as `common::Flags`, unaligned
  DirectXMath storage, vector operators instead of named DirectXMath functions,
  and guards wider than the locally differing statements;
- needless defensive validation between trusted internal callers, useless
  assertions, swallowed failures, ambiguous default returns, reachable
  empty/single/zero boundary gaps, and acquire/release paths not protected by
  RAII;
- dense narration comments and intra-file naming or TODO drift as local residue;
  hand actual formatting enforcement to `/code-style-review`.

Require concrete reachability or data-flow evidence. Parameter count, nesting
depth, function size, a boolean count, or a search match alone is an
observation. Recommend only the smallest local correction that preserves
behavior and repository invariants.

Keep class design, public signature redesign, ownership between types,
cross-file duplication, include/dependency shape, cohesion, and layer placement
out of findings. List such evidence under `Architecture handoff` for
`/external-architecture-review` without designing the fix.

Apply repository rules only where their authorities cover the file:

- `Common/AGENTS.md` and `Engine/Source/Memory/AGENTS.md` govern workbuffer use,
  tracked-loop allocation, suppression, and the required `// Heap:` rationale.
- `Common/AGENTS.md` and `Common/Log/AGENTS.md` govern allocation-free logging
  and workbuffer-backed formatting in tracked paths. Do not infer a violation
  from a format token without proving the call site's allocation boundary.
- PCH-backed consumption headers belong in `Common/ExternalHeaders.h`; the
  PCH-less AgentTools instead centralize shared consumption headers in
  `Tools/ToolCommon/ToolCliCommon.h`. Respect documented implementation-unit
  exceptions. Header placement is not an in-function finding; route exposed
  dependency-shape evidence through the architecture handoff.
- Treat a `*Base` reference as a candidate only when the file's authorities and
  an established game-derived surface require the derived type. Base
  definitions, deliberate base-layer code, and Engine reads of `game::gp*`
  globals are not violations; see `Engine/Source/AGENTS.md`.

## Evidence and Report

Fill every field of this schema for each finding, omitting empty
finding-category sections but never the manifest, coverage, handoff, or summary:

```markdown
## Refactor-Clean Analysis: <target>
Scope: <exact-file | directory-non-recursive | directory-recursive>

### Manifest and Authorities
- <path> — <tokens> — <analyzed | run /reduce-file <path>> — <authorities>

### Findings: <category>
- <path:line> — <function/local symbol> — Evidence: <code and reachable
  behavior>. Authority: <document and invariant>. Impact: <effect>. Smallest
  correction: <local boundary>. Verify: <decisive coverage>.

### Coverage
- <path> — inspected <functions/ranges>; checked <categories>; result
  <finding IDs | none | reduce-file handoff>

### Architecture handoff
- <path:line> — <class/module/dependency evidence> — run
  `/external-architecture-review <original target and scope>`
- none

### Summary
- Files: <enumerated/analyzed/oversized>
- Findings: <count by category and total>
- Observations not promoted: <threshold/search observations and reason>
- Residuals: <unreadable files, incomplete coverage, or external claims>
```

When invoked by `/external-deep-analysis`, retain its exact original target and
scope, consider Phase-1 investigation paths as evidence only, and return the
manifest, authority map, findings, coverage, residuals, and architecture handoff
without expanding the boundary.
