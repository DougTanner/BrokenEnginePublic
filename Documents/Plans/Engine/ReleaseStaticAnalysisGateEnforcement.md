# Enforce Release Microsoft Static-Analysis Gate

## Context

The explicit Release Microsoft PREfast mode now invokes both client and server builds with `EnableMicrosoftCodeAnalysis=true` and `RunCodeAnalysis=true`, while the Release project configurations set `TreatWarningAsError=true`. Empirical verification showed that this does not make Microsoft analysis diagnostics fail the build: client and server invocations returned exit code 0 while emitting C6385, C6001, C6387, C26498, and C26459 diagnostics across the verification rounds.

The warning-cleanup plan removed the targeted C6385/C6001/C6387 findings, but a future analyzer regression can still pass the advertised warnings-as-errors gate. The remaining C26498/C26459 output also proves that any true enforcement mechanism needs an explicit policy for existing unrelated diagnostics rather than assuming a warning-clean tree.

This is a decision plan. Resolve the enforcement mechanism and diagnostic policy in `/external-grill-plan` before implementation.

## Design

1. Verify the supported Microsoft/MSBuild analysis-failure controls against authoritative documentation and the installed Visual Studio toolchain. Do not infer that compiler `TreatWarningAsError` covers analyzer diagnostics.
2. Choose one enforcement contract:
   - **A — native analyzer enforcement (preferred when verified):** configure the supported Microsoft analysis warnings-as-errors mechanism for both Release projects and prove that the selected analyzer diagnostics make MSBuild return nonzero.
   - **B — deterministic diagnostic scan-and-fail:** add one compile-skill-owned wrapper that runs the serialized AgentCli build, preserves live output and the original MSBuild exit code, recognizes canonical Microsoft analysis diagnostic records, and returns nonzero when the selected policy matches. Keep AgentCli's generic build command unchanged unless inspection proves the contract cannot be implemented reliably at the skill boundary; an AgentCli change requires its primary-maintenance rebuild flow.
3. Define one explicit client/server diagnostic policy. Choose either all Microsoft analysis warnings or a documented selected-code set. If existing C26498/C26459 diagnostics remain allowed, enumerate a narrow baseline/allowlist that cannot hide newly introduced codes or new occurrences; do not use broad textual suppression.
4. Apply the same contract to both explicit Release PREfast invocations. Ordinary builds continue to pass `EnableClangTidyCodeAnalysis=false` and `RunCodeAnalysis=false`.
5. Align `.agents/skills/compile/SKILL.md` terminology and reporting with the implemented semantics: distinguish “analysis executed” from “policy passed,” require the enforcement exit status, and report matched diagnostics when the policy fails.
6. Verify the failure path with a controlled temporary analyzer-warning probe in each target, then revert the probe before final-tree verification. Verify the clean/policy-baselined path returns zero without manual log inspection.

## Critical files

- `.agents/skills/compile/SKILL.md` — explicit PREfast invocation, enforcement terminology, and reporting contract.
- `.agents/skills/compile/scripts/` — option-B wrapper location if deterministic scanning is selected.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` — client Release Microsoft analysis configuration.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj` — server Release Microsoft analysis configuration.
- `Tools/AgentCli/BuildCommand.cpp` — conditional only if option B cannot preserve output and exit semantics at the compile-skill boundary.

## Out of scope

- Cleaning unrelated C26498/C26459 call sites solely to obtain a warning-free build.
- Enabling Clang-Tidy or changing ordinary Debug/Profile/Release build behavior.
- Changing runtime engine/game behavior, simulation state, or asset output.
- Generalizing AgentCli into a diagnostic-policy framework beyond this explicit PREfast contract.

## Acceptance criteria

- The selected enforcement mechanism and diagnostic policy are documented with authoritative/toolchain evidence.
- Both client and server explicit Release PREfast commands return nonzero when a controlled Microsoft analysis diagnostic matches the policy.
- Both commands return zero when no diagnostic matches the policy, without a human scanning console output.
- Existing C26498/C26459 handling is explicit and narrow; newly introduced codes or occurrences cannot silently inherit an unrelated allowance.
- MSBuild failures remain failures and their diagnostics remain visible through the enforcement wrapper.
- Ordinary builds still disable `RunCodeAnalysis`; Clang-Tidy remains disabled.
- Temporary verification probes are absent from the final tree.
- No unit tests are added.

## Notes

- **Risk:** build/workflow only. The change intentionally alters explicit PREfast command exit semantics and may block verification when policy-matching diagnostics exist.
- **Determinism / CRC:** none.
- **Client / server:** mirrored build-policy change; no runtime affinity change.
- **Wire / save / replay / `.pack` / shader:** none.
- **Validation:** run `/validate-skill` if the compile skill or bundled scripts change; use `/compile` for the controlled client/server Release PREfast verification.
