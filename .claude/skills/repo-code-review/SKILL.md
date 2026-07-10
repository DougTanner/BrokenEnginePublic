---
name: repo-code-review
description: Reviews C++ code changes made this session for bugs, correctness, and Broken Engine pattern violations — XMVECTOR W invariants, allocation-tracker / LOG formatting discipline, useless-ASSERT discipline, collection integrity, determinism, client/server guard scope and affinity. Use after any C++ code change, when the user says "review my changes", "check my code", "code review", or before declaring an implementation complete. Flags oversized files for /reduce-file. Logic and correctness only — formatting/style belongs to code-style-review.
allowed-tools: [Read, Grep, Glob, WebFetch]
---

# Code Review

Reviews this session's C++ changes for **logic and correctness** — formatting/style is owned by `/code-style-review`.

## Instructions

### 1. Identify Modified Code

If invoked as a subagent, use the changed-file list, touched functions/regions (they scope the §2c/§2d "added this session" checks), and any focus areas from the caller's prompt. Otherwise review the conversation history to find all files that were edited during this session. Focus on:
- New functions/methods added
- Modified logic in existing functions
- New data structures or classes
- Integration points where new code connects to existing systems

### 2. Review for General Bugs

Check each modified section for: uninitialized variables (struct members especially), array/vector bounds, resource leaks (RAII everywhere — no manual `new`/`delete`/`malloc`/`free`), control-flow logic errors (loop conditions, early returns), narrowing conversions, and math errors (integer division, float precision, sign).

The project assumes parameters from within the codebase are valid — do not flag missing null checks or validation between our own functions. Only flag pointer/bounds issues on data from external sources (file I/O, network, user input).

### 2b. Memory & Allocation Discipline (Broken Engine specific)

The main loop runs under an allocation tracker that `DEBUG_BREAK()`s on heap allocations. Flag:

- **Local `std::vector` / `std::string` in hot paths** — must use `gpThreadLocal->mWorkbuffer` instead (see `Common/CLAUDE.md`).
- **Heap allocation in main loop without `ScopedSuppressAllocationTracking`** — any unavoidable heap use needs the guard plus a `// Heap:` comment justifying it (see `Engine/Source/Memory/CLAUDE.md`).
- **Allocating `LOG` format specs** — in allocation-tracked code only (Game and Engine; the offline DataPacker has no allocation tracker — do not flag it), flag any `LOG(...)` containing a float format spec such as `{:.Nf}`, `{:e}`, `{:g}`, width/precision like `{:>10}`, `{:#x}` (integer specs follow the same scope), or `std::format`/`std::format_to`/`std::to_string`/`std::ostringstream`. These go through heap-allocating `std::format` paths and trip the allocation tracker. Correct forms:
	- Wrap each float arg with `common::Wb(value, precision)` and each `XMVECTOR` arg with `common::WbV2`/`WbV3`/`WbV4` (logs 2/3/4 lanes — pick by needed fidelity); the placeholder stays `{}`.
	- For loop/lambda-driven content, pre-build via `common::ScopedWorkbufferArena builder = rWorkbuffer.Push(); builder.Append(...)`/`AppendFloat(...)` and emit as `LOG(cat, lvl, "{}", builder)` — the arena has its own `std::formatter` (emits `View()`), so no call-site `.View()` is needed.
	- Plain `{}` on integers and the named formatters in `Common/Log/LogFormatters.h` (XMVECTOR, Flags, chrono durations, paths, etc.) are safe.
- **Standard-library header placement** — new `#include <std>` in a `.h`/`.cpp` should move to `Common/ExternalHeaders.h`.

### 2c. ASSERT Discipline (Broken Engine specific)

`ASSERT` is active in **all** build configs and throws `std::runtime_error` on failure (`common::Assert`, `Common/ErrorUtils.cpp`) — it is not a debug-only no-op. A failing ASSERT *is* a crash; on a bare `std::thread` (loading thread, upload thread) the throw is worse than the fault it replaces: uncaught → `std::terminate`, bypassing the `HandleException` crash-report path.

**Flag any ASSERT added this session that merely throws one line before the code would crash anyway** (the classic agent anti-pattern). Test: if the ASSERT were deleted, what happens on the failing path?

- **Immediate fault at the same spot** (null dereference, OOB fault) → the ASSERT is useless; it adds a false impression of safety. Require removal or replacement per the ladder below.
- **Silent wrong behavior surfacing far away** (garbage texels, CRC desync, corrupted save, wrong-but-plausible value) → the ASSERT has real diagnostic value; keep it. Example: the zlib `uncompress` result check in `FileManager::LoadChunk` — without it a corrupt payload renders garbage with no fault.

Resolution ladder for a flagged ASSERT, in order of preference:

1. **Make the condition impossible in calling code.** Fix the caller or establish the invariant at the source — e.g., validate external data once at the trust boundary where it enters (file read, network receive), so downstream code needs no check at all.
2. **Recover / handle gracefully** if the condition is genuinely reachable (external data): `LOG(kError)` naming the bad input, `DEBUG_BREAK()` (debugger-only, no release crash), then fail through the function's *existing* failure channel — return false, skip the item, mark-ready-with-zeroed-data, etc. Never throw, and never trade the crash for a hang: check for condition-variable waiters that need notifying and loops that stop progressing (e.g., a 0-byte `ReadFile` in a `while (copied < size)` loop).
3. **Plain not-null ASSERTs** guarding an immediate dereference: delete them — the null-dereference crash is equally immediate and equally diagnosable in a debugger/crash dump.
4. **Static analyzer fallout** is handled case-by-case: if the analyzer's path is genuinely reachable, add a real (analyzer-visible) guard per step 2; if it is provably impossible, suppress with `NOLINT(clang-analyzer-...)` plus a comment stating the invariant. Note `ASSERT` never silences clang-analyzer — its `_Analysis_assume_` is MSVC-only and `common::Assert` lives in another TU — so adding an ASSERT to appease the analyzer is doubly useless.

### 2d. Changelog / Edit-History Comments (Broken Engine specific)

Comments explain what the current code does and why — never what it used to do or what a change did. This repo keeps no changelogs in source; git history is the forensic record. Flag any comment **added or edited this session** that narrates edit history instead of present-state rationale — require removal, or rewording to a present-tense reason when the comment carries a real one.

Telltale patterns to flag:
- Session/process narration: "this session", "this run", "added this session", "landed", "now-landed", "has been removed", "dropped", "as part of the X refactor".
- Before/after narration: "was X", "used to be", "previously", "changed from/to", "renamed from", "moved from", "replaces the old …", "the deleted / now-removed `<symbol>`", and "no longer" when it recounts a past edit rather than a current condition.
- Dated or commit-referenced notes: "as of <date/version>", parenthetical dates, commit hashes, "// TODO(2024): removed …".

Not a finding: a comment stating a current invariant, gotcha, or still-relevant bug/driver workaround — even one using "never"/"no longer" for a present condition (e.g. "the thread is no longer running at this point"). Test: does the sentence describe present behavior, or narrate an edit? Attributions to external sources/papers are also fine.

Reword, don't just delete, when the comment carries a real reason — keep the rationale, drop the history: "eliminates the duplicate sum the two passes used to evaluate via the now-removed GerstnerLow/Medium helpers" → "eliminates the duplicate sum the two passes would otherwise each evaluate".

The same no-changelog rule applies to any CLAUDE.md, plan, or other doc this session touched — flag stray "we did this" narration wherever this session introduced it.

### 3. Verify Broken Engine Patterns

#### Collection Integrity

If a collection struct gained, lost, or changed an SOA member pointer, verify against the `add-collection-member` skill's checklist (authoritative). Spot-check the high-failure steps:

1. Pointer in the correct tuple: `SharedMembers()` (cross-build), `ClientMembers()` (client-only, inside `#ifdef BT_CLIENT`), or `Members()` (unsplit collections). A member missing from the tuple corrupts memory — allocation, CRC, serialization, and swap-and-pop all iterate the tuple.
2. `AllocateAndCopy()` memcpy added for members that persist across frames (those not handled by the Update load/save pattern).
3. Update-loop load/save is unconditional — a save skipped by an early-exit branch leaves uninitialized memory and breaks determinism.
4. Initialized in `Spawn()` / `Add()`; sync-pattern collections must zero-init all Interpolate fields in `Add()`.
5. `LogDifferences()` updated for shared members; `Transfer()` data updated if the collection supports cross-cell transfer.
6. `kiVersion` bumped if the serialization layout changed (the collection's own constant, plus `Frame::kiVersion` for game collections) — old save files fail to load otherwise.

#### Manager Patterns

If modifying or adding managers:
- Global pointers use `gp*` naming convention (e.g., `gpGraphics`, `gpAudioManager`)
- Managers created in correct initialization order (check `Main.cpp`)
- No circular dependencies between managers

#### Engine → Game Layering

Engine→game access is sanctioned by design — do **not** flag engine code reading `game::gp*` globals, calling `game::` static functions, or referencing game types/constants/compile-time symbols (see `Engine/Source/CLAUDE.md` → Hub Conventions). The real violation is the reverse direction: engine *types* naming game concepts — e.g. an `engine::PacketType` enumerator only the game uses, or an engine class `friend`-ed to a game class. Flag only those. When engine code uses a Base-class global where a game-derived one exists (`GameBase` vs `game::gpGame`, `CameraBase` vs `Camera`), flag it — prefer the game version.

#### Client/Server Guard Scope

- Flag file-wide `#if defined(BT_CLIENT)` / `BT_SERVER` wrappers where a single function or block guard would suffice. Narrow is better.
- For collections with client-only fields, verify the `SharedMembers()` + `ClientMembers()` + `std::tuple_cat` pattern is used. Server build's `Members()` returns `SharedMembers()` only.

#### Flags over Multiple Booleans

If a struct or function grew to 2+ `bool` members/parameters in this change, flag it — use `common::Flags<EnumType>` instead (see `Common/CLAUDE.md`). This is a hard flag, not a suggestion.

#### Frame Phase Separation

If modifying frame update code:
- **Interpolate phase**: GPU-interpolated state only (positions, rotations, scales)
- **PostRender phase**: Non-interpolated state (flags, IDs, game logic, velocities)
- `AllocateAndCopy()` must be called before `Update()` logic accesses current frame data
- Verify modifications happen in the correct phase

#### XMVECTOR W Invariant

Every constructed or returned `XMVECTOR` must carry the right W lane for its role (core rule in root `CLAUDE.md` → Key Patterns → "XMVECTOR W invariant"). Failure is silent: `XMVector3Normalize` divides all four lanes by the 3D length, `XMVectorMultiplyAdd` propagates all four lanes, `XMVectorSetZ` leaves W untouched — a stray W survives every "3D" op and accumulates across ticks. Flag these as **bugs**, not style:

- **`XMVectorSet(x, y, z, W)` with wrong W for the value's role.** Position → `1.0f`. Direction / velocity / normal / axis / offset-added-to-position / color-alpha-meant-to-be-transparent → `0.0f`. Opaque color alpha → `1.0f`.
- **Function return or out-param with wrong W.** Inspect every `XMVECTOR`-returning function and every `XMVECTOR*` out-param added/modified: does *every* code path (including early returns, A*-miss, empty-input) write a correct-W value? Out-params should be initialized at function entry with a valid-W default so no path leaks an uninitialized or wrong-W value.
- **Consumer-side `XMVectorSetW(..., 1.0f)` laundering a producer bug.** If you see a consumer defensively stamping W right after reading a function's return or out-param, flag the **producer** — that's the real bug. Defensive stamps are anti-patterns; the only legitimate `SetW` is the `W=1.0` clamp after a `MultiplyAdd`-based position integration.
- **Subtract-then-normalize where operands have mismatched W.** `XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(a, b), 0.0f))`: if `a` and `b` are both positions (`W=1`), the difference has `W=0` naturally. If one has `W=0` and the other has `W=1`, the difference has `W=±1`, and `Normalize` scales it to `W ≈ ±1/|xyz|` which poisons downstream consumers. Trace both operands' W origins.
- **Inheriting W from input via `XMVectorGetW(input)` in a return value.** Silent propagation of whatever W the caller passed. Emit an explicit literal matching the output's role.
- **Offset constants added to positions built with `W=1.0`.** Offsets added via `XMVectorAdd` to a W=1 position must themselves be W=0; a W=1 offset produces a W=2 result.

If `common::ValidateVector<IS_POSITION>()` was added, removed, or moved, verify it's present at every Collection spawn / transfer boundary touched by the change.

#### RAII Compliance

- No manual `delete` or `free` calls anywhere
- VMA allocator used for Vulkan memory (not malloc)
- GPU resources wrapped in RAII classes (Buffer, Texture, Pipeline, CommandBuffer)
- Use `common::AlignedUniquePtr` for 64-byte aligned allocations (SIMD data)

### 4. Flag Guard-Affinity Changes

vcxproj membership/filter mechanics belong to the `update-vcxproj` skill (process step 7) — never grep the project XML here. This review owns affinity only:

- Flag the required affinity (client-only / server-only) of any file created this session that is fully wrapped in `#if defined(BT_CLIENT)` / `BT_SERVER`, and any existing file that gained or lost a file-wide guard (its membership must change).
- Exception: guardless engine files may be client-only by design via client-vcxproj membership + the `Engine.h` BT_CLIENT aggregation span (root `CLAUDE.md` → Client/Server Targets) — check before flagging.

### 5. Verify Determinism (Replay-Sensitive Code)

If the code affects game state that participates in replay:
- CRC calculations updated if any serialized state was modified
- Use `common::RandomEngine` for RNG (never `rand()`, `std::rand()`, or unseeded `std::mt19937`)
- No platform-specific operations in replay code path
- No wall-clock time dependencies (use frame delta time instead)
- **Multithreading**: inside `gpMultithreading->Dispatch()` lambdas, no write to shared state without per-thread accumulators + reduce. Floating-point reductions must preserve order.
- **Phase ordering**: reads in Update phase must not depend on values written later in PostRender. Interpolate↔PostRender boundaries enforce this at the collection level — verify any new field lives in the correct phase.

### 6. Check Common Library Usage

Verify existing `common::` utilities are used instead of reimplementing. `Common/CLAUDE.md` is the authoritative, current catalog — Read it and check the changed code against it. High-frequency offenders: hashing (`common::Crc` family, `CrcConsteval` where compile-time evaluation must be guaranteed), aligned SIMD memory (`AlignedUniquePtr`/`MakeAligned`), binary stream I/O (`common::Read`/`Write`), math helpers (`Distance`, `DirectionTo`, `RoundUp`), packed-color conversion/lerp (`ColorToVector`, `ColorLerp`), and `common::Flags<ENUM>` over raw bools/bitfields.

### 7. File Size Check

For each modified `.cpp` file, check its total line count:
- **Over 1000 lines**: Always flag as **REQUIRED** — the file needs `/reduce-file`. The caller routes flagged files to a follow-up plan in `Documents/Plans/`; the split is never run inline during a review
- **500-1000 lines**: Only flag as **RECOMMEND** if you identified a natural split point during the review (e.g., distinct responsibility groups, client/server code that could separate, utility functions that belong in a `*Utils` file). Do not flag files in this range that are cohesive and have no obvious split
- **Struct splitting**: Structs with static methods (e.g., SOA collections) can be split across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). Classes must NOT be split this way — extract independent classes instead. See `/reduce-file` skill

### 8. Implementation Assessment

Evaluate the changes holistically:
- **Completeness** - Does the implementation fully address the user's request?
- **Integration** - Were all callers, related systems, and edge cases updated? Integration-missed tripwires — grep, don't trust the diff narrative:
	- Changed function semantics (units, W convention, frame phase, ownership, a default) → grep every caller; the diff shows only the call sites the implementer remembered.
	- Mirrored patterns half-applied: client edit without server counterpart, per-collection pattern applied to N−1 of N collections, C++ struct changed without its shared GLSL header (and vice versa), Spawn updated but Transfer/AllocateAndCopy/LogDifferences not.
	- New enum value → grep every switch/dispatch/serialization table over that enum.
	- Anything renamed → grep comments, CLAUDE.md, plans, and shared headers for the old name.
- **Minimality** - No unnecessary refactoring, extra features, error handling, or cosmetic changes beyond what was requested. Flag over-built code added this session: an abstraction (base class, template, callback, indirection layer) with exactly one implementation/user and no second on the horizon; a config value, parameter, or option that never varies at any call site; speculative "for later" scaffolding no current code path exercises; reimplementation of an existing `common::` or stdlib facility (§6 owns the `common::` catalog check).
- **New duplication** - Flag (required) when the diff introduces a near-copy (~5+ lines, or a repeated multi-condition check) of logic that already exists in the repo — verify by grepping a distinctive fragment of each substantial new block; require calling or extracting a shared helper. Exception: deliberate mirrored patterns (client/server pairs, per-collection boilerplate) stay parallel — do not recommend abstracting them.
- **Workaround justification test** - A workaround that needs a paragraph-long comment to justify why it is OK is itself a required finding: the code is wrong — require fixing the underlying code, not accepting the justification.
- **Function size**: Aim for 50-100 lines max per function. Soft guideline — some functions are legitimately large. If a modified function has grown past this, flag with "function does too much" and recommend a split only if a natural responsibility boundary exists.

For micro-simplification opportunities (unnecessary intermediate variables, over-complicated expressions), recommend running `/simplify` on the changed files rather than listing them here — that skill owns surface-level simplification; duplication of existing repo logic is the §8 New-duplication check, not a `/simplify` punt. For nesting-depth / style complaints, `/code-style-review` owns those.

### 9. Severity Prefixes

Use these prefixes on findings so the author knows what blocks the change vs what is optional. Items without a prefix are **required** (must address):

- *(no prefix)* — Required change. Must address.
- **Critical:** — Blocks the change. Data loss, broken functionality, determinism break, allocation tracker violation.
- **Nit:** — Minor, optional. Author may ignore — naming preferences, micro-style.
- **Optional:** / **Consider:** — Suggestion worth considering but not required.

Marker equivalence: `✗` in Engine Pattern Issues = no-prefix (required); file-size `[REQUIRED]` = no-prefix; file-size `[RECOMMEND]` = **Optional:**. Use one scheme per finding, not both.

### 10. Honesty (Anti-Sycophancy)

- **Don't rubber-stamp.** "LGTM" without evidence of review helps no one.
- **Don't soften real issues.** "This might be a minor concern" when it's a bug that will hit production is dishonest.
- **Quantify problems when possible.** "This will allocate ~200 bytes per frame and trip the allocation tracker" beats "this could be slow."
- **Push back on approaches with clear problems.** Sycophancy is a failure mode in reviews.

Severity calibration — worked examples (severity comes from the invariant surface hit, not the code pattern):

- Local `std::vector` in a once-at-startup load function: not a finding — the allocation tracker covers the main loop only. The same vector in a per-tick `Update()`: **Critical:** (tracker `DEBUG_BREAK()`).
- `==` float compare in Interpolate-only visual code: **Consider:** — outside the CRC, cannot desync. The same compare gating a write to PostRender (CRC'd) state: **Critical:** — divergent rounding across machines is a desync source.

### 11. API Verification

For non-obvious API calls — Vulkan 1.2 entry points (especially extensions), DirectXMath alignment-sensitive ops, C++23 features new to the project, third-party library calls used at fewer than ~3 existing call sites — verify against the official spec before accepting the call. This review normally runs inside a subagent, which does not spawn further subagents and should not pull large spec pages into its context: emit each needed check as an entry under `### API Verification Requests` in the output — the API/symbol, the spec URL, and exactly what to confirm plus which finding depends on it — and the caller dispatches Sonnet fetch subagents to resolve them. Use WebFetch directly only for a small targeted page (a single man-page-style entry), citing the URL or section in the review note.

- Vulkan 1.2 spec: https://registry.khronos.org/vulkan/specs/1.2-extensions/man/html/
- DirectXMath: https://learn.microsoft.com/en-us/windows/win32/dxmath/ovw-xnamath-reference
- C++23 / STL: https://en.cppreference.com/

Skip verification for STL basics, `XMVector3Normalize`-class staples, and patterns already used at multiple call sites in the engine — those are battle-tested. Training data contains outdated patterns that look correct but break against current versions.

If WebFetch turns up nothing authoritative, mark the finding:

> UNVERIFIED: I could not find official documentation for this pattern. This is based on training data and may be outdated. Verify before using in production.

## Output Format

Only include sections where issues were found. For sections with no issues, omit them entirely.

```
## Code Review Results

### Files Reviewed
- [list of modified files with paths]

### Bugs Found
- file:line - Description of bug and suggested fix

### Engine Pattern Issues
- ✗ [pattern name] - [details of what's wrong and how to fix]

### File Size Warnings
- file (N lines) - [RECOMMEND / REQUIRED] `/reduce-file <path>`

### Implementation Issues
- [Description of completeness, integration, minimality, or simplification concern]

### API Verification Requests
- <api/symbol> — <spec URL> — <what to confirm, and which finding depends on it>

### Recommendation
[PASS / NEEDS FIXES]
Brief summary of overall assessment.
```

If no issues were found in any category, output the Files Reviewed list and "PASS — no issues found." In every case, the caller-required residuals footer (C++ Code Change Process) is appended after this template — it is not optional even on a clean PASS.
