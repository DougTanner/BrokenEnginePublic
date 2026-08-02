<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T15:22:59.028Z","dependsOn":[]} -->
# AgentTools Candidate Build Documentation

## Context

Documentation names an AgentTools candidate build but never states how to produce one, so every session that changes AgentTools source rediscovers the mechanism through a link failure.

- `Tools/WorktreeCli/AGENTS.md:7`: "source changes use the `/compile` candidate/promotion path".
- `.agents/skills/compile/SKILL.md:38-45` (AgentTools trigger): the rebuilt tools "are promoted through `/finalize-changes`' AgentTools promotion ... This compile run only builds; it never promotes or copies tools."
- `.agents/skills/finalize-changes/references/agenttools.md:22-25`: "After landing, rebuild both tools from the landed commit and promote only with `../scripts/Invoke-AgentToolsPromotion.ps1`, passing the freshly built pair via `-WorktreeCliCandidate` and `-AgentHarnessCandidate`".

No document states where that freshly built pair comes from. `/compile`'s Full-build commands section (`.agents/skills/compile/SKILL.md:113-129`) lists ThirdParty, DataPacker, and the two BrokenEngineSandbox solutions, and no WorktreeCli or AgentHarness command at all.

Building WorktreeCli through the documented driver with its default output fails, because that output is the shared primary Output the running driver itself holds locked. Reproduced this session:

```
LINK : fatal error LNK1104: cannot open file '...\Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe' [...\WorktreeCli.vcxproj]
```

(evidence: `Temp/AgentBuildLogs/worktreecli-20260802T150504248Z-37492.log:11`). `/compile`'s report guidance (`.agents/skills/compile/SKILL.md:150`) already tells the reader that LNK1104 "can mean a client/server process still holds the executable" and to report rather than diagnose it, which is exactly the wrong route here: the holder is the build driver being used to run the build, and the condition is structural, not contention to wait out.

The mechanism this session proved works: keep the same documented `WorktreeCli build` invocation and redirect only the output with an `OutDir` override to a session-local directory, for example `/p:OutDir=<root>\Temp\AgentToolsCandidate\`. That produced a clean build writing `...\Temp\AgentToolsCandidate\WorktreeCli.exe` (evidence: `Temp/AgentBuildLogs/worktreecli-20260802T151018159Z-39532.log:5`), left the shared primary Output byte-identical, and yields exactly the candidate path `Invoke-AgentToolsPromotion.ps1 -WorktreeCliCandidate` expects.

A second discovery belongs with it: `/p:` switches must be passed from PowerShell, not Git Bash. MSYS path mangling rewrites the leading slash of a `/p:...` argument, so the same command that works in PowerShell 7 silently loses its property switches under Git Bash.

Originating gap: proven in this session while building the changed WorktreeCli source; out of scope there because that session's approved boundary was the new read-only `plan list` verb, not the shared build documentation. No live Plan owns this root cause — `Documents/Plans/Agents/LandingBuildEvidenceCompleteness.md` covers build-result envelope reporting and a DataPacker receipt gap, and explicitly puts "AgentTools bootstrap/promotion policy" out of its scope; `Documents/Plans/Agents/SkillScriptMirrorModuleResolution.md` covers script module-path resolution. `plan validate` over the current tree reports `status: valid`, `code: ok`.

## Design

Document the existing, proven mechanism once at its owning site and reference it from the others. Change no script and no build behavior.

1. `.agents/skills/compile/SKILL.md` owns build commands, so the candidate build command belongs there: add the WorktreeCli and AgentHarness `WorktreeCli build` invocations to the Full-build commands section, each carrying the session-local `OutDir` override, alongside the existing `/p:Configuration`, `/p:Platform`, `/p:EnableClangTidyCodeAnalysis=false`, `/p:RunCodeAnalysis=false`, and `/verbosity:minimal` switches the section already standardizes. State plainly why the override exists: the default output is the shared immutable primary Output that the running driver holds locked, so a default-output build fails with LNK1104 by construction. Record the PowerShell-only rule for `/p:` switches at the same place, next to the existing Git Bash guidance in "Determine what to build".
2. Correct the LNK1104 line in the Report results section so it distinguishes the live-executable case it already describes from this structural case, and points at the candidate command instead of leaving the reader to report contention.
3. `Tools/WorktreeCli/AGENTS.md:7` and `.agents/skills/finalize-changes/references/agenttools.md` reference that one owning section rather than restating the command, matching the repository's one-owning-site rule.

The exact candidate directory the documentation names is a naming choice, not an open design question: use the session-local ignored `Temp/` tree the build logs already use, so nothing lands in tracked output and nothing is shared between checkouts.

## Critical files

- `.agents/skills/compile/SKILL.md` — Full-build commands, the AgentTools trigger section, "Determine what to build" (Git Bash guidance), and the LNK1104 line in Report results.
- `Tools/WorktreeCli/AGENTS.md` — the "Executable and Commands" opening paragraph naming the candidate/promotion path.
- `.agents/skills/finalize-changes/references/agenttools.md` — the "rebuild both tools from the landed commit" sentence.
- `Temp/AgentBuildLogs/worktreecli-20260802T150504248Z-37492.log` and `...-20260802T151018159Z-39532.log` — evidence only; ignored, untracked, and not part of the change.

## In scope

- `.agents/skills/compile/SKILL.md`: adding the two AgentTools candidate build commands with the `OutDir` override and their rationale; the PowerShell-only `/p:` note; the corrected LNK1104 report line.
- `Tools/WorktreeCli/AGENTS.md`: pointing its candidate/promotion sentence at the owning `/compile` section.
- `.agents/skills/finalize-changes/references/agenttools.md`: pointing its rebuild sentence at the same owning section.

## Out of scope

- Any change to `WorktreeCli` source, its `build` verb, the MSBuild driver, target serialization, or the build-result schema.
- Any change to `Invoke-AgentToolsPromotion.ps1`, `Wait-AgentToolsQuiescence.ps1`, bootstrap scripts, or promotion, quiescence, backup, and rollback policy.
- Adding a new script, wrapper, or automated candidate-build helper.
- `.vcxproj`/`.props` changes, including making the project itself default to a different output directory.
- Data-mode, oracle, PREfast, DataPacker, and ThirdParty build guidance.

## Risk tier and invariants

Tier 2 — documentation of one tool's build procedure. It changes no code and no coordination mechanism, but it is the instruction other sessions execute against shared AgentTools build output, so it is not treated as mechanical Tier 1. A reviewer escalates to Tier 3 if the accepted edit ends up changing the promotion or bootstrap contract rather than only describing the existing build.

Invariants: the documented candidate command writes only beneath the session-local ignored `Temp/` tree and never into any primary `Platforms/VisualStudio2026/Output` directory; the shared primary AgentTools binaries stay byte-identical across a candidate build; promotion remains the only path that replaces them.

## Acceptance criteria

- Running the documented candidate command verbatim from PowerShell 7 in a linked worktree, with WorktreeCli.exe itself in use, completes with `status` success and exit `0`, and the retained log shows the produced `WorktreeCli.exe` under the documented `Temp/` candidate directory.
- The shared `Tools/WorktreeCli/Platforms/VisualStudio2026/Output/WorktreeCli.exe` and `Tools/AgentHarness/.../AgentHarness.exe` are byte-identical (SHA-256) before and after that candidate build.
- The produced candidate paths are directly usable as `Invoke-AgentToolsPromotion.ps1 -WorktreeCliCandidate` and `-AgentHarnessCandidate` inputs with no further copying step.
- `/validate-skill` passes on the edited `.agents/skills/compile/SKILL.md`, and its `.claude` mirror stays consistent.
- `Tools/WorktreeCli/AGENTS.md` and `.agents/skills/finalize-changes/references/agenttools.md` each reach the command through a reference; the command text exists in exactly one place.
