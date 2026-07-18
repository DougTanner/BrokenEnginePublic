# Plans

Refactor/bugfix plan queue; sibling feature rules live in `../AGENTS.md`. The [canonical execution-gate contract](../../.agents/skills/next-plan/references/execution-gates.md) approves the full plan once; only the exact primary-mutation summary can authorize primary history to change.

## Plan queue (machine-local)

The plan queue is **machine-local state**, not a tracked repository file. Its bytes live under `%LOCALAPPDATA%\BrokenEngineLocks\plan-queue-state\<sha256(repo-common-dir)>\Plans-Order.md` (features use `Features-Order.md`), beside WorktreeCli's queue locks, row claims, and landing lock. Plan files stay in the repo; only the queue rows moved out. `plan order init` seeds the store once per machine (from the tracked `Order.md` files when they still exist, else an empty header-only table). The `--plans-order` / `--features-order` strings keep their `Documents/Plans/Order.md` / `Documents/Features/Order.md` defaults as *logical queue identities* only — no such file is read or written in the tree.

Plan-file edits are ordinary tracked repository work; queue-row mutations publish at landing (see the authority section below). Ad hoc planning and plan-file cleanup may use the checkout the user supplied. `plan order` selection and completion normally use a wrapper-created session worktree and live WorktreeCli claim. An explicitly user-authorized `primary-commit` may adopt clean primary under the root finalization contract.

The queue is a live queue, not a changelog. Its executable table contains every current plan, roughly ordered by score (Score = Effort − Impact + Risks), lowest first. Row order is only a loose tiebreaker; approximate placement is sufficient and WorktreeCli does not diagnose score ordering. Do not record landed/dropped plans, session retrospectives, removal annotations, or review-sweep narratives. Put durable context in the individual plan file instead.

WorktreeCli is the only component that parses, validates, or mutates the queue store's executable rows. The machine-owned store file has this exact shape:

```
| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |
| [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <computed> | <normalized dependencies or single dash> | <one-line summary of what lands> |
```

`Depends On` is `-` or a semicolon-separated list of normalized repository-relative `Documents/Plans/<area>/<file>.md|.txt` and `Documents/Features/<area>/<file>.md|.txt` identities. A dependency row blocks its dependents while it exists, including while another session has claimed it. Do not express directional prerequisites in prose. Notes may contain ordinary prose, including `|`; WorktreeCli owns the fixed-column parsing and rendering.

Mandatory nondirectional constraints (`never interleave`, joint resolution, alone execution, or protocol/version/CRC/replay/`.pack`/`kiVersion` batching) belong in a standard `## Coordination` section in every affected live plan. Those sections are plan-body prose, so writing or changing one is an ordinary direct file edit (see the direct-edit rule below); the authoring rule is that a change set updates every existing counterpart in the same commit. A concurrent edit to a counterpart is resolved as a merge conflict at landing, not a zero-mutation queue failure.

`### Reference / Index Documents` remains the non-executable table for meta/overview documents such as `Graphics/ShaderReview/00_Overview.md`. WorktreeCli validates that those references resolve and do not also appear as executable plans, but they are never scheduled or scored.

### Direct plan-file edits

Plan-body prose is ordinary tracked content. Editing a plan's text — including cross-references between plans and `## Coordination` sections — is a direct file edit whether or not the plan is claimed and needs no `plan order` request. Concurrent edits to the same plan resolve through the normal rebase + fix-merge-conflicts landing steps, exactly like any other source file. `plan order update` is required only for **row columns** (tier/effort/impact/risks/notes/dependsOn) and for plan renames/moves that change the row's plan identity. Never edit an executable queue row directly, and never place claim state or owner/session metadata in a plan file.

### Primary and session authority

The machine-local queue store is the live queue used by validation and `claim-next`. Row mutations from a wrapper session **publish at landing**: they become live only after the landing transaction advances primary, so a row never references a plan file absent from primary. An explicitly user-authorized primary commit may use registered primary as its worktree through the root `primary-commit` path, mutating the queue immediately. `--repo` is the Git common directory, `--primary-worktree` is the authoritative primary checkout whose plan bytes `claim-next` still checks, and `--worktree` is the registered checkout whose plan files, request, and staged content the command may inspect or mutate; the queue bytes themselves come from the machine-local store.

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` from the current checkout. Linked worktrees consume the provisioned immutable primary Output; routine workflows never build WorktreeCli or write through that link. WorktreeCli owns the existing global queue locks and row claims. Takeover still requires explicit user approval.

Plan-producing workflows finish semantic validation and write the new plan files in the session, then stage a schema-versioned `plan order add` request beneath that worktree's `Temp/`; the `add` submission runs post-landing so rows always reference landed files. WorktreeCli computes Score, validates both queues and the complete dependency graph, and adds every requested row or none. No landing means no rows and no orphans. Existing-plan **row** changes stage one atomic `plan order update` request; plan-body prose changes are direct file edits, not staged requests.

`plan order claim-next` reads the machine-local queue, requires matching selected-plan bytes against primary (`plan-byte-mismatch` is the sole plan-content guard — the claim no longer requires a clean session or primary tree), and creates the row claim without edits. `plan order complete` requires that claim and removes the row and its dependency edges; it tolerates an already-deleted plan file (the session deletes the file with `git rm`, so row removal at landing acts on a file that is already gone). A session landing keeps the claim until the post-advance `complete` and owner-unclaim; a primary commit keeps it until finalization validates committed primary with `plan order validate` and unclaims it. After a crash between primary advance and row removal, rerun `plan order complete` under the retained owner claim, passing the canonical `Documents/...` plan path (`complete` rejects the order-relative row key the `plan row` commands use).

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets a fully populated WorktreeCli add entry in the same edit session that creates the plan file: `queue`, `plan`, `tier`, `effort`, `impact`, `risks`, `notes`, and optional `dependsOn`. Use the scoring anchors in `../AGENTS.md`; WorktreeCli computes Score. Do not leave scoring for later. If the change is too speculative to score, it does not belong in the queue yet.
- Group new plans into prerequisite-first request sequences. WorktreeCli adds an immediate-predecessor edge within each sequence; put independent plans in separate sequences. Dependencies on existing live plans are explicit `dependsOn` identities.
- Never edit an executable queue row directly (it is machine-local, not a tracked file anyway). Use WorktreeCli `add`, `update`, or owned `complete`; the command validates and mutates both queue graphs atomically as required. Plan-body prose is the exception — it is direct file editing (see the direct-edit rule above).
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and submit the corresponding WorktreeCli queue mutation.

`Tier` remains an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **State the risk trigger.** A live plan names any Tier 3, queue, landing, or invariant exposure. Tier 3 uses a short execution card and conditional audit/grilling only for material ambiguity; the final-evidence acceptance table is reserved for queue mutation, reconciliation, or landing. Internal numbered design steps remain local to the plan.

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Ground the plan.** Every design step and acceptance check follows from the goal or an existing contract. Use repository evidence for facts; leave unresolved material choices to the user.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersPostRender`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan` execution locates code by symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine because execution carries any stale-citation corrections in the execution card and never edits the claimed plan file.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths. Pre-stage unresolved architectural decisions only when Tier 3 needs user input before approval.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
