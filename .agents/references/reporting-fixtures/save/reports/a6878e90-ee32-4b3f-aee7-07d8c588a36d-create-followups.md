Schema: be-agent-report/v1
Requested role: Opus/Terra C++ Code Change Process step-11 planner
Actual executor: GPT-5 Codex (Terra role)
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Documents/Plans/Save/ServerSaveFailureReporting.md; approved delta none; create step-11 follow-up plans for three supplied residual candidates

# Create Follow-up Plans

Result: PASS

## Validation and deduplication

- C001 mixed-generation replay persistence remains unresolved. `GameSaveLoad::SyncReplayTick` writes the fixed-name grid at recording start and the valid manifest before coordinate writers/metadata at stop (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:304-365`); `SaveLoadReplay` accepts that manifest before constructing fixed-name coordinate readers (`:197-267`). Individual atomic files have no set-level invalidation/commit state, so a preserved old manifest can authorize newer components. This is structural and outside the active plan's individual-result propagation boundary.
- C002 recorded-coordinate stop remains unresolved. Writers snapshot `mCoordFrames` at start (`GameSaveLoad.cpp:298-316`); `ServerSession::SyncActiveFrames` can erase a recorded coordinate (`ServerSession.cpp:401-417`); update skips an absent coordinate (`GameSaveLoad.cpp:378-388`), while stop calls throwing `CurrentFrame(rCoord)` (`:349-353`). A lifetime/end-state policy is required to uphold exhaustive stop, clear writers, and aggregate reporting.
- C003 PowerShell 5.1 provisioning remains unresolved. All six client/server vcxproj pre-build events call `powershell.exe`; imported `AgentCliSessionExclusion.psm1:15-16` uses `SHA256.HashData` and `Convert.ToHexString`. Step-8 compilation reproduced the 5.1 API failure, while direct PowerShell 7 provisioning plus `PreBuildEventUseInBuild=false` allowed later builds to pass.
- Repository-wide searches across every live `Documents/Plans/**/*.md`, both Order files, dependencies, and file groups found no other plan owning any candidate's root cause and implementation boundary. `Save/ServerSaveFailureReporting.md` overlaps the replay regions but explicitly lacks both set-level generation state and recorded-coordinate lifetime policy. No plan owned the PowerShell runtime mismatch.
- The three candidates were split because they have independent root causes, implementation boundaries, acceptance strategies, and can land independently. C002 is a decision plan because end-Frame ownership changes frame-lifetime shape; it presents eviction handoff, writer-owned snapshot, and frame pinning, recommending eviction handoff for grill consideration.

## Queue coordination

- Canonical repository locator: `<USER_HOME>\Documents\BrokenEnginePublic\.git`; order locator: `Documents/Plans/Order.md`.
- Mutation and verification used the provisioned worktree AgentCli. Every acquired queue lock returned `ownedByRequester: true` with a sorted claims array. No new target path was claimed.
- The authoritative final locked revalidation used owner `<GUID>`, observed three unrelated live row claims, re-read Order.md and implicated plans, reran duplicate checks, and completed with unlock exit 0. Earlier no-op/stale-anchor attempts and the successful mutation window also released their queue owners with exit 0 before any research/reporting continued.
- Final checks: all three plan paths exist; each has exactly one Order row; all required headings exist; every row score satisfies `Effort - Impact + Risks`; score placement is valid; referenced plans exist; duplicate search is clean; `git diff --check -- Documents/Plans` exits 0.

## Created

- C001 -> `Documents/Plans/Save/ReplayGenerationCommitAtomicity.md` — queues the acceptance gap that in-progress or failed replay persistence must invalidate the prior commit marker and cannot leave fixed-name mixed generations loadable. Medium / Effort 3 / Impact 4 / Risks 3 / Score 2.
- C002 -> `Documents/Plans/Save/RecordedCoordReplayStopPolicy.md` — queues the accepted structural gap requiring explicit last-complete-end-Frame ownership across recorded-coordinate eviction so stop attempts all writers/metadata, clears writers, and logs aggregate outcome without throwing. Medium / Effort 3 / Impact 4 / Risks 3 / Score 2. Decision plan (present options).
- C003 -> `Documents/Plans/Tools/PowerShell51ProvisioningCompatibility.md` — queues the infrastructure gap requiring identical repository lock identity and successful provisioning under Windows PowerShell 5.1 and PowerShell 7, with ordinary builds no longer disabling pre-build. Quick Win / Effort 1 / Impact 3 / Risks 1 / Score -1.

## Updated existing

- none

## Duplicate mappings

- none

## Order.md

- `| [Tools/PowerShell51ProvisioningCompatibility.md](Tools/PowerShell51ProvisioningCompatibility.md) | Quick Win | 1 | 3 | 1 | -1 | Replace PowerShell-7-only SHA-256/hex APIs with cross-runtime equivalents preserving exact lock identity, so Windows PowerShell 5.1 vcxproj provisioning and serialized builds succeed without disabling PreBuildEvent |`
- `| [Save/ReplayGenerationCommitAtomicity.md](Save/ReplayGenerationCommitAtomicity.md) | Medium | 3 | 4 | 3 | 2 | Make manifest the replay-set commit marker: invalidate before fixed-name overwrite, attempt all data components, publish valid manifest only after total success, and prevent loadable mixed generations |`
- `| [Save/RecordedCoordReplayStopPolicy.md](Save/RecordedCoordReplayStopPolicy.md) | Medium | 3 | 4 | 3 | 2 | Decision plan (present options). Own the last complete end Frame across recorded-coord eviction so stop never throws through CurrentFrame, always attempts later components, clears state, and reports aggregate result |`
- Dependencies: none added; all overlap is warning-only.
- File Groups: expanded the `GameSaveLoad.{h,cpp}` group with both replay plans and explicit `SyncReplayTick` reconciliation/reverification; expanded the game `Network/Server/` managers group with the recorded-coordinate plan's conditional `SyncActiveFrames` handoff overlap.

## Files changed + regions touched

- `Documents/Plans/Save/ReplayGenerationCommitAtomicity.md` — new complete plan.
- `Documents/Plans/Save/RecordedCoordReplayStopPolicy.md` — new complete decision plan.
- `Documents/Plans/Tools/PowerShell51ProvisioningCompatibility.md` — new complete plan and new owning area directory.
- `Documents/Plans/Order.md` — three scored rows and two File Groups updates.
- The pre-existing active-session modification to `Documents/Plans/Save/ServerSaveFailureReporting.md` was read but not edited by this skill.

## Residuals

- none
