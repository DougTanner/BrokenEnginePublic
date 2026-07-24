---
name: repo-code-review
description: >-
  Review session-changed C++ for reachable correctness defects and Broken
  Engine contract violations, including trust boundaries, allocation tracking,
  ASSERT use, SOA collections, determinism, XMVECTOR W roles, frame phases,
  client/server affinity, and integration. Use after C++ changes or when the
  user asks to review, check, or audit C++ code. Excludes shader-only and
  non-C++ changes; style and formatting belong to code-style-review.
allowed-tools: [Read, Grep, Glob, Bash, PowerShell]
---

# Repository C++ Review

Run one fresh `reviewer` pass. Findings only: do not edit, run mutating
commands, implement fixes, invoke other agents, or delegate. Review logic and
correctness, not style, formatting, naming, general comment quality, or
documentation.

## Required Inputs

Require a self-contained brief containing:

- the fixed baseline, complete immutable C++ diff, and exact changed
  files/regions, separated from pre-existing and concurrently owned changes;
- approved intent, plan and deltas, affected contracts, and declared
  invariants;
- implementation handoff, acceptance criteria, affected-site triggers, and any
  prior findings relevant to a focused re-review;
- checkout path and applicable repository instructions.

Return `BLOCKED` when the diff boundary, intent, or invariants are missing or
moving. Do not reconstruct them from a mutable merge base or expand a supplied
review into an unbounded repository audit. After an accepted fix, review only
the fixed region and directly affected paths unless a reproducible failure
justifies another wave.

## Workflow

1. Read the changed regions in full-function context, their applicable
   `AGENTS.md`, and the producers, consumers, callers, and mirrored paths needed
   to trace the declared contracts. Diff-only inspection is insufficient.
2. Search changed signatures, semantics, enum values, layouts, ownership,
   guards, frame phases, and serialization identities across every affected
   site. Check substantial new logic against existing helpers and deliberate
   mirrored patterns.
3. Apply the relevant checks below. Turn a checklist concern into a finding
   only when a concrete changed path makes the failure reachable.
4. Try to disprove each candidate finding against guards, caller preconditions,
   lifecycle, and current repository contracts. Report the smallest correction,
   without implementing it.
5. Emit an atomic `/verify-external-claims` request for every candidate finding
   that depends on a non-obvious external API, language, specification, OS, or
   library fact. Do not browse or present that fact as confirmed.
6. Measure changed `.cpp` files with
   `pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path>`.
   Record a size observation only when the changed region exposes a concrete
   cohesive split. Return it as a manager follow-up candidate; never reduce the
   file or prescribe an inline reduction during review.
7. Return the report and the conditional `/update-vcxproj` trigger. Never read
   or grep project XML in this review.

## Correctness Checks

### General logic and ownership

Trace initialization, lifetime, aliasing, ranges, conversions, arithmetic,
branch and early-return behavior, RAII cleanup, GPU-resource ownership, and
error-path progress. Internal callers may rely on established preconditions;
do not demand defensive checks between trusted code units. Validate opaque
file, network, OS, user, and third-party data at its owning trust boundary.

### Changed comments

Treat a changed comment as a correctness issue only when it asserts a materially
false runtime or contract fact. Changelog narration, wording, formatting, and
documentation coherence belong outside this review.

Route comments that merely explain a language feature or established house
pattern to `/code-style-review`; they are not correctness findings.

### Trust boundaries and failure channels

Require validation before externally controlled sizes, counts, indices, or
payloads can allocate, iterate, index, or mutate destination state. Preserve
the owning subsystem's established failure channel; there is no universal
"never throw" or "catch everything" policy.

- Network variable payload handlers parse into bounded local state before
  applying destination state. `Client::Receive` (`/Engine/Source/Network/Client/Client.cpp`)
  drops and logs one corrupt inbound packet; `Server::Receive` (`/Engine/Source/Network/Server/Server.cpp`)
  additionally records the contract violation. Keep their handshake, budget,
  and packet-specific policies distinct.
- Invalid persisted grid data follows
  `GameSaveLoad::ReadGrid` (`/Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`):
  clear partial grid/fleet state, log, and return `false`; apply the global-ID
  counter only after the complete read succeeds.
- Corrupt boot-required eager animation data follows
  `LoadAnimationDataFromEagerChunks` (`/Engine/Source/Graphics/AnimationData.cpp`):
  log the asset identity at `kError` and rethrow to the boot crash-report path.
- For asynchronous per-chunk failures, trace the owning worker's published
  completion and waiter/progress contract before accepting any return, throw,
  or skip path.

### Allocation-tracked paths and logging

Flag reachable heap allocation while Engine/Game tracking is active unless the
existing design requires it and `ScopedSuppressAllocationTracking` carries the
required `// Heap:` rationale. Startup, teardown, offline tools, and other
untracked paths do not become findings merely because they allocate.

Use `gpThreadLocal->mWorkbuffer` for tracked temporary data. Follow
`GameBase::BuildAndDispatchFrameTicks` (`/Engine/Source/GameBase.cpp`) for a
scoped workbuffer-backed list whose lifetime covers synchronous dispatch. For
loop-built dynamic log text, follow the current
`CrcValidateLoop` (`/Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayCrc.cpp`)
`ScopedWorkbufferArena` construction. Verify changed tracked logs use the
repository's allocation-free wrappers and formatters rather than temporary
strings or allocating formatting paths.

### ASSERT behavior

`ASSERT` throws in every configuration. For each added or changed assertion,
compare behavior with the assertion removed:

- If the next operation fails immediately at the same location, the assertion
  is useless; require removal or make the invariant impossible at its source.
- If the condition is externally reachable, require the trust boundary's
  existing failure channel, selected from the subsystem policy above rather
  than a blanket return or throw rule. Preserve progress and waiter notification.
- If failure would otherwise silently corrupt state, output, or determinism,
  the assertion may carry real diagnostic value.

Do not accept an `ASSERT` added only to silence an analyzer. Require an
analyzer-visible reachable guard, or a narrow suppression that states the proven
invariant.

### Collections, persistence, and identity

When a `Collection<T>` gains, loses, reorders, or changes a `* __restrict`
column, load the authoritative
`add-collection-member` (`../add-collection-member/SKILL.md`) skill and verify its
complete live-variant checklist. Do not substitute a copied checklist here.
Treat unresolved CRC membership, tuple order/subset, versioning, unconditional
persistence, creation initialization, transfer, hydration, or identity behavior
as correctness contracts, not style.

For other persisted or wire-visible layouts, trace version/identity updates,
read/write order, bounds, CRC participation, compatibility intent, and every
producer and consumer. Do not infer backward compatibility authority.

### Determinism, threading, and frame phases

For CRC-fed or replay state, check deterministic RNG and math, stable iteration
and reduction order, serialization/CRC coverage, and absence of wall-clock or
platform-dependent decisions. Dispatched workers may write only owned state or
per-thread accumulators; reductions, especially floating-point reductions, must
preserve the declared order.

Keep Interpolate data visual/client-only and PostRender data committed and
deterministic. Verify `AllocateAndCopy()` precedes current-frame access and that
no Update read depends on a later phase write.

### XMVECTOR W applicability

Apply the W rule only when the changed `XMVECTOR` semantically represents a
position (`W=1`), direction/velocity/normal/offset (`W=0`), or color (the
declared alpha). Do not impose those roles on quaternions, planes, matrix rows,
generic four-lane data, masks, or packed payloads; derive their lane contract
from the owning type.

For applicable values, trace every constructor, return, out-parameter path,
early return, arithmetic input, and collection spawn/transfer boundary. Flag
mismatched position subtraction, position-plus-offset construction, inherited
input W where the output role is fixed, and consumer-side stamping that merely
hides a producer error. Accept an explicit post-integration position clamp when
the owning math contract requires it.

### Integration, layering, and build affinity

Search all callers when a signature, unit, default, ownership rule, W role, or
phase changes. Search every switch/dispatch/serialization table for changed
enums and both CPU/GLSL consumers for shared headers. Check client/server and
per-collection mirrors without abstracting deliberate parallel boilerplate.

Engine code may call game hooks and use game globals/types; flag the reverse
ownership leak only when an engine-owned type acquires a game-specific concept.
Prefer game-derived aggregation surfaces where repository instructions require
them.

Verify guard placement from source and aggregation context. Emit an
`/update-vcxproj` trigger for every added or removed C++ file and every existing
file that gained or lost a whole-file `BT_CLIENT`/`BT_SERVER` guard. Do not
inspect membership or filters here.

### Public state and forwarding APIs

For each changed interface, flag a getter, setter, drain, take, `Is*`, `Can*`,
or equivalent whose entire implementation is one state access, assignment, or
pass-through call when the underlying state or component can be accessed
independently. Require direct public access. Private state is justified only
when one complete multi-statement operation preserves an invariant or required
ordering. Do not apply this check to semantic codecs or serialization adapters.

### Repository patterns

- Flag a struct or function that grew to two or more `bool` members or
  parameters in this change to use `common::Flags<EnumType>` instead (root
  `AGENTS.md` Key Pattern). This is a hard flag, not a suggestion.
- Flag a new standard-library or third-party `#include` added to a PCH-backed
  `.h`/`.cpp`; it belongs in `Common/ExternalHeaders.h`, not the individual
  source file (root `AGENTS.md` Key Pattern).

### Minimality and duplication

Flag incomplete integration and reachable edge failures. Flag overbuilt code
only when the change adds an unused option, one-use indirection with no required
contract, or speculative path with no current consumer. Flag substantial new
near-copies or repeated multi-condition logic after proving an existing helper
fits. Deliberate client/server and collection mirrors remain parallel.

Exclude micro-simplifications, style preferences, naming, header placement
other than the required `Common/ExternalHeaders.h` rule above, formatting, and
general documentation checks.

## External Claim Packet

Emit one proposition per packet; the caller routes it through
`/verify-external-claims`:

```markdown
### API Verification Request
- API/symbol/rule: <one external rule>
- Proposition: <exact statement that must be true or false>
- Applicability: <version, target, feature/extension, compile mode, and local evidence>
- Candidate official source: <direct official URL/section, or exact source needed>
- Dependent candidate finding: <path:line, failure, and why the verdict matters>
```

Pending verification makes the review `NEEDS_ACTION`; it is not a confirmed
finding until the caller receives `VERIFIED` evidence.

## Output

Order findings by impact. `Critical` means data loss, broken functionality,
determinism failure, or an equivalent contract breach; every other reported
finding is `Required`. Omit empty optional sections.

```markdown
## C++ Review Results

### Findings
- `path:line` — **Critical | Required:** <reachable failure, evidence, smallest correction>

### API Verification Requests
<atomic packets>

### Size Observations
- `path` (`N bt-token-v1`) — <cohesive split and why it is a manager follow-up candidate>

### Files Reviewed
- `path` — <regions and affected paths traced>

### Recommendation
PASS | NEEDS FIXES | BLOCKED

Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: none
Functions/regions touched: none
Decisive checks: <reads, searches, traces, and measurements>
Project membership trigger: /update-vcxproj — <paths/reason> | none
Build required: none
Residuals: <pre-existing defect, incomplete trace, pending external verdict, or none>
```

For a clean review, state `PASS — no issues found`, list the evidence and files,
and keep the unchanged footer. Never return `LGTM` without decisive trace
evidence.
