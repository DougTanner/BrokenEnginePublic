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

Root cause (proven from source in a later session's Step 4 `/compile`
diagnosis; the symptom reproduces for the BrokenEngineSandbox client
independent of which file is selected):

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj:74,86,99`
  define `<IntDir>$(ProjectDir)\Build\$(ProjectName)\$(Configuration)\</IntDir>`.
  `$(ProjectDir)` already ends in `\`, so the evaluated `ObjectFileName`
  metadata contains a doubled separator (`...\VisualStudio2026\\Build\...`).
  The same form appears in `BrokenEngineSandboxServer.vcxproj:74,85,97`,
  `DataPacker.vcxproj:151,160`, and `ThirdParty.vcxproj:61,67,73`.
- `ExtendedLengthPath` in `Tools/ToolCommon/ToolCliCommon.cpp:351-363` only
  calls `path.make_preferred()` before prepending the `\\?\` prefix and never
  collapses the doubled separator. The NT object manager rejects an empty
  component in a `\\?\`-prefixed path, so the `::DeleteFileW` call at
  `Tools/WorktreeCli/BuildCommand.cpp:645` fails with `ERROR_INVALID_NAME`
  (123) and the build is failed before MSBuild launches.

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

The root cause above is proven from current source, so no transcript review is
required and this plan no longer depends on the producing worktree surviving
`/cleanup-worktrees`.

## Design
Normalize the path inside `ExtendedLengthPath`
(`Tools/ToolCommon/ToolCliCommon.cpp`) so a doubled separator cannot survive
into a `\\?\`-prefixed path: collapse repeated separators (for example via
`lexically_normal`) after `make_preferred()` and before the prefix is
prepended, leaving the existing early returns for relative and UNC paths
unchanged.

That single change fixes the whole class: every caller of `ExtendedLengthPath`
in both AgentTools becomes immune to any project's unnormalized `IntDir`
metadata, and the client, server, DataPacker, and ThirdParty projects all stop
reproducing the symptom without touching build data.

Rejected alternative: dropping the leading `\` from the `IntDir` expressions in
the four `.vcxproj` files. It is a smaller edit but only fixes the projects
edited today and leaves the tool able to build the same invalid path from any
other metadata.

## Critical files
- `Tools/ToolCommon/ToolCliCommon.cpp` — `ExtendedLengthPath`, the sole fix
  site.
- `Tools/WorktreeCli/BuildCommand.cpp` — the `::DeleteFileW` selected-object
  invalidation that surfaces the failure; read-only reference for verification.

## In scope
- Normalizing the path in `ExtendedLengthPath` before the `\\?\` prefix is
  applied.
- Rebuilding and promoting the shared AgentTools binaries the change requires.

## Out of scope
- The landed Agent active-socket teardown change and all
  `Engine/Source/Agent/AgentCommandServer.*` implementation or documentation,
  and the graphics-menu UI change from the session that proved the root cause.
- Editing `IntDir` in any `.vcxproj`, and any other MSBuild project/target
  change.
- Unrelated WorktreeCli verbs, other `ToolCliCommon` helpers, and unrelated
  skills or scripts.
- Any transcript path, transcript text, or absolute home path in the repo.

## Risk tier and invariants
Tier 3: `ToolCliCommon` is shared by WorktreeCli and AgentHarness, so the fix
requires shared AgentTools rebuild and promotion — build/bootstrap coordination
that can block other sessions. `ExtendedLengthPath` is used for coordination,
queue-store, and build files, so the change must not alter behavior for
relative paths, UNC paths, or paths already free of doubled separators.

## Acceptance criteria
- `WorktreeCli build --files <a client .cpp> -- BrokenEngineSandbox.vcxproj`
  succeeds under the invocation documented in `.agents/skills/compile/SKILL.md`
  without the explicit `/p:IntDir=` workaround, and reports the invalidated
  object.
- A second selective build of the same file still succeeds, and a full-solution
  build is unaffected.
- `WorktreeCli plan validate` exits 0.
