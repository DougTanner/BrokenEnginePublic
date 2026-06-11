# Refactor: Client Guard Nesting and Wide-Char Directory APIs

## Context

Source: /external-refactor-clean on `Engine/Source/File`. Two small mechanical items in `FileManager`.

## Design

### Engine/Source/File/FileManager.cpp — redundant nested `BT_CLIENT` guard
- Lines 370-374: an inner `#if defined(BT_CLIENT)` sits inside the eager-load block already wrapped by `#if defined(BT_CLIENT)` at line 320 — the inner guard is dead weight and its first body line (line 371, `AnimationData& rAnimData = ...`) is mis-indented one level out. Remove the inner `#if`/`#endif`, fix the indentation. [~5m]

### Engine/Source/File/FileManager.cpp — ctor ANSI path APIs
- Lines 27-37: the `FileManager` ctor mixes wide and ANSI path retrieval — AppData uses `SHGetKnownFolderPath` (wide, correct), but Temp uses `GetTempPath` and the exe path uses `GetModuleFileName`, both resolving to the `-A` variants writing `char pcDirectory[MAX_PATH]`. Characters not representable in the active code page (user name, install path) mangle `mTempDirectory`/`mDataDirectory`. Switch to `GetTempPathW`/`GetModuleFileNameW` with a `wchar_t` buffer — `std::filesystem::path` takes wide natively, dropping the narrow round-trip. [~10m]

## Critical files

- `Engine/Source/File/FileManager.cpp`

## Out of scope

- The stale "pre-faulted" comments in `FileManager` — owned by `Common/StaleCodeCommentsSweep.md`.
- Moving the animation block the nested guard wraps — `File/Architecture_AnimationDataLoadPlacement.md`; if that lands first, the nested-guard item evaporates (check before executing).
- Other ctor behavior (directory creation, logging) — unchanged.

## Acceptance criteria

- Client + server build clean; boot logs show the same three directories on an ASCII-named user account.

## Notes

- No determinism/CRC/network/`kiVersion` exposure. Startup-only code.
- The wide-API change alters the bytes of `mTempDirectory`/`mDataDirectory` only for paths where today's behavior is already wrong (non-ACP-representable characters).

## Verification Notes

Verified against source (2026-06-10):

- Guard nesting confirmed: outer `#if defined(BT_CLIENT)` opens at `FileManager.cpp:320`, inner at 370, `#endif`s at 374 (inner) and 378 (outer) — the inner guard is genuinely redundant. The mis-indent at line 371 confirmed (`AnimationData& rAnimData` sits one tab level out from its siblings at 372-373). Minor adjacent note: the `[[maybe_unused]]` on `pAnimationData` (line 367) is equally vestigial, but that line belongs to the block `Architecture_AnimationDataLoadPlacement.md` relocates — leave it to that plan.
- ANSI-API claim confirmed: `GetTempPath` (line 29) and `GetModuleFileName` (line 36) both write the `char pcDirectory[MAX_PATH]` buffer (line 28) — they necessarily resolve to the `-A` variants (the `-W` forms would not compile against a `char` buffer). `SHGetKnownFolderPath` (line 20) is wide as stated. `std::filesystem::path` accepts `wchar_t*` natively, so the proposed `GetTempPathW`/`GetModuleFileNameW` + `wchar_t` buffer swap is a drop-in for both assignments (the buffer is shared by the two blocks — convert it once).
- Cross-reference with `Architecture_AnimationDataLoadPlacement.md` confirmed bidirectional (its landing removes the nested-guard item; the check-before-executing note is correct).
- Out-of-scope deferral confirmed: the FileManager "pre-faulted" comments are item 4 of `Common/StaleCodeCommentsSweep.md` — no duplication.
