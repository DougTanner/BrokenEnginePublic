# Architecture: Attribution ThirdParty Path Ownership

## Context
Source: /external-architecture-review on `DataPacker/Source/`. `attribution::CopyThirdPartyLicenses` (`Attribution.cpp:8`) derives the repo's ThirdParty directory as `rOutputDirectory / "../../../../../../ThirdParty"` — correct only when the output dir sits exactly six levels below the repo root (the zero-arg sandbox default). `FileManager` accepts an arbitrary CLI output directory (`FileManager.cpp:18`), so any non-default invocation makes the `std::filesystem::directory_iterator` at `Attribution.cpp:34` throw on a nonexistent path — failing the whole run *after* all packs exported successfully. It also violates the documented charter that FileManager "owns input/output/temp directory resolution" (`DataPacker/Source/CLAUDE.md`). The repo root is reliably derivable from the already-canonicalized engine data dir: `mpInputDirectories[0]` (`<repo>/Engine/Data`, canonicalized at `FileManager.cpp:21`) → `parent_path().parent_path()`.

## Design

### DataPacker/Source/FileManager.h / FileManager.cpp
- Add a `mThirdPartyDirectory` member to `FileManager`, resolved in the constructor from the canonical `mpInputDirectories[0]` (`parent_path().parent_path() / "ThirdParty"`), with a `VERIFY_SUCCESS(std::filesystem::exists(mThirdPartyDirectory))` matching the existing dir checks at `FileManager.cpp:27/42`. [~10m]
- Canonicalize `mOutputDirectory` after `create_directories` (`FileManager.cpp:26-27`) — closes the asymmetry that let the lexical `..`-relative derivation limp along, and gives downstream consumers a stable absolute path. [~5m]

### DataPacker/Source/Attribution.cpp
- `CopyThirdPartyLicenses` — replace the `rOutputDirectory / "../../../../../../ThirdParty"` derivation (`Attribution.cpp:8`) with `gpFileManager->mThirdPartyDirectory` (include `FileManager.h`). The `rOutputDirectory / "../Attribution"` destination (`Attribution.cpp:9`) is output-relative by design and stays. [~5m]

## Critical files
- `DataPacker/Source/FileManager.h`
- `DataPacker/Source/FileManager.cpp`
- `DataPacker/Source/Attribution.cpp`

## Out of scope
- Renaming `FileManager` (name collision with `Engine/Source/File/FileManager.h` — surfaced as a chat observation, not filed).
- The `Main.cpp` call site (`Main.cpp:572`) — signature unchanged, still receives the output directory for the Attribution destination.
- Attribution's license-matching policy (priority/fallback rules, `Prebuilts` skip, `ASSERT(bFoundLicense)`).
- Sandbox default paths in `FileManager.cpp:9-11` — documented, CWD-relative by design as a pre-build event.

## Notes
- Offline DataPacker only — no determinism/CRC exposure. Output artifacts unchanged for the default invocation; custom-output-dir invocations stop throwing.

## Verification Notes
- All claims verified against source. `Attribution.cpp:8` does derive ThirdParty as `rOutputDirectory / "../../../../../../ThirdParty"` (six `..` components), correct only for the default output `<repo>/Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data` — exactly six levels below repo root. `std::filesystem::directory_iterator` (`Attribution.cpp:34`) throws on a nonexistent path, and the call site (`Main.cpp:572`) runs after all exports, so a custom output dir fails the run post-export as claimed.
- `mpInputDirectories[0]` is canonicalized at `FileManager.cpp:21`, so `parent_path().parent_path() / "ThirdParty"` is `<repo>/ThirdParty` (exists at repo root). The derivation assumes the CLI engine-data argument is `<repo>/Engine/Data` — that is the tool's documented contract, and the planned `VERIFY_SUCCESS(exists(mThirdPartyDirectory))` makes any violation loud at startup instead of post-export.
- `std::filesystem::canonical` throws if the path doesn't exist — the plan's ordering (canonicalize `mOutputDirectory` *after* `create_directories` at `FileManager.cpp:26`) is required, not just tidy.
- `FileManager` charter claim verified: `DataPacker/Source/CLAUDE.md` states FileManager "owns input/output/temp directory resolution and CLI parsing only" — `mThirdPartyDirectory` fits. Update that CLAUDE.md sentence when the member lands (standard doc step).
