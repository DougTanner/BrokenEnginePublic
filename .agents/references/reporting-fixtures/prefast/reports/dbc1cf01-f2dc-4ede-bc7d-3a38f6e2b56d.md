Schema: be-agent-report/v1
Requested role: Luna (resolve-findings fix role)
Actual executor: Codex GPT-5
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Restore serialized Release PREfast builds; no approved delta. Accepted R003 plus R001/R002 from Temp/AgentReports/<GUID>.md; prior reports Temp/AgentReports/<GUID>.md and Temp/AgentReports/<GUID>.md; edit scope limited to Write-WorktreeCliLedger.

## Finding Resolution

Mode: fix

Caller classification: Intent conformance; Scope non_structural.

PLAN DELTA REQUIRED: no.

### Root-Cause Confirmation

Suspected root cause: Write-WorktreeCliLedger called `IO.File.Move(String,String,Boolean)`, which exists in pwsh's .NET runtime but not in the .NET Framework runtime used by Windows PowerShell 5.1. Every live ledger refresh therefore failed in Windows PowerShell after the already-fixed hash and JSON compatibility paths, before provisioning or C++ compilation.

Confirmed:

- Source report `<GUID>.md` records complete Windows PowerShell provisioning reaching `Write-WorktreeCliLedger` and failing with `Cannot find an overload for "Move" and the argument count: "3"`; pwsh provisioning passed.
- Direct inspection found the three-argument call in `Write-WorktreeCliLedger` at the assignment start.
- Direct method probes confirmed Windows PowerShell Desktop 5.1.26100.8655 exposes `File.Move(String,String)` but not the three-argument overwrite overload; pwsh Core 7.6.3 exposes both.
- The failure was deterministic after the stream had been flushed and disposed, at the final ledger transition.

### Item Results

- R003: FIXED
  - Root cause: confirmed as the unavailable three-argument `File.Move` overload described above.
  - Change: after the same-directory temp file is fully written and flushed, `Write-WorktreeCliLedger` now uses `File.Replace(temp,destination,null)` when the ledger already exists and two-argument `File.Move(temp,destination)` for first creation. PowerShell's `NullString.Value` sentinel supplies a true null backup path because both hosts coerce ordinary PowerShell `$null` to an empty string for this overload. The existing temp cleanup remains in `finally`; the caller-owned mutex, JSON validation, and transition flow are unchanged. There is no delete-before-move gap.
  - Verification: isolated first-create and overwrite transitions passed under Windows PowerShell 5.1 and pwsh 7.6.3; complete live provisioning passed under both hosts; output inventories were unchanged; whole-module static API audit found no remaining unavailable call.
- R001: UNRESOLVED
  - Root cause: client Release PREfast was previously blocked by R003; the provisioning blocker is fixed.
  - Change: no client-build-specific change.
  - Verification: full client build was not run, per caller instruction; caller retains the Release PREfast rerun.
- R002: UNRESOLVED
  - Root cause: server Release PREfast was previously blocked by R003; the provisioning blocker is fixed.
  - Change: no server-build-specific change.
  - Verification: full server build was not run, per caller instruction; caller retains the Release PREfast rerun.

### Files Changed and Regions Touched

- `.agents/scripts/WorktreeCliSessionExclusion.psm1` — `Write-WorktreeCliLedger`, lines 109-121; replacement transition only.

Assignment-owned tracked mutation: one file, one prior three-argument move statement replaced by an existing-ledger `File.Replace` branch and absent-ledger two-argument `File.Move` branch. Earlier session changes in `Get-WorktreeCliRepositoryIdentity` and `Read-WorktreeCliLedger`, plus all unrelated tracked edits, were preserved and not edited.

### Scoped Verification

1. Root-cause API probe: PASS.
   - Windows PowerShell Desktop 5.1.26100.8655: no three-argument `File.Move`; two-argument `File.Move` and three-argument `File.Replace` present.
   - pwsh Core 7.6.3: both `File.Move` overloads and three-argument `File.Replace` present.
2. Null-backup binder probe: PASS after using the host-supported true-null sentinel.
   - Ordinary `$null` and `[string]$null` were both coerced to an empty backup path and rejected (`The path is not of a legal form` on Windows PowerShell; `The path is empty` on pwsh).
   - `[System.Management.Automation.Language.NullString]::Value` passed a true null backup path and `File.Replace` atomically replaced the destination with the source under both hosts.
3. Isolated first-create and overwrite transitions: PASS under both hosts.
   - Windows PowerShell 5.1: first-create ledger 78 bytes, overwrite ledger 82 bytes, strict UTF-8 decode passed, `Read-WorktreeCliLedger` validation passed for both states, first-create temp residue 0, overwrite temp residue 0.
   - pwsh 7.6.3: the same 78-byte and 82-byte valid canonical JSON states, validation results, and zero-residue results.
   - First JSON: `{"version":1,"repository":"first-repository","sessions":[],"maintenance":null}`.
   - Overwrite JSON: `{"version":1,"repository":"overwrite-repository","sessions":[],"maintenance":null}`.
4. Complete provisioning with live wrapper owner `<GUID>`: PASS.
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/Provision-WorktreeThirdParty.ps1 -RepositoryRoot ROOT`: exit 0.
   - `pwsh.exe -NoProfile -File .agents/scripts/Provision-WorktreeThirdParty.ps1 -RepositoryRoot ROOT`: exit 0.
   - Both emitted: `Shared worktree dependencies validated for '<WORKTREE>' using primary '<USER_HOME>\Documents\BrokenEnginePublic'.`
   - Live ledger after both runs: version 1, three sessions, 1,042 bytes, repository `<USER_HOME>\Documents\BrokenEnginePublic\.git`, temp residue 0.
5. Primary output preservation: PASS.
   - Recursively inventoried primary `ThirdParty/Prebuilts/Platforms/VisualStudio2026/Output`, primary `Tools/WorktreeCli/Platforms/VisualStudio2026/Output`, and primary/shared `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output` before and after both provisioning runs.
   - Before: 91 entries, 68 files, 2,102,408,053 bytes, aggregate path/type/attribute/link-target/length/per-file-SHA-256 digest `49ffa7c293c2b98ba583a434efe56ac0133a911c67aecfa9f4913de21b053912`.
   - After: 91 entries, 68 files, 2,102,408,053 bytes, identical digest `49ffa7c293c2b98ba583a434efe56ac0133a911c67aecfa9f4913de21b053912`.
6. Whole-module static .NET API audit: PASS.
   - Inspected the complete module's static calls and probed 18 call categories in both Windows PowerShell 5.1 and pwsh 7.6.3: path, hashing, encoding, conversion, Windows identity, process, string validation, date parsing, GUID parsing, generic HashSet construction, file open/replace/two-argument move, NullString sentinel, mutex construction, Math, StringBuilder construction, and the compiled TrackedProcess static entry point.
   - Windows PowerShell: 18 checks, 0 failed, unavailable=none.
   - pwsh: 18 checks, 0 failed, unavailable=none.
   - No remaining static .NET compatibility residual was found.
7. `git diff --check -- .agents/scripts/WorktreeCliSessionExclusion.psm1`: PASS, no output.
8. Complete fixed region and direct call path re-read: PASS. The stream still flushes to disk before transition; existing-ledger replacement and first-create move are same-directory operations; cleanup remains unconditional; `Invoke-LedgerTransition` still holds the named mutex across read/action/write.
9. Full client/server builds: not run, per caller instruction.

### Residuals

- R001: caller must rerun client Release PREfast and evaluate the requested warning codes now that Windows PowerShell provisioning passes.
- R002: caller must rerun server Release PREfast and evaluate the requested warning codes now that Windows PowerShell provisioning passes.

### Decision Index

- X001 | fixed | `.agents/scripts/WorktreeCliSessionExclusion.psm1:109` | R003 atomic ledger compatibility fix applied; isolated transitions and complete live provisioning passed under Windows PowerShell 5.1 and pwsh 7.6.3 with no temp residue or output mutation.
- R001 | unresolved | client Release PREfast | Caller-owned full build remains to be rerun; no full build was run by instruction.
- R002 | unresolved | server Release PREfast | Caller-owned full build remains to be rerun; no full build was run by instruction.
