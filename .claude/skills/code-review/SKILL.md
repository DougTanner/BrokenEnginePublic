---
name: code-review
description: Reviews C++ code changes for bugs, implementation correctness, and simplification opportunities. Use this skill after making C++ code changes to catch logic errors and correctness issues.
allowed-tools: [Read, Grep, Glob]
---

# Code Review

Reviews C++ code changes for correctness, bugs, and adherence to Broken Engine patterns. This skill focuses on **logic and correctness**, not formatting or style.

## Instructions

### 1. Identify Modified Code

Review the conversation history to find all files that were edited during this session. Focus on:
- New functions/methods added
- Modified logic in existing functions
- New data structures or classes
- Integration points where new code connects to existing systems

### 2. Review for General Bugs

Check each modified section for common issues. Note: the project assumes parameters to functions are valid (no defensive null checks or validation needed), so only flag pointer/bounds issues where data comes from external sources (file I/O, network, user input).

- **Uninitialized variables** - Are all variables initialized before use? Check struct members especially.
- **Array bounds** - Are all array/vector accesses within valid ranges?
- **Resource leaks** - Are resources managed via RAII? No manual `new`/`delete` or `malloc`/`free`.
- **Logic errors** - Does the control flow match the intended behavior? Check loop conditions and early returns.
- **Type mismatches** - Are conversions between types correct? Watch for narrowing conversions.
- **Math errors** - Are calculations correct? Watch for integer division, floating point precision, and sign issues.

### 2b. Memory & Allocation Discipline (Broken Engine specific)

The main loop runs under an allocation tracker that `DEBUG_BREAK()`s on heap allocations. Flag:

- **Local `std::vector` / `std::string` in hot paths** — must use `gpThreadLocal->mWorkbuffer` instead (see `Common/CLAUDE.md`).
- **Heap allocation in main loop without `ScopedSuppressAllocationTracking`** — any unavoidable heap use needs the guard plus a `// Heap:` comment justifying it (see `Engine/Source/Memory/CLAUDE.md`).
- **Allocating `LOG` format specs** — flag any `LOG(...)` containing a float format spec such as `{:.Nf}`, `{:e}`, `{:g}`, width/precision like `{:>10}`, `{:#x}`, or `std::format`/`std::format_to`/`std::to_string`/`std::ostringstream` anywhere. These go through heap-allocating `std::format` paths and trip the allocation tracker. Require pre-building via `common::ScopedWorkbufferBuilder` on `gpThreadLocal->mWorkbuffer` (`Append` / `AppendFloat`) and emitting with `LOG(cat, lvl, "{}", builder)`. Plain `{}` on integers and the named formatters in `Common/LogFormatters.h` (XMVECTOR, Flags, chrono durations, paths, etc.) are safe.
- **Standard-library header placement** — new `#include <std>` in a `.h`/`.cpp` should move to `Common/ExternalHeaders.h`.

### 3. Verify Broken Engine Patterns

#### Collection Integrity

If any collection structure was modified (adding/removing members), verify completeness:

**Engine collections** (e.g., `AreaLightsInterpolate`) require **2 steps**:
1. Macro list updated (e.g., `AREA_LIGHTS_INTERPOLATE_LIST(a)`)
2. Equality operator updated (`operator==` with `common::BreakOnNotEqual`)

**Game collections** (e.g., `BlastersPostRender`, `SpaceshipsInterpolate`) require **5 steps**:
1. Macro list updated
2. Equality operator updated
3. Load from previous frame in `Update()` loop
4. Save to current frame in `Update()` loop
5. Initialize in `Spawn()` function

**Version constants**: If collection structure changed, increment the version constant for save file compatibility.

#### Manager Patterns

If modifying or adding managers:
- Global pointers use `gp*` naming convention (e.g., `gpGraphics`, `gpAudioManager`)
- Managers created in correct initialization order (check `Main.cpp`)
- No circular dependencies between managers

#### Engine → Game Access

Engine code must access game functionality through `game::gpGame` (the game-derived singleton), never through Base-class globals or direct game-type references. Flag direct use of game-specific enums, collections, or types from inside `Engine/Source/**`.

#### Client/Server Guard Scope

- Flag file-wide `#if defined(BT_CLIENT)` / `BT_SERVER` wrappers where a single function or block guard would suffice. Narrow is better.
- For collections with client-only fields, verify the `SharedMembers()` + `ClientMembers()` + `std::tuple_cat` pattern is used. Server build's `Members()` returns `SharedMembers()` only.
- If a file becomes fully wrapped in a `BT_` guard that it wasn't before, it must be removed from the opposite vcxproj (see §4 below).

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

### 4. Verify vcxproj Inclusion

If new `.cpp` files were created during this session, verify they are added to the correct vcxproj:
- If the file is fully wrapped in `#if defined(BT_CLIENT)` — must be in client vcxproj only, NOT in server vcxproj
- If the file is fully wrapped in `#if defined(BT_SERVER)` — must be in server vcxproj only, NOT in client vcxproj
- If the file has shared code (no guard or partial guards) — must be in both vcxproj files

If an existing file was changed to be fully wrapped in a `BT_` guard that it wasn't before, flag that it should be removed from the opposite vcxproj.

### 4b. Verify vcxproj.filters Paths

If files were added to `.vcxproj.filters` during this session, verify the filter path mirrors the on-disk directory:
- `Engine/Source/<path>/File.h` must use filter `Engine\<path>` (backslash-separated)
- `Projects/BrokenEngineSandbox/Source/<path>/File.h` must use filter `Game\<path>`
- If a new subdirectory filter is needed, verify it was added to the filter definitions with a unique GUID
- Flag any file whose filter doesn't match its on-disk directory

### 5. Verify Determinism (Replay-Sensitive Code)

If the code affects game state that participates in replay:
- CRC calculations updated if any serialized state was modified
- Use `common::RandomEngine` for RNG (never `rand()`, `std::rand()`, or unseeded `std::mt19937`)
- No platform-specific operations in replay code path
- No wall-clock time dependencies (use frame delta time instead)
- **Multithreading**: inside `gpMultithreading->Dispatch()` lambdas, no write to shared state without per-thread accumulators + reduce. Floating-point reductions must preserve order.
- **Phase ordering**: reads in Update phase must not depend on values written later in PostRender. Interpolate↔PostRender boundaries enforce this at the collection level — verify any new field lives in the correct phase.

### 6. Check Common Library Usage

Verify that existing utilities are used instead of reimplementing:

**String & Hash**:
- `common::Crc(std::string_view)` - compile-time string hashing for asset IDs
- `common::Crc<T>(const T&)` - hash trivially copyable types by bytes

**Memory**:
- `common::AlignedUniquePtr<T>` - 64-byte aligned RAII memory for SIMD
- `common::MakeAligned<T>(count)` - factory function for aligned allocation

**Binary I/O**:
- `common::Read(stream, value)` / `common::Write(stream, value)` - handles reinterpret_cast

**Math**:
- `common::Distance(vec1, vec2)` - 3D distance calculation
- `common::DirectionTo(from, to)` - normalized direction vector
- `common::RoundUp<T>(value, multiple)` - integer rounding up

**Colors**:
- `common::ColorToVector(uint32_t)` - packed RGBA to XMVECTOR
- `common::ColorLerp(color1, color2, t)` - interpolate packed colors

**Type Safety**:
- `common::Flags<ENUM>` - type-safe bitfield wrapper with Set/Clear/Toggle

### 7. File Size Check

For each modified `.cpp` file, check its total line count:
- **Over 1000 lines**: Always flag as **REQUIRED** — `/reduce-file` must be invoked on this file
- **500-1000 lines**: Only flag as **RECOMMEND** if you identified a natural split point during the review (e.g., distinct responsibility groups, client/server code that could separate, utility functions that belong in a `*Utils` file). Do not flag files in this range that are cohesive and have no obvious split
- **Struct splitting**: Structs with static methods (e.g., SOA collections) can be split across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). Classes must NOT be split this way — extract independent classes instead. See `/reduce-file` skill

### 8. Implementation Assessment

Evaluate the changes holistically:
- **Completeness** - Does the implementation fully address the user's request?
- **Integration** - Were all callers, related systems, and edge cases updated?
- **Minimality** - No unnecessary refactoring, extra features, error handling, or cosmetic changes beyond what was requested.
- **Function size**: Aim for 50-100 lines max per function. Soft guideline — some functions are legitimately large. If a modified function has grown past this, flag with "function does too much" and recommend a split only if a natural responsibility boundary exists.

For micro-simplification opportunities (duplicated snippets, unnecessary intermediate variables, over-complicated expressions), recommend running `/simplify` on the changed files rather than listing them here — that skill owns surface-level simplification. For nesting-depth / style complaints, `/code-style-review` owns those.

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

### Recommendation
[PASS / NEEDS FIXES]
Brief summary of overall assessment.
```

If no issues were found in any category, output only the Files Reviewed list and "PASS — no issues found."
