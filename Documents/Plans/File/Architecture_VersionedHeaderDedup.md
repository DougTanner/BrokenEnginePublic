# Architecture: Versioned-Header Convention Dedup

## Context

Source: /external-architecture-review on `Engine/Source/File`. The version+size file-header convention exists in two hand-synchronized copies: `WriteVersionedFile`/`ReadVersionedFile` (`FileManager.h:262-265`, `285-290`) and `DifferenceStreamWriter::Save`/the `DifferenceStreamReader` ctor, whose comments literally say "matches WriteVersionedFile pattern" (`DifferenceStream.h:73-77`, `161-183`). Drift in one copy silently breaks the other's on-disk compatibility expectations.

## Design

### Engine/Source/File/FileManager.h
- Extract the convention into shared helpers beside the existing templates: e.g. `template <typename T> void WriteVersionHeader(std::fstream&)` (writes `T::kiVersion`, then `is_trivially_copyable_v<T> ? sizeof(T) : 0`) and `template <typename T> bool ReadAndValidateVersionHeader(std::fstream&, int64_t& riVersion, int64_t& riSize)` (reads both into the out-params, applies the existing version+size validity rules). The out-params are load-bearing: both callers print the file's read values on mismatch (the `DifferenceStreamReader` `kWarning` LOGs and `ReadVersionedFile`'s `iVersion: {} == {}` LOG) and `ReadVersionedFile`'s `DEBUG_BREAK` re-tests `iVersion == kiVersion && iSize != sizeof(T)` — a bare `bool` return cannot supply them. Rewire `WriteVersionedFile`/`ReadVersionedFile` through them, keeping the existing LOG lines and the `DEBUG_BREAK` size-mismatch diagnostic at the `ReadVersionedFile` caller. [~20m]

### Engine/Source/File/DifferenceStream.h
- `DifferenceStreamWriter::Save` writes its `SAVED_TYPE` + `DIFFERENCE_TYPE` headers via `WriteVersionHeader<T>` (lines 74-77); the `DifferenceStreamReader` ctor validates via `ReadAndValidateVersionHeader<T>` (lines 162-183), preserving the existing per-type `kWarning` LOGs (log at the caller on a false return). [~15m]

## Critical files

- `Engine/Source/File/FileManager.h`
- `Engine/Source/File/DifferenceStream.h`

## Out of scope

- Changing the on-disk format — the helpers must produce byte-identical output to today's writers (that is the acceptance criterion, not a goal of change).
- A concept/`static_assert` formalizing DifferenceStream's template contract (`kiVersion`, `Crc()`, `interpolate.iTick`, `LogDifferences`, stream operators) — adjacent finding, deliberately not filed: the contract is stable with a single instantiation pair (`game::Frame`/`game::FrameInput`); YAGNI until a second consumer appears.
- Relocating the `has_binary_stream_operators` trait to `Common/` — cosmetic placement, not filed.

## Acceptance criteria

- Existing saves/replays still load; newly written ones round-trip (byte-identical headers).
- A single definition of the version+size write/read convention remains.

## Notes

- Touches save/replay header serialization paths — mechanical and compile-checked, but verify with a record/replay round-trip. Local files only; no network/CRC-sim exposure, no `kiVersion` value changes.
- Depends on `File/Architecture_IncludeHygiene.md`'s `DifferenceStream.h` → `FileManager.h` include (or land together) so the helpers are visible without `Engine.h` ordering reliance.

## Verification Notes

Verified against source (2026-06-10); helper signature amended:

- Byte-identical extraction confirmed feasible. Write side: `WriteVersionedFile` (FileManager.h:262-265) writes `int64_t` version then `int64_t` size (`is_trivially_copyable_v ? sizeof : 0`) via `common::Write`; `DifferenceStreamWriter::Save` (DifferenceStream.h:74-77) writes the identical two-int64 sequence per type (`SAVED_TYPE` then `DIFFERENCE_TYPE`) — a shared `WriteVersionHeader<T>` produces the same bytes for all three call sites. Read side: `ReadVersionedFile` (FileManager.h:285-290) and the `DifferenceStreamReader` ctor (DifferenceStream.h:162-183) apply the same validity rule (`version == T::kiVersion && (trivially-copyable ? size == sizeof(T) : true)`).
- Diagnostic-placement claim sanity-checked and found under-specified as written: the caller-side `kWarning` LOGs (DifferenceStream.h:174, 181) and `ReadVersionedFile`'s LOG (289) + `DEBUG_BREAK` re-test (309-315) all consume the *read* version/size values, which a `bool`-returning helper cannot supply. Design bullet amended in place to out-param the read values (`int64_t& riVersion, int64_t& riSize`). With that, keeping all LOGs and the `DEBUG_BREAK` at callers is feasible as planned.
- Comments "matches WriteVersionedFile pattern" / "matches ReadVersionedFile pattern" confirmed at DifferenceStream.h:73 and :161 — the hand-sync risk motivating the plan is real.
- Out-of-scope items confirmed accurate (single instantiation pair; trait already in FileManager.h:203-224).
- Scoring caveat: byte-identity is not compile-checked — the plan itself requires a record/replay round-trip, which leans Risks 2 rather than 1 under the scoring anchors.
