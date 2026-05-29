# Determinism: Indeterminate / Padding Bytes in Hashed & Serialized Blobs

## Context

Determinism is the rollback-and-replay contract (`Documents/FloatingPointDeterminism.txt`): dual CRCs detect client/server desync, and DataPacker `ContentsEqual` checks gate reproducible bakes. **Any byte that differs across machines or builds for a logically-equal value is a desync source.**

Three foundation-layer primitives share one root-cause class — they move *whole object representations* (via `reinterpret_cast` + `sizeof`) into a hash or onto disk, gated only by `std::is_trivially_copyable_v<T>`. Trivial-copyability guarantees byte-copyability but says **nothing** about padding: inter-member and trailing padding bytes are *indeterminate* unless the object was value-initialized. The three sites compose on the deterministic path:

1. `common::Crc(const T&)` / `Crc(const T*, n)` (`Common/Crc.h`) hash the raw bytes, so padding feeds the desync CRC and the `unordered_map` dedup (`std::hash<ModelVertex>`).
2. `common::Write/Read` (`Common/Serialization.h`) byte-blast the same way, so padding is persisted into `.pack`/save blobs and round-tripped.
3. `ChunkHeader` (`Common/DataFile.h`) is written by DataPacker and `reinterpret_cast`-read by the engine, then CRC-verified. The writer (`ExportJob::AllocateHeaderAndData` + `ExportJob::RunExport`, `DataPacker/Source/ExportJobs/ExportJob.cpp:37-45,162-169`) does **not** value-init a `ChunkHeader{}` — it `resize()`s an `std::vector<std::byte> mHeaderAndData` (zero-filled by `vector`) and then `reinterpret_cast`s the front to `ChunkHeader*` and stamps individual fields (`iMagic`/`crc`/`flags`/`pcPath` + the per-type header). So today the union tail happens to be zeroed by `vector::resize`, but nothing in the *type* guarantees it: any non-resize writer (or a future placement that uses `ChunkHeader chunkHeader {}` directly, as the engine read path does at `FileManager.cpp:242`) value-inits only the *first* union member (`fontHeader`), leaving the tail up to `sizeof(union)` indeterminate. The union of seven NSDMI-bearing header structs has **no canonical-zero member**. `SceneHeader` additionally hides 4 compiler-inserted padding bytes before `modelCrc` that the hand `uiPad[3]` does not cover.

Because #1 hashes what #2 persists and what #3 lays out, fixing them piecemeal leaves the contract half-enforced (e.g. zero a struct for serialize but still hash its padding elsewhere). The coordinated fix is **one shared rule** — *equal values produce equal bytes* — expressed as: (a) byte-hash/byte-blast paths assert no-padding where the type allows it, (b) every persisted/hashed aggregate is value-initialized before fill so padding is deterministically zero, (c) the layout of the persisted `.pack` header is locked with `static_assert`.

This plan is the cross-cutting determinism root-cause fix. The non-padding findings in the per-file reports stay with their own plans (see Out of scope).

## Design

### Shared rule (propose once, apply to all three sites)
**"Equal values must produce equal bytes."** Operationally:
- **R1 — No-padding guard on byte paths.** Where a byte-reinterpret hash or byte-blast serialize is used on a *non-float* aggregate, require `std::has_unique_object_representations_v<T>` (no padding, no trap reps). This converts a silent determinism bug into a compile error. **It cannot be the blanket assert on the generic templates**: `has_unique_object_representations` deliberately rejects every float-bearing type (±0.0/NaN), and the codebase hashes/serializes float structs everywhere (XMFLOAT*, IslandHeader, MaterialShaderData). So R1 is applied as an *opt-in* `static_assert` on the concrete persisted/hashed types (R3), not on `Crc(const T&)`/`Write(const T&)` themselves. The generic templates instead get a loud documented contract (R2).
- **R2 — Documented contract + value-init mandate.** The generic byte templates document: "callers must pass a value-initialized (`{}`) object of a padding-free or deterministically-zeroed type; padding/float bit patterns are hashed/persisted verbatim." This matches the existing `XMVECTOR` overloads that already normalize layout via `XMFLOAT4`.
- **R3 — Zero-then-fill + lock layout on persisted types.** Every struct that is written into the `.pack` / a save blob, or fed to the desync CRC as raw bytes, is value-initialized before population and pinned with `static_assert(sizeof == expected)` so any future layout drift fails the build.

### Site 1 — `Common/Crc.h`
- `Crc(const T&)` `Common/Crc.h:97-102` and `Crc(const T*, int64_t)` `Common/Crc.h:81-86`: keep `is_trivially_copyable_v` assert; **add R2 contract comment** stating padding/float bytes are hashed verbatim and the object must be value-initialized. Do **not** add a blanket `has_unique_object_representations_v` assert here (would reject all float structs the codebase already hashes). **Effort 1.**
- Provide an opt-in checked entry for callers who want the guarantee: a thin `CrcNoPadding(const T&)` (or a `static_assert` snippet callers paste) that adds `static_assert(std::has_unique_object_representations_v<T>)` then forwards to `Crc`. Only adopt if a concrete caller needs it; otherwise R3's per-type `static_assert` at the struct definition (DataFile.h) covers the real consumers. **Effort 1, YAGNI-gate this — prefer per-type asserts.**

### Site 2 — `Common/Serialization.h`
- `Write(const T&)` `Common/Serialization.h:44-49`, `Read(T&)` `:20-24`, and the pointer/array overloads `:26-31,51-56`: keep `is_trivially_copyable_v` assert; **add R2 contract comment** ("POD written verbatim incl. padding; pass `{}`-initialized, padding-free or zeroed structs; native endianness/sizeof, x64-only"). No signature change for determinism. **Effort 1.**
- Determinism is enforced at the *type definition* (R3) and at *fill sites* (value-init), not by tightening the generic assert (same float-rejection reason as Site 1).

### Site 3 — `Common/DataFile.h` (the persisted format — primary fix)
- **Canonical-zero the union** so the *entire* `ChunkHeader` footprint is deterministic regardless of active member. KISS option: give `ChunkHeader` a user-defined default ctor that value-inits the largest-footprint coverage, or add a leading `uint8_t pad[sizeof(<largest member>)] {}` as the union's **first** member so `ChunkHeader{}` zeroes the whole union. `union` `Common/DataFile.h:299-308`. **Effort 2.**
- **Lock the format contract:** `static_assert(std::is_trivially_copyable_v<ChunkHeader>)` — required because the engine read path `reinterpret_cast`s mapped pack bytes to `ChunkHeader*` (`FileManager.cpp:340`) and copies a `ChunkHeader chunkHeader {}` by value (`FileManager.cpp:242-252`). `has_unique_object_representations_v<ChunkHeader>` would be the ideal no-hidden-padding proof, but `ChunkHeader` embeds float-bearing headers (`IslandHeader`, `MaterialShaderData`, etc. via the union), and that trait *rejects* every float type — so it will not compile. **Use `static_assert(sizeof(ChunkHeader) == <expected>)` + per-field `offsetof` asserts as the layout lock** (the float-compatible KISS choice the DataFile report's H3/M1 also recommends); the actual determinism guarantee comes from R3 zero-init at the type + writer. `ChunkHeader` `Common/DataFile.h:288-309`. **Effort 2.**
- **Fix `SceneHeader` hidden padding** `Common/DataFile.h:77-88`: the manual `uiPad[3]` leaves 4 compiler-inserted bytes before the 8-byte `modelCrc`. Either reorder `crc_t modelCrc` first (largest-alignment-first, eliminates the gap) or add explicit `uint8_t uiPad2[4] {}`. Add `static_assert(sizeof(SceneHeader) == N)`. **Effort 1.**
- **Audit the other hand-padded headers** for the same hidden-tail risk and add `sizeof` static_asserts: `ModelNode` `:127-135` (`uiPad[2]`), `MaterialInfo` `:150-157` (`uiPad[3]`), `Character` `:217-226` (14-byte odd size), `ModelVertex` `:324-346` (the `std::hash` byte-hashes it — confirm padding-free with `static_assert(sizeof(ModelVertex) == sum-of-members)` so dedup equality matches the byte hash). **Effort 1.**
- **Zero-then-fill at the writer:** the DataPacker writer reaches the union via `reinterpret_cast` into a `resize()`d `mHeaderAndData` buffer (`ExportJob::AllocateHeaderAndData` `DataPacker/Source/ExportJobs/ExportJob.cpp:37-45`), so the canonical-zero union member must come from the *type* (above) since no `ChunkHeader{}` value-init runs there. Adding the leading zero-pad union member makes the resize-zeroed buffer correct *by contract*, not by accident. Verify each per-type export job (`ExportJobs/Export*.cpp`) writes into the zeroed buffer and does not introduce an unzeroed `ChunkHeader` temporary. **Effort 1.**

### Why one coordinated change
R3 zero-init is what actually makes bytes deterministic; R1/R2 are the guard rails that keep new code on the rule. Splitting them risks "serialize zeroed but hash raw" inconsistencies across the exact same struct, since `Crc` and `Write` are applied to the same persisted headers.

## Critical files
- `Common/Crc.h` — `Crc(const T&)` :97-102, `Crc(const T*, int64_t)` :81-86 (R2 contract comments; optional opt-in checked entry).
- `Common/Serialization.h` — `Write/Read` overloads :20-56 (R2 contract comments).
- `Common/DataFile.h` — `ChunkHeader`/union :288-309 (canonical-zero + layout lock), `SceneHeader` :77-88 (hidden padding), `ModelNode`/`MaterialInfo`/`Character`/`ModelVertex` (`sizeof` asserts), `std::hash<ModelVertex>` :350-357.
- Persisted-struct consumers (verify zero-then-fill / read contract; do not refactor):
  - `DataPacker/Source/ExportJobs/ExportJob.cpp` — `AllocateHeaderAndData` (:37-45) + `RunExport` (:162-169) are the actual chunk-header writer (`reinterpret_cast` into the `resize()`d `mHeaderAndData`); relies on the union being canonical-zeroable so the resize-zeroed buffer is correct by contract.
  - DataPacker per-type export jobs (`DataPacker/Source/ExportJobs/Export*.cpp`) — confirm each populates the already-zeroed header buffer, no unzeroed `ChunkHeader` temporaries.
  - `Engine/Source/File/FileManager.cpp` — `ChunkHeader chunkHeader {}` copy (:242-252) and `reinterpret_cast<ChunkHeader*>` over the mmap'd pack (:340-341) read sites; rely on the trivially-copyable + no-indeterminate-tail contract the static_asserts now lock. `ExportJob::Version()` (`ExportJob.h:11`) already folds `sizeof(ChunkHeader)` into the cache version, so a `sizeof` change auto-invalidates caches.

## Out of scope
Non-padding findings already owned by the per-file plans (`Crc.md` / `Serialization.md` / `DataFile.md`):
- Crc.h: H2 duplicated `Crc`/`CrcConsteval` constants, M1 `char` sign-extension, M2 `XMStoreFloat4A`, M3 `XMVECTOR` overload-resolution guard, M4 XOR-fold cancellation, all Low items (naming, `IntToString`, `XM_CALLCONV`).
- Serialization.h: H1 short-read detection, H2 untrusted-count/resize overload, H3 size overflow, M2 endianness note, M3 pointer-member footgun, M4 non-const `Write` pointer, all Low items.
- DataFile.h: H1 `AlignOutputStream` stream-state guard, H2 untrusted `.pack` field bounds validation, H3 version-vs-layout coverage (the broad version scheme; this plan only adds the *layout-lock* asserts that catch padding drift), H4 magic-number type, M2 `MAX_PATH` size, M3 flag hierarchy, M4 elevation constants placement, M5 `kiMaxMeshes` invariant assert, M6 `WAVEFORMATEX` coupling.
- Per project rules: no input validation, no error handling, no unit tests. Padding/determinism fixes are *correctness*, not input validation — they are in scope.

## Acceptance criteria
- For any persisted/hashed aggregate, two logically-equal values produce **byte-identical** representations → equal `Crc()` and equal serialized bytes → no spurious `ContentsEqual` / desync mismatch from stale padding.
- `ChunkHeader{}` deterministically zeroes the entire union footprint; `static_assert` on `ChunkHeader` layout (`sizeof` + `offsetof`, or `has_unique_object_representations` if it compiles) holds.
- `SceneHeader` (and the other audited headers) carry `sizeof` static_asserts that fail the build on layout drift; no hidden compiler padding remains uninitialized in any persisted header.
- `std::hash<ModelVertex>` byte hash is proven padding-free, matching `operator==`.
- Generic `Crc`/`Write`/`Read` carry the R2 contract comment; DataPacker writers value-init headers before fill.
- All new `static_assert`s compile; DataPacker and Engine build clean.

## Notes
- `has_unique_object_representations_v` rejects all float-bearing types by design (±0.0/NaN) — so it is **not** usable as the blanket assert on the generic templates or on float headers. The real determinism guarantee comes from value-init (R3); the `static_assert` lock on `.pack` headers should therefore prefer `sizeof`/`offsetof` for float-containing structs.
- The `XMVECTOR` `Crc`/`Write` overloads already model the right pattern (normalize layout via `XMFLOAT4`); the generic path should aspire to the same via R3, not by tightening the trivially-copyable assert.
- DataPacker is offline (no main-loop allocation tracker concern); the runtime hash path is allocation-free (`string_view` non-owning).
- Keep the existing `is_trivially_copyable_v` asserts; the changes are additive (comments, value-init, `static_assert`s) and do not alter the wire/hash algorithm, so they do **not** require a `.pack` version bump *unless* a header's `sizeof` changes (the `SceneHeader` reorder/pad-fix would — bump `DataHeader::kiVersion` if `sizeof(ChunkHeader)` shifts).
