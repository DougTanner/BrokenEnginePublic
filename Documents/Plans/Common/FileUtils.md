# FileUtils: ContentsEqual false-equal on unreadable file (silent stale codegen)

## Context

`Common/FileUtils.h` holds three offline-only helpers (`GetFileOrStringContent`, `ContentsEqual`, `ReadEntireFile`) consumed exclusively by DataPacker codegen. All callers are offline tooling, so general I/O error-handling is out of scope per project policy ("assume params valid"). One narrow exception survives: a silent-wrong-data path that can leave a **stale generated header on disk with no failure signal**.

`GetFileOrStringContent` (FileUtils.h:8) on the `std::filesystem::path` branch only sets `valid == false` when `std::filesystem::exists` is false (FileUtils.h:17). For a path that exists but cannot be read (locked, permission denied, or a directory), it still returns `valid == true` with a `uiFileSize`-length buffer whose tail is whatever `resize` zero-filled, because the result of `fileStream.read` (FileUtils.h:29) is never checked. `ReadEntireFile` (FileUtils.h:59-64) has the same unchecked-read shape but its callers consume the bytes directly, so that path is not the correctness hazard — leave it.

The hazard is `ContentsEqual` (FileUtils.h:42-55), used at two DataPacker codegen sites:
- `WriteFileIfDifferent` — `DataPacker/Source/Main.cpp:49`: `ContentsEqual(rContent /*new string*/, rPath /*existing header*/)`; on equal it **skips** writing the freshly generated header.
- Header rename — `DataPacker/Source/Main.cpp:462`: writes `name.h.tmp`, then `ContentsEqual(temporaryHeaderFile, headerFile)`; on equal it **skips** the rename so the old `.h` survives.

If the existing on-disk header is present but unreadable and happens to compare equal to the other (garbage) side, `ContentsEqual` returns a false "equal", the write/rename is skipped, and the build proceeds against a stale `.h` that no longer matches the regenerated `.pack` data — corrupt baked output with no error. Low probability (the new side is content the tool just produced this run), but it is a true silent-wrong-data correctness bug, so fix it narrowly by making `valid` mean "successfully read all requested bytes."

## Design

Fold the read-success check into `GetFileOrStringContent`'s `valid` flag on the path branch (FileUtils.h:15-33). After the read, require the open to have succeeded and the full requested size to have been read; otherwise return `{false, {}}`. This makes `ContentsEqual`'s existing `if (!oneValid || !twoValid) return false;` guard (FileUtils.h:48-51) correct for unreadable-but-existing files — an unreadable side becomes "not equal", so the codegen sites write/rename instead of silently skipping. **(effort 1)**

- `common::GetFileOrStringContent` — `Common/FileUtils.h:28-32`: after constructing `fileStream`, check `fileStream.read(...)` then `if (!fileStream || static_cast<size_t>(fileStream.gcount()) != uiFileSize) return {false, {}};` before the `{true, std::move(fileContents)}` return. The `std::string` overload (FileUtils.h:13) and the `static_assert` else branch (FileUtils.h:36) are unchanged. **(effort 1)**
- `common::ContentsEqual` — `Common/FileUtils.h:42-55`: no code change; behavior is corrected transitively by the `valid` semantics above. Confirm both call sites (Main.cpp:49, Main.cpp:462) now write/rename when the existing header is unreadable. **(effort 1)**

Keep the `std::pair<bool, std::string>` return type — do not migrate to `std::optional`/`std::expected` (YAGNI for two internal call sites).

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\FileUtils.h` — `GetFileOrStringContent` read-success check (lines 28-32).
- `C:\Users\dougt\Documents\BrokenEnginePublic\DataPacker\Source\Main.cpp` — call sites to re-verify (lines 49, 462); no edits expected.

## Out of scope

- **All pure I/O error-handling on offline-tool inputs** (report H1 partial-read in `ReadEntireFile`, M1 TOCTOU between `exists`/`file_size`/open, M2 throwing `filesystem` overloads, M3 directory/special-file guard). These are robustness against OS faults on tool-controlled paths with no silent-wrong-data consequence — dropped per project "assume params valid" policy. Only the false-equal codegen-skip path is kept.
- **`ReadEntireFile` unchecked read** (report H1 second half): its callers consume the bytes directly; no false-equal / silent-stale-output channel. Leave as-is.
- **M4 size early-out / chunked `memcmp` in `ContentsEqual`**: performance only; generated headers are small. YAGNI.
- **All style findings (L1-L7)**: Hungarian prefixes (`oneValid`/`twoValid`), `template<` spacing, uppercase template params, `static_assert(false)` idiom (already correct under C++23/MSVC2026), redundant `.close()`, offline-only comments, `std::string`-vs-`std::vector<std::byte>` cohesion. Route to code-style-review; do not address here.

## Acceptance criteria

- `GetFileOrStringContent` returns `valid == false` for a path that exists but whose bytes cannot be fully read (open fails or short read), instead of `valid == true` with a zero-padded buffer.
- An existing-but-unreadable header no longer causes `ContentsEqual` to report "equal"; the two DataPacker codegen sites consequently write/rename rather than silently skipping.
- `std::string` overload and `static_assert` else branch unchanged; return type stays `std::pair<bool, std::string>`.
- DataPacker builds clean.

## Notes

- Source verified as ground truth on 2026-05-28; report symbols and line numbers match the live `FileUtils.h`. Report is genuine (not the hallucinated one in this effort).
- The dangerous direction (false-equal → skip write) requires both compared sides to read as identical garbage; realistic but low-probability since one side is freshly generated this run. Fix cost is one branch, so worth taking despite low likelihood.
