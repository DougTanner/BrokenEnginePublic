<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T16:54:27.785Z","dependsOn":[]} -->
# Fix: WorktreeCli selective build — delete selected object fails with Windows error 123

## Context
During Change Workflow Step 4, the delegated `/compile` builder used the
provisioned WorktreeCli selective invocation documented at
`.agents/skills/compile/SKILL.md:131-136`:

`WorktreeCli build --files <worktree>\Engine\Source\Agent\AgentCommandServer.cpp -- <worktree>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj /p:Configuration=Debug /p:Platform=x64 ...`

Two client attempts failed before MSBuild with `status:fail`,
`failureKind:tool`, exit code `1`, `msbuild.launched:false`, and
`delete selected object failed (Windows error 123)`. The retained logs are
`Temp/AgentBuildLogs/brokenenginesandbox-20260803T163112557Z-40036.log` and
`Temp/AgentBuildLogs/brokenenginesandbox-20260803T163148962Z-3020.log`.

Normalizing the selected and target paths alone did not fix the failure. The
evaluated `IntDir` contained duplicate `VisualStudio2026\Build` separators
while the project’s `$(ProjectDir)\Build` form was preserved. Supplying an
explicit normalized `/p:IntDir=<worktree>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Build\BrokenEngineSandbox\Debug\`
to the identical selective build allowed it to succeed; the retained success
log is `Temp/AgentBuildLogs/brokenenginesandbox-20260803T163327973Z-33140.log`.

Originating gap: Change Workflow Step 4 `/compile`; the required client/server
build eventually passed only through this workaround and repetition. The
claimed active intent is
`Documents/Plans/Network/AgentActiveSocketTeardownSynchronization.md`; its
`## In scope` names only the Agent command-server teardown files, so
WorktreeCli/build scripts are outside that boundary and this is not an
in-scope blocker.

Session provenance (machine-local; not reproducible after cleanup):
- Client: codex
- Session: 67ae69e6-6c04-4a5b-88ca-21c4a603c637
- Session branch: codex/67ae69e6-6c04-4a5b-88ca-21c4a603c637
- Worktree: .codex\worktrees\BrokenEnginePublic\67ae69e6-6c04-4a5b-88ca-21c4a603c637 —
  never an absolute path, so no home prefix enters the public repo
- Session baseline: `37cb66b21a2f0e47d798acf294eac97c6d18829e`
- Landing commit: `git log --diff-filter=A --format=%H -- <this plan path>`
- Run the review before /cleanup-worktrees removes this worktree: Codex
  transcript discovery requires the producing worktree to remain registered,
  and Claude review requires the exact session id above.

## Design
In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files
- `.agents/skills/compile/SKILL.md` — `## Selective file compile` and its
  documented `--files` invocation/property guidance.
- `Tools/WorktreeCli/BuildCommand.cpp` — `EvaluateProjectCompileItems` and
  `InvalidateSelectedObjects`, which evaluate selected object paths and
  invalidate them before MSBuild.

## In scope
- Root-cause investigation via /next-plan-review with the recorded provenance
- The smallest resulting fix, confined to the files named above: the
  selective-file compile guidance and the
  `EvaluateProjectCompileItems`/`InvalidateSelectedObjects` path used by
  `WorktreeCli build --files`.

## Out of scope
- The landed Agent active-socket teardown change and all
  `Engine/Source/Agent/AgentCommandServer.*` implementation or documentation.
- Unrelated WorktreeCli verbs, MSBuild project/target changes, and
  AgentTools promotion or bootstrap policy.
- Unrelated skills/scripts; any transcript path or transcript text in the repo,
  and any absolute home path.

## Risk tier and invariants
Expected Tier 2 (scoped tool behavior); escalate if the fix reaches
build/bootstrap coordination. Never embed transcript paths or home paths.

## Acceptance criteria
- The recorded symptom no longer reproduces under the documented selective
  invocation without requiring the explicit `IntDir` workaround.
- `/validate-skill` passes for any changed SKILL.md; plan validate exits 0.
