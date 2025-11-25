---
name: code-review
description: Reviews C++ code changes for bugs, implementation correctness, and simplification opportunities. Use this skill after making code changes as part of the C++ code change workflow (step 3). (project)
allowed-tools: [Read, Grep, Glob, Task]
---

# Code Review

Reviews C++ code changes for correctness, bugs, and adherence to Broken Engine patterns. This skill focuses on **logic and correctness**, not formatting or style (that's handled by code-style-review in step 4).

## When to Use

Invoke this skill as **step 3** in the C++ Code Change Process (see `/CLAUDE.md`), after:
- Making the requested code changes (step 1)
- Updating all affected locations in the codebase (step 2)

And before:
- Running code-style-review (step 4)
- Updating CLAUDE.md files (step 5)

## Instructions

### 1. Identify Modified Code

Review the conversation history to find all files that were edited during this session. Focus on:
- New functions/methods added
- Modified logic in existing functions
- New data structures or classes
- Integration points where new code connects to existing systems

### 2. Review for General Bugs

Check each modified section for common issues:

- **Null/invalid pointer access** - Are pointers checked or guaranteed valid before dereferencing?
- **Array bounds** - Are all array/vector accesses within valid ranges? Use `.at()` not `[]` for bounds checking.
- **Uninitialized variables** - Are all variables initialized before use? Check struct members especially.
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

### 6. Verify Implementation Completeness

Answer these questions:
- **Does the implementation address the user's request?** Is the functionality complete?
- **Were all integration points updated?** Check callers, related systems, and edge cases.
- **Are changes minimal?** No unnecessary additions beyond what was requested.

### 7. Check for Simplification Opportunities

Look for logic improvements (not style - that's step 4):
- **Duplicated code** that could be extracted into a function
- **Over-complicated algorithms** that could be simplified
- **Missing Common library usage** where utilities exist
- **Unnecessary intermediate variables** that add no clarity

### 8. Verify Minimal Changes Principle

Ensure changes follow minimal change philosophy:
- No unnecessary refactoring of surrounding code
- No additional features beyond what was requested
- No extra error handling or validation (per project directives)
- No cosmetic changes to unrelated code

## Output Format

Provide a structured review report:

```
## Code Review Results

### Files Reviewed
- [list of modified files with paths]

### Bugs Found
[List any bugs discovered, or "No bugs found"]
- file:line - Description of bug and suggested fix

### Engine Pattern Compliance
- ✓/✗ Collection integrity (if applicable) - [details]
- ✓/✗ Manager patterns - [details]
- ✓/✗ Frame phase separation - [details]
- ✓/✗ RAII compliance - [details]
- ✓/✗ Determinism (if replay-sensitive) - [details]

### Implementation Assessment
- ✓/✗ Fully addresses user request - [details]
- ✓/✗ All integration points updated - [details]

### Simplification Opportunities
[List any suggestions, or "None identified"]
- Description of opportunity and location

### Minimal Changes Check
- ✓ Changes are minimal and focused
- or: List unnecessary additions that should be removed

### Recommendation
[PASS / NEEDS FIXES]
Brief summary of overall assessment.
```
