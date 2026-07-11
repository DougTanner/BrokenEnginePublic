# Plans

Refactor/bugfix plan queue — debt reduction that doesn't add a new engine capability. Counterpart: `Documents/Features/`; the deciding test and the scoring anchors live in [`../AGENTS.md`](../AGENTS.md). `/next-plan` executes the top-priority (lowest-score) plan and refreshes its file/line citations against current source.

## Order.md

Priority index — single table of all live plans sorted by score, lowest first (Score = Effort − Impact + Risks). It reflects only the CURRENT set of live plans; **it is not a changelog.** Do not record landed/dropped plans, "this run / this session" retrospectives, "has landed and been removed" annotations, or review-sweep narratives here — every row and reference must correspond to a plan file that exists on disk right now. It also carries:

- `### Reference / Index Documents` — second table for meta/overview docs that are never executed as plans (e.g. `Graphics/ShaderReview/00_Overview.md`); excluded from the priority walk and orphan scans.
- `## Dependencies` — explicit prerequisite and landing constraints between live plans. Only directional dependency language blocks selection; ordinary overlap is warning-only, while `never interleave`, conflicts/joint resolution, alone execution, and invariant batches remain mandatory landing constraints.
- `## File Groups` — live plans touching the same files. These entries warn about intersecting files and likely landing order; they never block selection by themselves.

Debt-score / review-sweep retrospectives do **not** belong in `Order.md` — capture that context (if wanted) in the individual plan files' `## Context`, not in the priority index.

The authoritative claim is an AgentCli v2 plan-domain lock keyed by the normalized `Order.md`-relative plan path. Use the installed `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`: generate an owner with `lock token`, then `lock claim --domain plan --key <normalized-plan-path> --owner <token> --session <label> --worktree <path>`. Use the same domain/key locator for `status`, `release --owner <token>`, and `steal --expect <old-token> --owner <new-token> --session <label> --worktree <path>`. A Notes cell prefixed `[CLAIMED <date>]` is only the lock's informational mirror in the session worktree. Claim age is warning-only. Takeover requires explicit user approval and conditional `steal` using the owner token returned by a fresh `status`; release requires the matching owner token.

Concurrent plans may overlap files. Ordinary `co-schedule`, shared-file, and refresh-citation overlap warns with the active plan/session, intersecting files, and expected landing order, then proceeds in isolated worktrees. Under root C++ Code Change Process step 12, the later lander incorporates the newer target-branch commit, reconciles the overlap, and reruns every affected review, build, and verification step before fast-forwarding the recorded target branch. Explicit prerequisites still block selection. `Never interleave`, conflicts/joint resolution, alone execution, and protocol/version/CRC/replay/`.pack`/`kiVersion` batching are mandatory landing constraints that must be preserved, reconciled, and reverified rather than downgraded to warnings.

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets its scored `Order.md` row in the same edit session that creates the plan file, with the full row populated: `Tier`, `Effort`, `Impact`, `Risks`, computed `Score`, `Notes`. Use the scoring anchors in [`../AGENTS.md`](../AGENTS.md). Do not leave scoring "for the user to fill in later" — that abdication has caused plans to land outside the priority queue. If the change is genuinely too speculative to score, the plan does not belong here yet.
- Insert the new row at its score-correct sorted position. If the plan touches files already listed in `## File Groups` or `## Dependencies`, add it to the relevant entry there too.
- When a plan is executed (or dropped): remove its row from `Order.md` and delete the plan file from disk. Also **prune every reference to it** from `## Dependencies` and `## File Groups` — delete the plan from any entry that names it, and delete entries that no longer reference two or more live plans. Do **not** leave "landed and been removed" / "dropped as invalid" annotations behind; there is no historical record here (`git log` and the git history of deleted plan files are the archive). Any surviving context worth keeping (e.g. an API a follow-up plan builds on) is stated as present-tense current state ("reuses the existing `X` API"), never as "added this session by the now-landed `Y`".
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and update both `Order.md` files.

### Required `Order.md` row format

```
| Plan | Tier | Effort | Impact | Risks | Score | Notes |
| [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <Effort − Impact + Risks> | <one-line summary of what lands> |
```

`Tier` is an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersPostRender`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan`'s citation-refresh pass relies on symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine since they get refreshed at execution.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths — and pre-stage any single open decision for `/external-grill-plan` in `## Notes`.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
