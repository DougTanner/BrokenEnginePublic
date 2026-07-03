# Common Primitives Hardening Batch

## Context

2026-07-03 review sweep of `Common/` determinism/threading/serialization primitives. No shipped-path desync bugs found; this batch closes the **latent-hazard class** the sweep surfaced — sharp edges in repo-wide-blast-radius primitives where a future caller compiles silently and desyncs/corrupts. All items verified against current code; each is small and mechanical.

## Design

**Determinism / math (`Common/`):**

1. **`Determinism.h:9-12` contract comment**: the header claims "bitwise (non-epsilon) equality" but `XMVector4Equal`/member `==` are IEEE (`-0.0 == +0.0` true, NaN never equal) — while `Crc()` distinguishes `±0.0` and unifies bit-identical NaNs. `LogDifference` already bypasses these operators with `memcmp` for exactly this reason. Reword to "IEEE (non-epsilon) equality — NOT bitwise: ±0 compare equal but CRC differently; NaN never compares equal; CRC identity requires memcmp (see LogDifference)". Comment-only.
2. **`Crc.h:125-130` single-arg `Crc(const T&)` accepts raw pointers** and hashes the 8 address bytes — a `Crc(pFoo)` typo compiles and, if it ever reached the tick CRC chain, desyncs instantly (address-dependent). Zero current misuses (swept). Add `&& (!std::is_pointer_v<std::remove_cvref_t<T>>)` to the constraint; the intentional case stays on the explicit pointer+count overload.
3. **`ConvexHull.h:70-91` `ConvexHullsOverlap` zero-length-edge axis**: duplicate consecutive vertices produce a `(0,0)` SAT axis whose projections make `fMaxA <= fMinB` true → hull reports "separated" against everything. Structurally unreachable from the current producer (`BuildValidAreaHull` monotone chain drops collinear points; pixel-quantized vertices can't collapse under rotation rounding at world scale — verified) but the function is a shared primitive. Skip axes where both components are `0.0f` (two-line guard, deterministic).
4. **`ConvexHull.h:106-116` `IsPolygonCcw`** accumulates a float shoelace on absolute coordinates — wrong verdicts for small polygons at km-scale offsets. Both current callers pass island-local (small) coords, so unreached. Make it scale-robust and still deterministic: shoelace relative to vertex 0 (`(a−v0)×(b−v0)`) — identical basic-IEEE ops on both sides. NavData is derived per-cell data outside the CRC, rebuilt identically by both sides from the same code, so a same-binary change is determinism-safe (a client/server *version-skew* concern would only exist if NavData were compared across builds — it is server-built and wire-shipped).
5. **`MathUtils.h:202-206` `ExponentialInterpolant`** lacks its sibling's clamp: `x=inf` → NaN, near-`FLT_MAX/2` → `+inf` (doc says never exceeds 1.0), `x=-2` divides by zero. Unreachable at fixed dt with sane rates. Mirror `ExponentialDecay`: clamp result to `[0, 1]`.
6. **`Random.h:15` 32-bit seed ctor vs doc**: `FloatingPointDeterminism.txt:89` claims "explicit 64-bit seeding"; ctor takes `uint32_t` (splitmix64 natively takes 64-bit input). Add a `uint64_t` ctor (same splitmix64 path, no truncation), mark both `explicit` to block silent narrowing, fix the doc line. Existing 32-bit callers (`SeedFromGridCoord` pre-mixes deliberately) unchanged — no stream change for existing seeds.

**Exception handling (`Common/Determinism.cpp`):**

7. **`:95-99` `DBG_PRINTEXCEPTION_C`** dereferences `ExceptionInformation[1]` without checking `NumberParameters >= 2` — a malformed third-party `RaiseException` faults inside the handler while holding its mutex. Exception records are OS/third-party input (trust boundary). Guard on `NumberParameters >= 2` and non-null.
8. **`:114-136` `EXCEPTION_STACK_OVERFLOW`** runs `LOG` + a DbgHelp stack walk on the exhausted stack → re-overflow, losing the report. Special-case: emit a fixed-string log only, skip the walk.

**Threading (`Common/Threading/`):**

9. **`Multithreading.h:38-56` nested-`Dispatch` hardening**: re-entry from a worker or the main-thread remainder range (a) races the plain-`bool` `mbDispatched`, (b) the contract ASSERT throwing inside the Wake loop unwinds past already-woken workers — exactly the running-against-unwound-stack hazard the drain exists to prevent. No nested call site exists today. Detect re-entrancy up front (e.g. an `mbInDispatch` flag ASSERTed before any Wake) so misuse fails before a worker wakes.
10. **`Multithreading.h:74-87` drain loop** silently discards every worker exception after the first. `LOG(kDefault, kError, ...)` the dropped exception's `what()` before discarding.

**Serialization / data-format guards:**

11. **`Serialization.h:96-128` `std::vector<T>` Write/Read** lack the `static_assert(std::is_trivially_copyable_v<T>)` the pointer overloads carry — `Write(vector<std::string>)` would compile and serialize heap pointers. Add the assert to both. Rider: `DifferenceStream.h`'s trivially-copyable branch raw-writes `std::tuple<int64_t, T>` (unpinned layout + inter-member padding) — dead today (`FrameInput` is non-TC); replace with two separate writes or a plain struct while in the area.
12. **`DataFile.h` `ChunkLocation` (`:42-48`) + outer `ChunkHeader` fields (`:409-435`)** have size-only asserts — a same-size field reorder produces garbage offsets with a valid version and no diagnostic, and unlike the payload structs the assert messages carry no reorder instruction. Add `BT_OFFSETOF` locks for both (the established `SceneHeader` pattern).
13. **`FileUtils.h:65-71` `ReadEntireFile`** returns a zero-filled buffer on open failure/short read (offline/tools path; `GetFileOrStringContent` beside it checks both). Check stream + `gcount`, throw on failure.
14. **`Flags.h:73-77` `operator&`** is any-bit, not all-bits, for composite enumerators — document the any-bit semantics at the operator (comment only; no composite-enumerator caller found in spot checks).
15. **`AlignedMemory.h:9-34` `MakeAligned`** never runs constructors/destructor; all six current call sites are trivial types. Add `static_assert(std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>)`.

## Critical files

- `Common/Determinism.h`, `Common/Determinism.cpp`
- `Common/Crc.h`, `Common/Math/ConvexHull.h`, `Common/Math/MathUtils.h`, `Common/Math/Random.h`/`.cpp`
- `Common/Threading/Multithreading.h`
- `Common/Serialization.h`, `Common/DataFile.h`, `Common/FileUtils.h`, `Common/Flags.h`, `Common/AlignedMemory.h`
- `Engine/Source/File/DifferenceStream.h` (item 11 rider)
- `Documents/FloatingPointDeterminism.txt` (item 6 doc line)

## Invariant exposure

- **CRC/determinism paths touched but behavior-preserving where reached**: items 2/3/5 change behavior only on inputs that are currently impossible or already-broken; item 4 changes NavData winding math (derived data outside the CRC, rebuilt from one code path — same-binary safe, see item 4). Item 6 adds a ctor; existing seeds and streams unchanged. No `kiVersion`, wire, or save-format change (items 11/12 are compile-time guards; the DifferenceStream tuple branch is dead code today).
- No allocation-tracked-path changes beyond one `LOG` in the Dispatch drain (worker teardown path, not per-frame).

## Out of scope

- Making `Determinism.h` `operator==` actually bitwise (memcmp) — the sole consumer needing bitwise semantics (`LogDifference`) already memcmps; changing global operator semantics has unknown blast radius. Comment fix only.
- A repo-wide sweep of `Flags<>` instantiations for composite enumerators (spot-checked clean; the doc note is the guard).
- Lemire-bias rejection sampling in `Random(uint32_t)` — bias ≪1 ppm, contract-compliant; reviewer recorded as informational.
- `PersistentWorker`, `ThreadLocal`, `Workbuffer` (own plan), `Smoothed`, log internals — all verified sound in the sweep.
- The `FloatingPointDeterminism.txt` §10 rewrite — owned by `Engine/Architecture_DeterminismDocSync.md` (co-schedule the item-6 line edit with it if convenient).

## Acceptance criteria

- Both builds compile (items 2/11/15 are constraint tightenings — any compile break they cause is a found bug, fix the caller).
- No stream/CRC change for any currently-reachable input: items with runtime-behavior deltas (3, 4, 5, 7, 8) trigger only on currently-degenerate/unreached inputs.

## Notes

- Grill decision pre-staged: item 4's shape (relative shoelace vs `double` accumulation) — recommended: relative shoelace (stays float, no double-rounding question); confirm at grill.
