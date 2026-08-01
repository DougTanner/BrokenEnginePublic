# Explicit Microsoft PREfast verification mode

Use this mode only when an approved plan explicitly requires Microsoft PREfast verification. Retain every ordinary game-build protection the skill states: the rules for taking and releasing the short-lived operation lock, immutable prebuilt WorktreeCli, worktree provisioning and lifecycle validation, WorktreeCli target serialization, synchronous foreground execution, data-mode selection and the authoritative `@DataProperties` from [runtime-data-mode.md](runtime-data-mode.md), complete-data checks, and selected/primary oracle verification. Do not invoke MSBuild or `/analyze` outside WorktreeCli.

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` owns the Microsoft code analysis explanation in its "Microsoft code analysis" bullet: the two Release paths selected by `RunCodeAnalysis`, the `EnablePREfast` gate, the rule set, `CodeAnalysisTreatWarningsAsErrors`, and the `CodeAnalysisNeverReportRuleErrors` prohibition. Two facts this mode adds on top of it:

- Report "analysis executed" and "policy passed" as separate facts — a zero exit alone establishes only the second.
- `RunNativeCodeAnalysis` is an incremental target, so a green incremental run can mean "skipped as up-to-date"; require a rebuild, or positive evidence that this run regenerated the merged `.nativecodeanalysis.xml`, before reporting that analysis ran.

Force Clang-Tidy off and Microsoft code analysis on only for the Release target commands below. Do not override the projects' warnings-as-errors settings, the rule set, or `CodeAnalysisTreatWarningsAsErrors`, and never pass `CodeAnalysisNeverReportRuleErrors` — it disables error promotion silently:

```powershell
# BrokenEngineSandbox client Release PREfast, then server Release PREfast.
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
& $WorktreeCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Release' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:EnableMicrosoftCodeAnalysis=true' '/p:RunCodeAnalysis=true' '/verbosity:minimal'
```

`EnableMicrosoftCodeAnalysis=true` is an extra safety measure here: the toolchain forces analysis off only on an explicit `false`, and the Release configurations set it nowhere, so passing it changes nothing today. Keep it so an upstream default change cannot silently disable the mode.

A policy failure surfaces as a nonzero MSBuild exit after the link step, with the executable already produced — analysis runs through `AfterBuildLinkTargets`. Existing binaries after a failed run are expected, not a partial success. A failing run also does not write `*.lastcodeanalysissucceeded`, so the failure correctly re-reports on the next build until it is fixed. Report the matched diagnostics from the structured `diagnostics` array, noting `diagnosticsTruncated: true` and pointing at the retained log when the cap elides the rest.

Outside this explicitly authorized mode, keep `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; never infer PREfast authorization from a routine compile, rebuild, or link-error check.
