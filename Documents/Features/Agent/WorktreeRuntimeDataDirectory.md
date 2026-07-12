# Worktree Runtime Data Directory

## Context

Session worktrees build independent client/server executables under their own `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output`, but `engine::FileManager` currently derives runtime data exclusively as the sibling `Output/Data` directory. The multi-gigabyte `.pack` payloads intentionally are not copied into every worktree, so a worktree executable cannot currently run against the authoritative data produced in the primary checkout.

The game projects also run DataPacker automatically into the worktree-local `Output/Data`. Missing local packs mark every affected aggregate dirty, which turns an ordinary code build into a full export. The motivating session established the failure modes directly:

- A normal worktree build reached the Gaea launch path even though terrain had just been exported and the intended task changed no data.
- Suppressing the DataPacker custom build prevented the expensive export but compilation then failed because `Data/DataTypes.h` was unavailable.
- The primary and worktree `Scene.h` matched, confirming that generated headers can be shared independently of executable location.
- A subsequent SHA-256 comparison showed all ten copied generated headers (`Data.h`, `DataTypes.h`, and eight per-type headers) matched the authoritative primary copies.

Worktrees need two explicit data modes so ordinary code sessions reuse immutable authoritative outputs without invoking exporters, while sessions that intentionally change assets, exporters, generated headers, or pack contracts retain isolated local outputs.

## Design

### Shared authoritative mode

This is the default for an ordinary code worktree.

- Resolve one authoritative `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data` beneath the recorded primary non-worktree checkout. The compile and harness skills already receive the session lifecycle's resolved primary checkout; canonicalize both checkouts, confirm they share the same canonical `git rev-parse --git-common-dir`, confirm the selected path is outside the session worktree, and require the complete data output. Never hard-code a branch/path, select an unverified first worktree entry, or fall back to the session's incomplete `Output/Data`.
- Pass an explicit MSBuild data mode and authoritative data-directory property. In this mode both game projects consume generated headers directly from the authoritative output, and their DataPacker custom step is disabled. Do not copy packs, create a directory junction, hard-link outputs, or run DataPacker with the authoritative directory as its writable output.
- Before compiling, run a strictly read-only output validation path. It must verify all expected generated headers, packs, and manifests; manifest `DataHeader` magic/version and bounded chunk tables; generated CRC header contents against the current asset path set; and each export job's current version/fingerprint/CRC against the manifest/pack without exporting. Any mismatch fails with a message requiring local export mode.
- The validation entry point must branch before `MigrateLegacyIntermediates`, `BakeIslandIntermediates`, Gaea launch/staging, cubemap generation, compression, aggregate rewrites, attribution copying, or any other output mutation. Its log must identify validation-only shared mode unambiguously.
- Compile the worktree client/server against the validated authoritative generated headers. Launch both worktree executables with the same authoritative runtime data directory, leaving each executable and log under the session worktree.

### Local export mode

This mode is explicit for a data/export worktree: any task changing `Engine/Data`, game `Data`, DataPacker export behavior, generated-header shape, `common::DataHeader`, chunk headers, compression, or pack/version contracts.

- Use the worktree's existing `Output/Data` as both generated-header include source and DataPacker output. Run the worktree's Release DataPacker normally, producing local headers, manifests, and packs without writing the primary output.
- Build the client/server against those local generated headers. The compile skill must not silently fall back to authoritative headers if local output is incomplete; export failure or missing output fails the build.
- Launch both client and server, including agent-harness verification, against the same local `Output/Data`. Tests must therefore exercise the session's exporter/data changes rather than accidentally passing against primary packs.

### Shared runtime launch option

- Extend `engine::LaunchOptions` / `ParseLaunchOptions` with one optional absolute `--data-directory <path>` used by both client and server. Empty preserves the existing `<executable directory>/Data` behavior for ordinary installed and local-export launches.
- Treat the supplied path as a filesystem trust boundary: canonicalize it, require a directory, report the resolved directory, and fail startup before `FileManager::LoadPackFiles` if it is unusable. `FileManager::GetDataFilePath` and all pack consumers remain unchanged after `mDataDirectory` is selected.
- Keep Git/worktree discovery outside the runtime binary. The compile and agent-harness skills resolve and validate the directory, then pass the explicit path. The binary understands a data directory, not repository topology.

### Skill and build integration

- Add an explicit shared/local mode interface to the compile skill. A session worktree defaults to shared authoritative mode; a plan or user request that changes data/export surfaces selects local export mode before the first build. Report the selected mode, canonical data directory, validation result, and whether the DataPacker export step is enabled.
- Express the mode in both game vcxprojs through common property names rather than duplicating ad-hoc command lines per Debug/Profile/Release block. The generated-data include root and DataPacker custom-step condition must resolve identically for client and server.
- Update the agent-harness skill's launch setup to accept the compile-selected canonical data directory and append `--data-directory` to both commands. Report which mode/path is under test. Do not infer shared mode merely because packs are missing; a local-export build with missing outputs is a failure.
- Update the Engine, DataPacker, and VisualStudio2026 `AGENTS.md` contracts with the two modes, the read-only validation rule, and the prohibition on canonical-output writes from a worktree.

## Critical files

- `Engine/Source/LaunchOptions.{h,cpp}` — shared `LaunchOptions::dataDirectory` field and `ParseLaunchOptions` trust-boundary parsing for `--data-directory`.
- `Engine/Source/File/FileManager.cpp` — `FileManager::FileManager` selection of the explicit directory or existing executable-relative default before `LoadPackFiles`.
- `DataPacker/Source/Main.cpp` and `DataPacker/Source/FileManager.{h,cpp}` — validation-only command interface and reuse of input/output discovery plus export-job dirty metadata without entering mutation/export orchestration.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `BrokenEngineSandboxServer.vcxproj` — shared/local generated-header include root, local-only DataPacker custom-step condition, and common mode properties across configurations.
- `.agents/skills/compile/SKILL.md` — safe authoritative-primary discovery, explicit mode selection, Release DataPacker freshness/validation, and mode/path reporting.
- `.agents/skills/agent-harness/SKILL.md` — client/server launch commands using the exact data directory selected for the build.
- `Engine/Source/AGENTS.md`, `DataPacker/Source/AGENTS.md`, and `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` — runtime, exporter, and project-layer contracts.

## Out of scope

- Copying multi-gigabyte packs into every worktree or moving authoritative data into a new PC-global store.
- Junctions, symlinks, or hard links from a worktree output to the authoritative output; no worktree DataPacker process may gain a writable alias to canonical packs.
- Changing asset contents, Gaea terrain generation, texture compression, cubemap generation, export fingerprints, `.pack`/`.manifest` layout, `DataHeader::kiVersion`, or generated CRC semantics.
- Relocating the Gaea agent cache or DataPacker's `%TEMP%/DataPacker/<Project>` cache; those are separate concerns.
- Supporting mixed inputs such as local headers with authoritative packs, authoritative headers with local packs, or client/server processes using different data roots in one verification run.
- Changing AppData saves, replay locations, runtime asset hot reload, distribution packaging, or sharing data between PCs/users.

## Acceptance criteria

- An ordinary code worktree builds client and server in shared authoritative mode without invoking Gaea, texture compression, cubemap export, pack aggregation, or any DataPacker output mutation.
- Shared-mode compilation succeeds with no local `Output/Data/DataTypes.h`; compiler includes and runtime `.pack`/`.manifest` reads all resolve from one validated authoritative data directory.
- Hashes, sizes, and timestamps of authoritative generated headers, manifests, and packs are unchanged by a shared-mode build and harness run.
- The read-only validator rejects missing outputs, dirty assets/export versions, `DataHeader` version mismatch, generated-header/manifest CRC-set mismatch, corrupt manifest bounds, and any attempted mix of local and authoritative outputs. Rejection occurs before any exporter or migration side effect.
- Both worktree-built executables accept the same explicit absolute `--data-directory`; client loads the complete authoritative set and server loads its required Islands data. Omitting the option preserves executable-relative behavior.
- The agent harness launches and controls the worktree client/server against the compile-selected shared directory, with logs proving the resolved path and successful required-manifest loads.
- A data/export worktree explicitly selects local export mode, builds Release DataPacker, produces only worktree-local headers/manifests/packs, compiles against those headers, and runs both harness processes against those same local outputs.
- Neither skill guesses a fallback data directory. Invalid/missing lifecycle metadata, ambiguous primary discovery, incomplete shared output, or incomplete local export fails with the selected mode and path in the error.
- Compile, runtime, harness, and AGENTS.md documentation use the same two mode names and state which mode owns DataPacker execution and output writes.

## Notes

- Recommended mode surface: MSBuild properties such as `DataBuildMode=Shared|Local` plus `GameDataDirectory=<absolute .../Output/Data>`, with the skills owning safe values. Project defaults remain local outside an orchestrated session so normal Visual Studio/installed builds retain current behavior.
- Open decision for grill: implement read-only compatibility validation as a dedicated DataPacker command that reuses export-job dirty checks (recommended), or as a standalone tool. The chosen path must validate the current worktree's exporter versions/fingerprints and generated header text without reaching any mutation path; a filename-only script is insufficient.
- Shared mode is for unchanged data contracts, not merely unchanged source assets. A change to `Common/DataFile.h` or any serialized chunk header/export version selects local export mode even if every asset file is untouched.
- Invariant exposure: runtime startup and dev tooling only. No deterministic simulation/CRC state, replay/network wire format, client/server guard scope, or allocation-tracked main-loop path changes. The plan validates existing asset CRC/pack invariants but does not change them or bump `kiVersion`.
