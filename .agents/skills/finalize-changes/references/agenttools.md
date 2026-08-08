# AgentTools Landing

Load only when the landing-commit/landed diff changes a non-Markdown path under
`Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. This is the
exhaustive authoritative shared-artifact trigger; ordinary Git changes, Markdown in
those trees, landing-commit build outputs, data reads, locks, and claims do not qualify.

These trees are shared tool infrastructure: every linked worktree consumes the
same authoritative binaries. When a change here could break another live worktree —
a changed CLI contract, a changed lock or claim format, or a behavior other
sessions depend on mid-run — pause and warn the user before landing. When it
cannot affect another live worktree, continue without a pause.

Before the landing summary, release reconcile leases and run
`../scripts/Wait-AgentToolsQuiescence.ps1` with primary root, durable session
owner, and `-WaitSeconds 55`. `shared-quiescence` is a typed retry with no user
authority: wait only for its retry interval, then reconcile because primary may
have advanced. Invalid exclusion-ledger state requires authority and is never
bypassed.

The summary states that landing replaces the authoritative AgentTools binaries
consumed by every linked worktree and names changed CLI contracts. After
landing, rebuild both tools from the landed commit with the Release candidate
commands in [`/compile` Full-build commands](../../compile/SKILL.md#full-build-commands) and promote only with
`../scripts/Invoke-AgentToolsPromotion.ps1`, passing the freshly built pair via
`-WorktreeCliCandidate` and `-AgentHarnessCandidate`; it backs up the previous
pair and rolls back on failure. Promotion failure never rewinds the landed Git change:
report whether the previous pair was restored, a verified pair remains,
a retryable idle wait applies, or rollback failed with its named backup.

Wrapper bootstrap is the only in-place shared-Output build path. It runs at
wrapper start under its global mutex, incrementally rebuilds WorktreeCli,
AgentHarness, and ThirdParty, and best-effort seeds DataPacker. Routine builds
consume immutable primary outputs and never promote or copy tools.

Run AgentTools promotion fixtures only when this contract or its projection
changes.
