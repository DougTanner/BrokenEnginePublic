---
name: code-review
description: Reviews C++ code changes for bugs, implementation correctness, and simplification opportunities. Use this skill after making code changes as part of the C++ code change workflow (step 5).
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

#### Frame Phase Separation

If modifying frame update code:
- **Interpolate phase**: GPU-interpolated state only (positions, rotations, scales)
- **PostRender phase**: Non-interpolated state (flags, IDs, game logic, velocities)
- `AllocateAndCopy()` must be called before `Update()` logic accesses current frame data
- Verify modifications happen in the correct phase

#### RAII Compliance

- No manual `delete` or `free` calls anywhere
- VMA allocator used for Vulkan memory (not malloc)
- GPU resources wrapped in RAII classes (Buffer, Texture, Pipeline, CommandBuffer)
- Use `common::AlignedUniquePtr` for 64-byte aligned allocations (SIMD data)

### 4. Verify Determinism (Replay-Sensitive Code)

If the code affects game state that participates in replay:
- CRC calculations updated if any serialized state was modified
- Use `common::RandomEngine` for RNG (never `rand()`, `std::rand()`, or unseeded `std::mt19937`)
- No platform-specific operations in replay code path
- No wall-clock time dependencies (use frame delta time instead)

### 5. Check Common Library Usage

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

### 6. File Size Check

For each modified `.cpp` file, check its total line count:
- **Over 1000 lines**: Always flag as **REQUIRED** — `/reduce-file` must be invoked on this file
- **500-1000 lines**: Only flag as **RECOMMEND** if you identified a natural split point during the review (e.g., distinct responsibility groups, client/server code that could separate, utility functions that belong in a `*Utils` file). Do not flag files in this range that are cohesive and have no obvious split
- **Struct splitting**: Structs with static methods (e.g., SOA collections) can be split across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). Classes must NOT be split this way — extract independent classes instead. See `/reduce-file` skill

### 7. Implementation Assessment

Evaluate the changes holistically:
- **Completeness** - Does the implementation fully address the user's request?
- **Integration** - Were all callers, related systems, and edge cases updated?
- **Minimality** - No unnecessary refactoring, extra features, error handling, or cosmetic changes beyond what was requested.
- **Simplification** - Could any duplicated code be extracted, over-complicated algorithms be simplified, or unnecessary intermediate variables be removed?

### 8. Function Size and Nesting

Check modified functions for size and nesting as related code smells — deeply nested code often signals a function doing too much:
- **Function size**: Aim for 50-100 lines max per function. Soft guideline — some functions are legitimately large
- **Nesting depth**: Flag functions where nesting depth makes the logic hard to follow
- Recommend **inversion** (early return / guard clauses) as the preferred fix: flip conditions and return/continue early for error or edge cases, keeping the happy path at the lowest indentation
- Only recommend **extraction** when there is a natural split — the extracted block must represent a genuinely independent responsibility. Do NOT recommend extracting small blocks just to reduce indentation; this trades visible nesting for call-stack nesting, which is equally hard to follow

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

### Function Size / Nesting Issues
- file:line - Description of issue and suggested inversion or extraction

### Implementation Issues
- [Description of completeness, integration, minimality, or simplification concern]

### Recommendation
[PASS / NEEDS FIXES]
Brief summary of overall assessment.
```

If no issues were found in any category, output only the Files Reviewed list and "PASS — no issues found."
