# ClientGuid Versioned-Header Convention (Decision)

## Context

The landed `File/Architecture_VersionedHeaderDedup.md` single-sourced the int64 `version` + int64 `size` on-disk file-header convention into `engine::WriteVersionHeader<T>` / `engine::ReadAndValidateVersionHeader<T>` (`FileManager.h`), shared by `WriteVersionedFile`/`ReadVersionedFile`, `DifferenceStream` save/load, and `GameSaveLoad` grid saves.

`ClientGuid` persist/load is a **fourth** hand-rolled copy of the same int64-version + int64-size prefix idiom — `LoadClientGuidFromDisk` (`ClientSessionBase.cpp:17-39`, reads at `:27-31`) and `PersistClientGuidToDisk` (`:41-60`, writes at `:49-54`) — but it deliberately differs and could **not** be folded byte-identically by that plan:

- `ClientGuid` (`NetworkProtocol.h:86-93`) is **trivially-copyable** (two `uint64_t`, defaulted `operator==`, no user dtor) → `WriteVersionHeader<ClientGuid>` would emit `sizeof(ClientGuid)` (16) instead of the current hardcoded `0`, and `ReadAndValidateVersionHeader<ClientGuid>` would require `size == 16`, **rejecting every existing `ClientGuid.bin`** (written with size `0`).
- `ClientGuid` has **no `kiVersion` member** — the persist path uses a bare literal `1`.
- The read path validates `iGuidVersion >= 1` (forward-compatible), looser than the helper's `version == kiVersion`.
- The payload is written **field-by-field** (`uiHigh`/`uiLow`), not as the whole struct.

`ClientGuid.bin` is a network-persisted identity file with a documented atomic-write / orphan-prevention invariant (`ClientSessionBase.cpp:43`; `Engine/Source/Network/Client/CLAUDE.md` "GUID"): a load failure re-handshakes as a new client and orphans this client's server-side fleets/players once.

## Design (present options)

- **A — Accept + document (recommended).** Leave persist/load as-is; add a short comment at the two functions (and/or a one-line `Client/CLAUDE.md` "GUID" note) explaining the on-disk header **intentionally does not** share the engine helper — size-`0` sentinel + `>= 1` forward-compat + no `kiVersion` member — so a future dev does not "helpfully" route it through `WriteVersionHeader` and break every existing `ClientGuid.bin`. Doc/comment-only, Risks 0.
- **B — Add a `kiVersion` and migrate to the shared helper.** Give `ClientGuid` a `static constexpr int64_t kiVersion = 1;` and route both paths through the helpers. Because `ClientGuid` is trivially-copyable the helper writes size `16`, so existing size-`0` files fail validation — either **accept the one-time reset** (every client re-handshakes as new; server-side fleets orphaned once) **or add a back-compat read path** (fall back to the old size-`0` layout when the new validation fails). Touches a wire-protocol struct (`NetworkProtocol.h`) and the persisted-identity invariant. Risks 2.
- **C — Generalize the helper** (e.g. a non-size-validating variant or a `bForwardCompatible` parameter) so this one caller can share it without a format change. Adds generality to the shared helper for a single consumer — likely YAGNI / over-abstraction.

**Recommendation: A.** The duplication is two int64 writes; the value of folding is marginal and the migration risk (breaking persisted identity) is real. Documenting *why* it is deliberately separate is the higher-value, zero-risk outcome.

## Critical files

- `Engine/Source/Network/Client/ClientSessionBase.cpp` — `LoadClientGuidFromDisk` (`:17-39`), `PersistClientGuidToDisk` (`:41-60`)
- `Engine/Source/Network/NetworkProtocol.h` — `ClientGuid` (`:86-93`) — option B only
- `Engine/Source/Network/Client/CLAUDE.md` — "GUID" section — option A doc note

## Out of scope

- The three sites already deduped by `File/Architecture_VersionedHeaderDedup.md` (`FileManager.h`, `DifferenceStream.h`, `GameSaveLoad.cpp`) — landed.
- The `ClientGuid` **network** wire format — this concerns the on-disk `ClientGuid.bin` header only.
- Any other `NetworkProtocol.h` struct; the magic-prefixed `TextureCache`/`.pack` header conventions (different shape, not this convention).

## Acceptance criteria

- **Option A**: a comment/doc clearly states why `ClientGuid`'s on-disk header is intentionally not the shared helper; no code/format change; existing `ClientGuid.bin` still loads.
- **Option B**: existing-file handling decided (one-time reset accepted, or a back-compat read path added); `ClientGuid.bin` round-trips.

## Notes

- **Decision plan (present options)** — resolve via `/external-grill-plan` before any edit.
- **Invariant exposure**: option A is doc/comment-only (no exposure). Option B touches a wire-protocol struct (`NetworkProtocol.h`) and the network-persisted identity file `ClientGuid.bin` / its atomic-write orphan-prevention invariant, and is **not** byte-identical (rejects existing files) — a one-time GUID reset or a migration read path must be decided at grill. No CRC-sim/determinism exposure either way; client-only.
- Surfaced by the `File/Architecture_VersionedHeaderDedup.md` `/next-plan` Step-6 sweep as the fourth sibling of the version+size header convention (the other near-misses — `TextureCache` header, DataPacker `.pack` header — are different magic-prefixed conventions, not in scope).
