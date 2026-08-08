<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T17:00:08.541Z","dependsOn":[]} -->
# Fix: WorktreeCli build result — normalized target names a nonexistent solution

## Context
During landing `/verify-changes`, the delegated `/compile` builder ran the
documented full-server command from `.agents/skills/compile/SKILL.md`:

`WorktreeCli build <worktree>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln /p:Configuration=Debug /p:Platform=x64 <required data properties and analysis switches>`

The authoritative `broken-engine-build-result/v1` object reported the existing
`BrokenEngineSandboxServer.sln` path in `target.requested`, but
`target.normalized` named the nonexistent
`Projects\BrokenEnginePublic\Platforms\VisualStudio2026\BrokenEnginePublicServer.sln`.
The remaining status, exit, and retained-log evidence showed that MSBuild built
the intended server project. This contradictory typed artifact could not settle
the landing criterion and forced a replacement server build.

The claimed active intent was
`Documents/Plans/Network/ServerSessionReadFleetDataRemoval.md`; its `## In scope`
named only the `ServerSession::ReadFleetData` declaration and definition in
`ServerSession.h` and `ServerSession.cpp`. The compile skill and WorktreeCli
build-result production were therefore outside that boundary.

Session provenance (machine-local; not reproducible after cleanup):
- Client: codex
- Session: 3f6fce97-198e-4e6b-94d1-dc020a1fe045
- Session branch: codex/3f6fce97-198e-4e6b-94d1-dc020a1fe045
- Worktree: .codex\worktrees\BrokenEnginePublic\3f6fce97-198e-4e6b-94d1-dc020a1fe045
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
- `.agents/skills/compile/SKILL.md` — documented full-server invocation and
  authoritative structured-result contract.
- `Tools/WorktreeCli/BuildCommand.cpp` — `ComparablePath` and build target
  `requested`/`normalized` result production.

## In scope
- Root-cause investigation via /next-plan-review with the recorded provenance.
- The smallest resulting fix, confined to the full-server invocation and
  structured-result contract in `.agents/skills/compile/SKILL.md`, and the
  target identity normalization/result production in
  `Tools/WorktreeCli/BuildCommand.cpp`.

## Out of scope
- The landed `ServerSession::ReadFleetData` removal and all files named by that
  completed Plan.
- Selective-object invalidation and `IntDir` handling, which are already owned
  by `Documents/Plans/Agents/WorktreeCliSelectiveBuildIntDir.md`.
- Other WorktreeCli verbs, unrelated compile behavior, other skills/scripts,
  and any transcript path or transcript text in the repository.

## Risk tier and invariants
Expected Tier 3: a source fix under `Tools/WorktreeCli` requires shared
AgentTools rebuild and promotion, which is build/bootstrap coordination that can
block other sessions. If root-causing proves this is a documentation-only fix,
reclassify from the actual changed surface. Never embed transcript paths or
home paths. Preserve the contract that `target.requested` identifies the exact
argument and `target.normalized` is the normalized identity of that same target.

## Acceptance criteria
- A documented full Debug server build reports the existing
  `BrokenEngineSandboxServer.sln` consistently in `target.requested` and
  `target.normalized`, while building that same target.
- The `broken-engine-build-result/v1` artifact alone unambiguously settles which
  target MSBuild built.
- `/validate-skill` passes for any changed SKILL.md; `plan validate` exits 0.
