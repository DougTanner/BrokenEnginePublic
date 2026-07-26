# Transcript Finder Subagent Ambiguity

## Status: manual — awaiting re-audit of the revised design

The original auto-selection design was audited and falsified; this document now
carries the replacement design (rank-and-confirm). It stays marker-less so the
scheduler cannot claim it until the revised design is audited.

## Context

`.agents/skills/next-plan-review/scripts/Find-AgentSessionTranscript.ps1` is the
only bounded provenance mechanism `/next-plan-review` has. Run against commit
`32cba814316dc42144a72f93258afc81b162043f` it exits `2` with
`code: transcript.ambiguous`, forcing `Transcript provenance: BLOCKED`. Its
documented recovery — "rerun with an exact SessionId" — requires the reviewer to
already know the answer the finder exists to produce.

The producing parent is Codex session `019f9ed5-9c6c-7e81-859b-5b8654eea292`,
whose `session_meta.cwd` is the registered Codex worktree at that commit's
`HEAD`. Commit facts: author `2026-07-26T16:27:55Z`, commit `2026-07-26T17:16:37Z`.

### D1 — session identity is read from the wrong field

`Get-TranscriptMetadata` sets `$id` from `payload.session_id`, falling back to
`payload.id` only when absent. In Codex multi-agent rollouts a subagent's
`session_meta.session_id` is the **root thread id** while `payload.id` is the
transcript's own id, so every subagent transcript is filed under its parent.
Confirmed across 2,447 readable stored headers: every `payload.id` matches its
own filename, and top-level records have `payload.id == payload.session_id`.
Consequences: candidate counts inflate by one per concurrently live subagent, and
the `-SessionId` comparison at line 333 uses the root id, so naming a subagent's
own id can never match it.

### D2 — ambiguity is a dead end rather than a decision

The candidate count is not stable: the same commit yielded 6 candidates, then 14
after further sessions ran inside the ±360-minute window. `transcript.ambiguous`
discards every distinguishing fact the script already computed and offers only an
unreachable recovery.

**Timing cannot resolve this.** Two top-level sessions both satisfy "started
before the author time and still running at the commit time":
`019f9ed5-9c6c-7e81-859b-5b8654eea292` (start `16:30:30Z`) and
`019f9f2d-0ae1-7693-8c0c-a8a25c95bf92` (start `16:07:32Z`). An author-time filter
was measured and rejected: it leaves two candidates, and it would falsely exclude
a session that lands a commit authored by an earlier session, since author dates
survive a rebase (`Invoke-FinalizeApprovalPreparation.ps1` copies the oldest
commit's `%aI` and `%cI` unchanged into `commit-tree`).

### D3 — proven delegation data is parsed and discarded

`session_meta.payload.source` carries subagent metadata including
`thread_spawn.parent_thread_id`, `depth`, and `agent_path`. The finder opens
these records and drops the fields, forcing every reviewer to reconstruct the
delegation tree by hand. The store also contains 14 legacy
`source.subagent.other` records and 48 depth-2 rollouts whose immediate parent
differs from the root `session_id`, so any classification rule must handle more
than `thread_spawn`.

## Design

The finder's job is to **bound the search and present evidence**, not to decide
provenance. Provenance already requires judgment — `SKILL.md` step 4 demands
production proof that no timestamp can supply — so auto-selection on transcript
fields is abandoned.

### 1. Correct session identity [~20m]

In `Get-TranscriptMetadata`, read the transcript's own identity from
`payload.id`, falling back to `payload.session_id` only when `id` is absent.
Additionally return `RootSessionId` (`payload.session_id`) and, when
`payload.source.subagent.thread_spawn` is present, `ParentThreadId`, `Depth`, and
`AgentPath`.

### 2. Classify root vs. descendant by the id relationship [~20m]

A record is a **root session** when `session_id` is absent or equals `id`;
otherwise it is a descendant. This rule covers `thread_spawn`, legacy
`source.subagent.other`, and depth-2 rollouts uniformly, rather than inferring
top-level status from a missing `thread_spawn`. Only root sessions are candidates.

### 3. Replace the dead end with a ranked, evidence-bearing result [~45m]

When exactly one root candidate remains, keep today's `status: pass` /
`transcript.found` / `candidate` shape unchanged.

When more than one remains, return `status: needs-selection`, code
`transcript.needs-selection`, at the existing exit `2`, with every root candidate
listed and each carrying the facts the script already computed:

- `sessionId` (its own), `locator`, `sessionStartUtc`, `sessionEndUtc`
- `startsBeforeAuthorUtc` — boolean, reported as evidence and never as a filter
- `commitHashMentions` — how many lines mention the full 40-character hash,
  counted during the existing full-file line read
- `descendantCount` — how many descendant transcripts in the bounded set claim it

**Order deterministically by `sessionStartUtc`, then `sessionId`; do not rank by
the evidence fields.** Every candidate-scoring rule considered here was measured
against the real store and found misleading: the unrelated root
`019f9f2d-0ae1-7693-8c0c-a8a25c95bf92` mentions the commit hash, starts earlier
than the true parent, and satisfies the author-time bound, so hash presence,
earliest start, and author-time ordering would each promote the wrong session.
Mention *counts* differ sharply (16 vs 2) but that is an unvalidated heuristic;
report it and let the reviewer weigh it.

Also report the commit's author timestamp as `commit.authoredUtc` beside the
existing `committedUtc`.

### 4. Report descendants as discovery metadata [~20m]

Attach to each root candidate the descendant transcripts from the bounded file
set that claim it, ordered by start, each with `sessionId`, `locator`,
`sessionStartUtc`, `sessionEndUtc`, `agentPath`, and `depth`. Associate a
descendant with its root by `RootSessionId`, so depth-2 records attach correctly.

This is **discovery metadata, not proof.** `session_meta.source` records a
claimed relationship; `SKILL.md` step 4's required parent delegation event lives
in the parent's own `sub_agent_activity` records, which this plan does not parse.
The output field and the skill prose must both say so.

In `explicit-session-id` mode the file set contains only the filename ending in
the requested id, so no descendant can be present. Report `descendants` as
`null` in that mode — not an empty array — so "none discovered" is never
confused with "none exist".

### 5. Synchronize `.agents/skills/next-plan-review/SKILL.md` [~20m]

Update step 2 to describe root-only candidacy, the `needs-selection` outcome, and
that ranking is presentational. Update step 4 to state that the finder's
descendant list is discovery metadata requiring the reviewer to confirm the
parent delegation event, and that it is unavailable in explicit mode. Leave the
Claude-side rule in step 3 unchanged.

## Critical files

- `.agents/skills/next-plan-review/scripts/Find-AgentSessionTranscript.ps1`
- `.agents/skills/next-plan-review/SKILL.md`

## Out of scope

- Any widening of the search surface: no home-directory content search, no
  additional store roots, no following a transcript-provided path (including
  `agentPath`), and no relaxation of the `Get-PathSafety` / `Get-SafeStoreRoot`
  reparse-point checks. The `containsCommitHash` scan reads only lines the script
  already streams from files bounded discovery already opened.
- Claude-side transcript discovery (`SKILL.md` step 3).
- Parsing `sub_agent_activity` events to mechanize step 4's delegation proof.
- `Get-EligibleWorktreeRoots` and its "recorded `HEAD` contains the commit" rule.
- The `-SessionId` filename glob, `WindowMinutes` default, exit-code vocabulary,
  and the `broken-engine-agent-session-transcript/v2` schema version — new fields
  are additive.
- `Tools/WorktreeCli` terminal-manifest behaviour, and the assessment stages
  `CoreWorkflowTranscriptEnforcement.md` owns.

## Risk tier

Tier 2 — scoped behavior change in one review-support tool.

The prior audit escalated the *auto-selection* design to Tier 3 because candidate
selection began trusting new fields from explicitly untrusted transcript input.
That basis is removed here: the new fields are reported as evidence and ranking,
never as a selector, and the one field that still gates candidacy
(`session_id` vs `id`) narrows the candidate set only into a human-confirmed
decision. A forged `session_id` can at most move a transcript from the candidate
list into a descendant list that the reviewer still sees. **This reclassification
is a claim for the re-audit to test, not a settled conclusion.**

Invariant exposure: provenance presentation for `/next-plan-review` only. No
change can auto-promote a wrong session, because the multi-candidate path no
longer auto-selects at all.

Coordination note: `CoreWorkflowTranscriptEnforcement.md` also edits
`.agents/skills/next-plan-review/SKILL.md` but declares this script unchanged and out of its
scope, and its edits target the assessment stages rather than steps 2 and 4.
Neither plan is a prerequisite of the other.

## Acceptance criteria

- `-Commit 32cba814316dc42144a72f93258afc81b162043f` exits `2` with
  `status: needs-selection`, listing `019f9ed5-9c6c-7e81-859b-5b8654eea292` and
  `019f9f2d-0ae1-7693-8c0c-a8a25c95bf92` as root candidates, with
  `019f9ed5-…` ranked first.
- No candidate or descendant entry carries another session's id; every reported
  `sessionId` equals its own `payload.id`.
- The true parent's descendants include
  `019f9f22-c62d-75d2-8557-6ecadc69bef8` and
  `019f9f45-1321-7fe1-bf2f-af5910018f14` with their `agentPath` values.
- Legacy `source.subagent.other` records and depth-2 rollouts are classified as
  descendants, not root candidates.
- `-SessionId 019f9ed5-9c6c-7e81-859b-5b8654eea292` returns that single candidate
  at `status: pass` with `descendants: null`.
- A commit whose bounded window contains exactly one root candidate still returns
  `status: pass` / `transcript.found`, unchanged from today.
- `.agents/skills/next-plan-review/scripts/Test-Find-AgentSessionTranscript.ps1`
  passes, including its existing reparse-root and reparse-file unsafe-path
  assertions, proving `transcript.read-error` behaviour is unchanged.

## Verification

Run the existing fixture `Test-Find-AgentSessionTranscript.ps1` and name its
unsafe-path assertions; run the finder against `32cba814…` for the ranked
multi-candidate result; run it against a commit whose window holds one root
session to confirm the unchanged single-candidate path.
