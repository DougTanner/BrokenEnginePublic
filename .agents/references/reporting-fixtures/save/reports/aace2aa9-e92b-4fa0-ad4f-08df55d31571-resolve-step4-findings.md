Schema: be-agent-report/v1
Requested role: Opus/Terra finding resolver
Actual executor: GPT-5 Codex (Terra role)
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: <WORKTREE>\Documents\Plans\Save\ServerSaveFailureReporting.md; approved delta none; both accepted items classified conformance + non_structural

## Finding Resolution

Mode: fix

### Item Results

- Repository review F001, deduped with adversarial review F001: FIXED
  - Root cause: `BareFilenameParam` extracted the agent JSON value into a length-aware `std::string`, but did not reject embedded NUL before passing `utf8.c_str()` to the pointer-source `std::filesystem::path` constructor. Direct inspection confirmed that `IsWindowsReservedDeviceBasename` compared the full sized basename while path construction could observe only the prefix, so `"NUL\u0000.save"` bypassed reserved-name validation and became path `NUL`. External report `Temp/AgentReports/<GUID>-verify-external-claims.md` A001 VERIFIED both JSON preservation and path truncation premises.
  - Change: `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp:46-65` now rejects any embedded NUL immediately after the non-empty check and before reserved-name validation or path construction. Existing separator, `..`, colon, reserved-name, and successful path semantics remain unchanged.
  - Verification: Re-read both `CommandSave` and `CommandLoad` call paths; neither can construct a filesystem path from an embedded-NUL agent value. `git diff --check` passed. Targeted server Debug compilation of `AgentCommandsServer.cpp` plus affected shared-header consumers passed with exit 0; client Debug shared-header reachability compilation also passed with exit 0. Full compile evidence: `Temp/AgentReports/<GUID>-compile-resolve-step4.md`.

- Repository review F002, deduped with adversarial review R001: FIXED
  - Root cause: `DifferenceStreamWriter::Save` called `FileManager::RemoveFile` sequentially for each replay sibling after a write failure. Direct inspection confirmed `FileManager::RemoveFile` uses the throwing `std::filesystem::remove(path)` overload, so the first OS removal failure could escape `Save`, skip remaining sibling cleanup, and prevent `GameSaveLoad::SyncReplayTick` from continuing to later coordinate writers, clearing `mReplayWriters`, writing metadata, and emitting the aggregate failure diagnostic. External report `Temp/AgentReports/<GUID>-verify-external-claims.md` A002 VERIFIED the throwing overload behavior.
  - Change: `Engine/Source/File/DifferenceStream.h:153-177` routes each sibling cleanup through a local helper that catches and logs `std::filesystem::filesystem_error` independently. Every existing sibling is still attempted, the first failed write remains the reported failure, serialized bytes and write order are unchanged, and `Save` still returns `false` after any component write failure.
  - Verification: Re-read the complete fixed cleanup block and `GameSaveLoad::SyncReplayTick` replay-stop loop. A removal OS error is now contained per sibling, so remaining cleanup and the caller's later writers, clear, metadata write, and aggregate failure path remain reachable. `git diff --check` passed. Targeted client and server Debug compilation of all affected translation units passed with exit 0 and no changed-file warnings; Shared data identity remained unchanged. Full compile evidence: `Temp/AgentReports/<GUID>-compile-resolve-step4.md`.

### Files Changed and Regions Touched

- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp` — `BareFilenameParam` embedded-NUL trust-boundary validation.
- `Engine/Source/File/DifferenceStream.h` — `DifferenceStreamWriter::Save` failed-sibling cleanup only.

### Verification Summary

- `git diff --check`: pass.
- BrokenEngineSandbox client Debug selective compile: final success, exit 0; affected target `Engine/Source/Pch.cpp` reparsed the shared `DifferenceStream.h` signature; no final errors or changed-file warnings.
- BrokenEngineSandbox server Debug selective compile: success, exit 0; targeted `Engine/Source/Pch.cpp`, `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`, and `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp`; no final errors or changed-file warnings.
- Data mode: Shared, `RunDataPacker=false`; selected data hashes remained unchanged after both final builds.
- The first client attempt failed before C++ compilation because the vcxproj pre-build invoked Windows PowerShell 5.1 against `[System.Security.Cryptography.SHA256]::HashData`. Direct PowerShell 7 provisioning succeeded, and the final no-edit retry used `/p:PreBuildEventUseInBuild=false` to skip only the redundant incompatible hook. Exact failure lines, provisioning evidence, build commands, hashes, and final exit codes are retained in the compile report above.

### Residuals

- R001 — Adversarial R002 remains untouched by instruction: replay persistence has no set-level commit/invalidation, so a failed manifest write can leave a loadable mixed-generation replay. This is pre-existing structural work outside accepted fix scope; owner/action: main session route through C++ Code Change Process step 11 `/create-follow-up-plans` if not already queued.
- R002 — Compile infrastructure: the game vcxproj pre-build event invokes Windows PowerShell 5.1, but `.agents/scripts/Provision-WorktreeThirdParty.ps1` uses `SHA256.HashData`, unavailable there. Final verification passed after direct `pwsh` provisioning and disabling the redundant hook for the build invocation; repository files were not changed. Owner/action: main session route the infrastructure incompatibility for separate correction/follow-up rather than expanding this save fix.
