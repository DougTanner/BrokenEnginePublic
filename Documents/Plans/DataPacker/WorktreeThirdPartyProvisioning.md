# Worktree ThirdParty Auto-Provisioning (submodules + prebuilt lib)

## Context

A freshly-created linked Git worktree (created by `/prepare-session-worktree` under `.claude/worktrees/<name>/`) cannot build the client, server, or DataPacker until `ThirdParty` is provisioned by hand. Two independent gaps, both verified this session:

1. **Empty submodule working trees.** Git worktrees do not check out submodule working trees on creation, so every directory named in `.gitmodules` (20 submodules: `DirectXTK`, `tinyobjloader`, `stb`, `SPIRV-Cross`, `StackWalker`, `bc7enc_rdo`, `openexr`, `zlib`, `tinygltf`, `gli`, `glm`, `PerlinNoise`, `imgui`, `mimalloc`, `cmft`, `lz4`, `enet`, `implot`, `meshoptimizer`, `Clipper2`) is empty in the worktree. Engine/DataPacker TUs that include ThirdParty headers fail (`C1083: Cannot open include file: '.../stb/stb_image_write.h'`), and a `ThirdParty.sln` build has no sources.
2. **Missing prebuilt lib.** No `ThirdParty/Prebuilts/Platforms/VisualStudio2026/Output/ThirdParty.$(Configuration).lib` exists in the worktree. The client/server pre-build step (`BrokenEngineSandbox.vcxproj` / `BrokenEngineSandboxServer.vcxproj` `<PreBuildEvent>`, three configs each) opens with `if not EXIST "…ThirdParty.$(Configuration).lib" ( "$(DevEnvDir)devenv" …ThirdParty.sln /Build … )`. Outside the VS IDE `$(DevEnvDir)` is empty, so the macro expands to `"devenv"` (not on PATH) → `exited with code 9009` (`MSB3073`), failing both client and server before any engine TU compiles. `DataPacker.vcxproj` carries the identical `if not EXIST … ThirdParty.lib` PreBuildEvent (two configs), so DataPacker hits the same wall.

Manual workaround performed this session (the thing to automate away): `git submodule update --init` in the worktree (offline — objects already in the primary's `.git/modules/ThirdParty/*`), which additionally failed for `SPIRV-Cross` with `Filename too long` (Windows `MAX_PATH` 260 exceeded — the long `.claude/worktrees/<name>/` prefix plus SPIRV-Cross's deeply-nested MSL test-reference filenames), needing `git config core.longpaths true` then re-checkout; then copying the primary's prebuilt `ThirdParty.Release.lib` (+`.pdb`) into the worktree `Output/` to satisfy the `if not EXIST` gate and skip a multi-minute rebuild (valid only because all 20 submodule SHAs matched the primary).

**Existing mechanism to extend.** DataPacker already provisions a worktree with **validated Windows directory symlinks from the primary checkout**: `FileManager::InitializeWorktreeOutputs()` (`DataPacker/Source/FileManager.cpp`) detects a linked worktree by comparing `git rev-parse --git-dir` against `--git-common-dir` (differing ⇒ linked), validates the worktree metadata via `git worktree list --porcelain -z` to recover the primary root, then symlinks the missing `Output/Data` and `Output/../Attribution` roots to the primary with `CreateSymbolicLinkW(dest, src, SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)`. It fails closed on unexpected reparse points (`IsRecognizedLinkRaw` / `RejectUnvalidatedReparse`) and no-ops when the root is already a recognized primary link. The MSBuild side already detects a linked worktree the same way this plan needs: `DataRepositoryIsLinkedWorktree = $([System.IO.File]::Exists('$(DataRepositoryRoot)\.git'))` (a linked worktree's `.git` is a FILE; the primary checkout's is a DIRECTORY), gating `RunDataPacker`.

**Acceptance gap.** Every `/prepare-session-worktree` session currently requires the manual submodule-init + long-paths + lib-copy sequence above before any build succeeds. This plan makes that automatic by reusing the same worktree-safe symlink approach at a ThirdParty pre-compile step (mirroring how client/server already invoke the ThirdParty build).

## Design

Add a **worktree ThirdParty provisioning step** that runs, gated on linked-worktree detection only, before the existing `if not EXIST …ThirdParty.lib` check in the ThirdParty pre-compile step. When run in a linked worktree it symlinks, from the primary checkout into the worktree:

- **the 20 ThirdParty submodule working-tree directories** (each `.gitmodules` `path`), and
- **the ThirdParty Prebuilts Output** (the directory holding `ThirdParty.$(Configuration).lib` / `.pdb`), gated on a submodule-SHA-match guard (below).

Symlinking the submodule directories from the primary (already checked out, short path) **sidesteps the SPIRV-Cross `MAX_PATH` re-checkout failure entirely** — no nested ThirdParty files are ever written into the long worktree path; they are read through the link from the primary's short path. It is also **offline**: it reads the primary's already-populated working trees, never touching `.git/modules` objects or the network (unlike `git submodule update --init`).

Wiring:
- The provisioning command is **prepended to the existing `<PreBuildEvent>` `<Command>`** in `BrokenEngineSandbox.vcxproj`, `BrokenEngineSandboxServer.vcxproj` (three configs each), and `DataPacker.vcxproj` (two configs), gated on `DataRepositoryIsLinkedWorktree == True`. In the primary checkout the gate is False → the step is a no-op and the current build path is untouched.
- Provisioning runs **before** the `if not EXIST …ThirdParty.lib` gate so that, in the common SHA-match case, the symlinked Output lib satisfies the gate and no `devenv` build is attempted (killing the 9009). Submodule symlinks make header includes resolve for the engine/DataPacker TUs that follow.
- **Primary-root discovery** mirrors DataPacker: `git -C "$(DataRepositoryRoot)" worktree list --porcelain` first `worktree ` record (or `git rev-parse --path-format=absolute --git-common-dir` with the trailing `/.git` stripped).

**Prebuilt-lib SHA guard.** Sharing the primary's prebuilt Output is valid only when the worktree's baseline ThirdParty submodule SHAs match the primary's. Compare per submodule (e.g. `git rev-parse HEAD:ThirdParty/<sub>` in worktree vs primary, or `git submodule status`). If all match, symlink the Output directory (read-only sharing is safe — `ThirdParty/` is "do not modify," and ThirdParty builds write intermediates/outputs under `Prebuilts/`, never back into submodule source dirs). If any differ, **skip the Output-lib symlink** and leave the existing behavior (the `if not EXIST` gate falls through to the `devenv` build path, which still requires the IDE / manual provisioning for a divergent pin) — document this as the fallback so a divergent worktree never links a stale lib.

**Idempotency.** No-op when the worktree is already provisioned: skip a submodule symlink whose target directory is already populated (real checkout or existing valid link); validate any existing reparse point fail-closed before reuse (mirror DataPacker's `IsRecognizedLinkRaw`). The lib symlink is naturally idempotent via the downstream `if not EXIST` gate.

**Privilege.** Windows symlink creation needs elevation unless Developer Mode is on. DataPacker's proven path passes `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE`; a `cmd` `mklink /D` does **not** set that flag and still requires elevation even under Developer Mode. Reuse DataPacker's mechanism rather than reinventing with `mklink`. The exact reuse form is the open architectural decision below.

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` — client `<PreBuildEvent>` `<Command>` (Debug/Profile/Release); prepend the provisioning step; `DataRepositoryIsLinkedWorktree` / `DataRepositoryRoot` gate properties already present.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj` — server mirror of the same PreBuildEvent (kept parallel with the client).
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` — DataPacker `<PreBuildEvent>` (Debug/Release) carrying the same `if not EXIST …ThirdParty.lib` gate; add the gate property + provisioning step (DataPacker.vcxproj does not currently define `DataRepositoryIsLinkedWorktree`).
- `FileManager::InitializeWorktreeOutputs` / the `CreateSymbolicLinkW(… ALLOW_UNPRIVILEGED_CREATE)` + `IsRecognizedLinkRaw` / `RejectUnvalidatedReparse` helpers in `DataPacker/Source/FileManager.cpp` — the reference symlink mechanism; source of the extract-vs-reimplement decision.
- `.gitmodules` (repo root) — the authoritative list of 20 submodule `path`s to provision (read-only input).
- New provisioning artifact — an inline PreBuildEvent script **or** a small ThirdParty-free helper (Option A/B below).
- `DataPacker/Source/AGENTS.md` — documents the worktree output-linking contract; extend it (or add a build-tooling note) to describe ThirdParty worktree provisioning.

## Out of scope

- The DataPacker `Output/Data` and `Output/Attribution` worktree symlinks that already work (`InitializeWorktreeOutputs` / `EnsureLocal` copy-on-write) — referenced only as the pattern to reuse, not modified in behavior.
- Any change to ThirdParty submodule pins/versions or to the ThirdParty build itself (`ThirdParty.sln` / `ThirdParty.vcxproj`).
- The AgentCli build driver (`Tools/AgentCli/BuildCommand.cpp`) and the `/compile` skill.
- Fixing the `$(DevEnvDir)`-empty `devenv` 9009 for the genuine ThirdParty-rebuild case (divergent-SHA worktrees) — those remain on the existing (manual / IDE) path; this plan only removes the need to rebuild when SHAs match.
- Worktree creation itself (`/prepare-session-worktree`) — this provisioning is invoked from the build, not the worktree-create step.

## Acceptance criteria

- From a freshly-created linked worktree with empty `ThirdParty/` submodule trees and no `Output/ThirdParty.*.lib`, building client, server, and DataPacker succeeds with **no** manual `git submodule update --init`, `core.longpaths`, or lib copy — the pre-build provisioning links submodule dirs + the prebuilt Output from the primary, the `if not EXIST` gate passes, and no `devenv` 9009 occurs.
- Building in the **primary checkout** is byte-for-behavior unchanged (provisioning gate is False → no-op).
- Re-building an already-provisioned worktree no-ops the provisioning (idempotent; no relink churn, no reparse-validation failure).
- A worktree whose branch pins a **different** ThirdParty submodule SHA does not silently link a stale prebuilt lib (Output symlink skipped; documented fallback).
- Provisioning runs offline (no network) and does not trigger the SPIRV-Cross `MAX_PATH` failure.

## Notes

- **Invariant exposure:** build/dev-tooling only. No runtime, determinism/CRC sim path, `kiVersion`/`.pack` layout, replay, wire protocol, allocation-tracked, or shader exposure. Changes are confined to vcxproj `<PreBuildEvent>` regions and (per the decision below) a provisioning script or small helper + an AGENTS.md note. Fully reversible; manual provisioning remains the fallback.
- **Verification:** create a throwaway linked worktree from the primary, clean the worktree `ThirdParty/` submodule dirs + `Output`, build client + server + DataPacker via the normal driver, confirm success; then build in the primary checkout to confirm the no-op; then re-build the worktree to confirm idempotency. No agent-harness/live-sim step needed (no runtime surface).

Open decisions to resolve at grill:
- **(architectural) Mechanism location.** The proven `CreateSymbolicLinkW(… ALLOW_UNPRIVILEGED_CREATE)` path lives in DataPacker's C++, but DataPacker.exe itself depends on ThirdParty to build, so it cannot be the runtime that provisions ThirdParty (bootstrap/chicken-and-egg) — the step must run in the PreBuildEvent before any TU compiles. Options: **A** — inline the provisioning in the PreBuildEvent (PowerShell `New-Item -ItemType SymbolicLink`, which sets the unprivileged flag under Developer Mode; zero new build artifacts, but re-implements the reparse-validation/idempotency/SHA-guard logic in script). **B** — a tiny standalone **ThirdParty-free** helper `.exe` (its own vcxproj, no ThirdParty link) invoked from the PreBuildEvent, reusing an extracted shared `CreateSymbolicLinkW` + reparse-validation helper factored out of `FileManager.cpp` (proven privilege path, testable, but adds a build artifact + a first-build bootstrap for the helper itself). Recommend **B** if privilege-robustness/validation fidelity matter; **A** if minimizing new build machinery is preferred.
- **Prebuilt Output granularity + SHA guard form.** Symlink the whole `Prebuilts/Platforms/VisualStudio2026/Output` directory vs only the `ThirdParty.$(Configuration).lib` (+`.pdb`); and the SHA-comparison form (`git rev-parse HEAD:ThirdParty/<sub>` per submodule vs `git submodule status` diff). Recommend directory symlink of `Output` (mirrors DataPacker's directory-symlink form) gated on an all-submodule SHA match.
- **Submodule symlink granularity.** Per-submodule directory symlinks (the 20 `.gitmodules` paths) — recommended, because `ThirdParty/Prebuilts/` and other tracked non-submodule ThirdParty files are already present in the worktree and must stay worktree-local — rather than symlinking the whole `ThirdParty/` tree.
- **Overlap (warning-only):** edits the `DataPacker.vcxproj` PreBuildEvent region (see File Groups — the two DataPacker sibling-TU plans touch that file's ClCompile membership, a disjoint XML region) and the client/server vcxproj PreBuildEvent regions (line-disjoint from `Network/Refactor_SessionBaseCollapse.md`'s ClCompile-membership flips in those same files). No logic conflict; refresh citations if co-scheduled.
