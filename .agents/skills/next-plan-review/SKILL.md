---
name: next-plan-review
description: Review the latest landed `/next-plan` execution from its Git commit and producing Claude Code or Codex CLI transcript. Use when the user asks to assess the C++ Change Workflow, plan-execution quality, review or testing sufficiency, token efficiency, worktree/rebase friction, or elapsed time, and wants prioritized process-improvement recommendations.
disable-model-invocation: true
user-invocable: true
argument-hint: "[commit-ish]"
allowed-tools: [Read, Grep, Glob, Agent, PowerShell]
shell: powershell
---

# Next Plan Review

Audit one completed `/next-plan` execution. Default to `HEAD`; when an argument
is supplied, resolve that commit. Produce an evidence-based, priority-sorted
improvement backlog for the C++ Change Workflow and `/next-plan` workflow.
This is a read-only postmortem, not a code review, a plan retry, or an occasion
to modify the commit under review.

## Establish provenance first

1. Resolve the commit, its parent, author/committer timestamps, branch refs,
   complete diff, and changed-file list with Git. Use the workflow files as
   they existed at the reviewed commit, including `AGENTS.md` and
   `.agents/skills/next-plan/SKILL.md`; otherwise a later process edit can be
   mistaken for a requirement that did not exist.
   Read the commit-keyed global landing artifact through
   [`Find-AgentLandingArtifact.ps1`](../../scripts/Find-AgentLandingArtifact.ps1)
   when it exists. Treat its report and transcript locators as leads, not proof:
   independently validate the report hash/ranges and transcript evidence below.
2. For a Codex CLI session, run
   [Find-AgentSessionTranscript.ps1](scripts/Find-AgentSessionTranscript.ps1)
   from the repository root. Its deterministic search is restricted to the
   known Codex session and archived-session stores. For a Claude Code session,
   locate the appropriate transcript using the client session context and
   facilities available to you; do not prescribe a local transcript path or
   mechanically sweep the user's local Claude data.
3. Confirm the producing transcript rather than accepting a filename or a hash
   match alone. Prefer a transcript that starts before the commit, covers its
   timestamp, identifies the landing worktree or branch, and records the
   commit or finalization command with the resulting hash. A session started
   after the commit can contain its hash as baseline metadata and is not proof
   that it produced the change.
4. Identify the parent transcript and every material subagent transcript:
   implementation, review, verification, debugging, queue, or landing work
   counts as material; idle or mechanically delegated children do not. Do not
   load unrelated user-local sessions or expose credentials, tokens, or
   unrelated user content.

If no transcript can be proven, report `Transcript provenance: BLOCKED`, list
the strongest candidates and missing proof, and limit conclusions to the Git
history and repository artifacts. Do not invent session, timing, review, or
worktree evidence.

## Delegate transcript analysis

Transcripts can be too large for the main session to analyze efficiently.
After provenance is established, delegate the parent transcript and material
subagent transcripts to exactly one fresh, high-capability review agent. Prefer
an Opus/Terra-class reviewer when the host exposes that choice; otherwise use
a fresh reviewer without claiming a profile the host cannot attest. Give it the
commit facts, parent transcript path, and the material child transcript paths
or identifiers. The reviewer reads targeted event ranges and returns evidence,
not a final recommendation list.

Require this compact handoff:

```markdown
## Transcript analysis

- Provenance: PROVEN | AMBIGUOUS | BLOCKED — <basis>
- Sessions read: <parent and material child identifiers>
- Timeline: <source session, timestamp or line/event location, and decision, implementation, review, test, approval, and landing event>
- Pauses: <user/external idle intervals and duration, or none identified>
- Quality evidence: <plan delivery, review, test, and minimality evidence>
- Process evidence: <tooling, delegation, worktree, rebase, and retry evidence>
- Gaps: <missing transcript or unverifiable fact, or none>
```

Keep the handoff factual and compact: quote only the smallest necessary
transcript fragments, redact secrets, and identify the source session plus a
timestamp or event/line location for each claim. The main session inspects the
cited transcript ranges needed to confirm the diagnosis, verifies decisive
claims against Git and repository artifacts, then owns the final assessment
and recommendations. Do not make the main session reread the entire
transcript. If delegation is unavailable, use targeted transcript searches
yourself and state that the fresh transcript-analysis pass was unavailable.

## Reconstruct the execution

Build a compact chronological evidence table. Include plan selection, tier
classification, implementation, required propagation, review and fix cycles,
builds, live-harness runs, queue completion, approval waits, and landing or
rebase. Link each event to transcript timestamps, Git state, WorktreeCli output,
or a final verification report.

Locate the selected plan from the claim/completion transcript evidence and its
queue row. Compare its acceptance criteria and scope boundaries with the final
commit diff and any final-tree report. Separate required propagation from
scope expansion; do not penalize a necessary affected-site update merely
because it was not the first edited file.

For a post-migration landing, use the global artifact's verification locator,
hash, ranges, queue receipt, and transcript candidate as the first deterministic
route to finalization evidence. A missing artifact is evidence that the landing
predates the store or did not complete its artifact contract; continue with the
direct transcript and Git checks rather than guessing.

For elapsed time, state all three values when evidence permits:

- Wall-clock span: from the producing session's start to landing completion.
- Non-working pauses: explicit user-feedback or approval waits and clearly
  external idle waits, listed separately.
- Active elapsed estimate: wall-clock span minus those pauses.

Treat the estimate as approximate when timestamps are incomplete. Builds,
harness execution, debugging, and review are working time, not pauses. Do not
count an approval as waste when it is an explicit workflow gate; flag only
avoidable extra approval loops or unexplained waiting.

## Evaluate in priority order

### 1. Result quality

- Map every requested plan result and acceptance criterion to final-diff and
  verification evidence. Identify missing, inaccurate, or extra behavior.
- Judge review coverage by the actual risk tier and changed surfaces. Check
  whether the required fresh domain review occurred, whether Tier 3's bounded
  falsification and final evidence path applied when triggered, and whether
  review findings were correctly resolved. Never claim code is bug-free;
  report confidence and remaining untested failure modes instead.
- Judge testing against observable acceptance criteria. For client/server or
  runtime-visible behavior, look for a focused live harness scenario with
  decisive observed results. Do not call a harness omission a defect when the
  change has no runtime-observable acceptance criterion or the workflow made a
  narrower check sufficient.
- Check minimality against the plan's ceiling, required affected-site
  propagation, and final diff. Call out independently useful changes only when
  they materially expanded scope or increased verification burden.

### 2. Token efficiency

- Prefer evidence that deterministic scripts, WorktreeCli, build drivers, and
  harness commands handled mechanical work. Flag manual reconstruction only
  where a provided deterministic tool would have supplied the answer.
- Check that delegation was driven by an independent investigation, required
  fresh review, or worthwhile parallel work. Do not treat the absence of
  subagents as a weakness for a narrow change.
- Check whether the primary transcript remained a manager/delegator context:
  direct it toward decisions, evidence, and synthesis rather than long raw
  logs or repeated file exploration already delegated.
- Count review-change and test-change loops. Classify each as necessary when a
  concrete finding or failing acceptance check caused it; flag only duplicate
  passes, rediscovered issues, or loops with no new evidence.

### 3. Worktree isolation and landing

- Verify wrapper-created worktree, live session claim, immutable WorktreeCli
  provisioning, and readiness before work began. Check the transcript for
  bootstrap, provisioning, or tool-capability errors and retries.
- Inspect each meta-tool interaction for avoidable failures, confusing output,
  manual fallbacks, or repeated retries. Distinguish a legitimate external
  conflict from a poor workflow interface.
- Verify the rebase/reconciliation and final branch advance from Git parents,
  transcript evidence, and finalization receipts. Confirm whether conflicts
  were cleanly resolved and reverified when reconciliation changed content.
- Assess whether the written workflow led naturally to the right next action.
  Identify the exact instruction, ambiguity, or missing deterministic command
  behind any confusion rather than attributing it vaguely to the agent.

### 4. Speed

- Compare active elapsed time with the actual change complexity, changed
  surfaces, and required checks. Explain the dominant productive costs.
- Check that user feedback was requested only for material choices, plan
  approval where required, and landing approval. Keep mandatory approvals
  separate from avoidable pauses.

## Recommendations and report

Prioritize an improvement only when it has concrete evidence and a plausible
workflow change. Prefer a deterministic script, clearer precondition, or
removed duplicate stage over advice to "be more careful." Do not recommend
loosening a control that was required by the reviewed tier without explaining
the safety tradeoff.

Return this exact structure:

```markdown
# Next-plan review: <commit subject> (<short hash>)

## Executive verdict

- Transcript provenance: PROVEN | AMBIGUOUS | BLOCKED
- Result confidence: high | moderate | low — <one-sentence basis>
- Process assessment: <one-sentence outcome>

## Evidence timeline

| Time | Event | Evidence | Assessment |
|---|---|---|---|
| ... | ... | ... | ... |

## Findings by concern

### Result quality

<criterion-to-evidence assessment, including confidence gaps>

### Token efficiency

<evidence-based assessment>

### Worktree isolation and landing

<evidence-based assessment>

### Speed

<wall-clock, excluded pauses, active estimate, and assessment>

## Proposed improvements (highest priority first)

1. **P0 — <concise action>**
   - Evidence: <transcript/Git/report reference>
   - Change: <specific workflow, script, or instruction change>
   - Expected benefit: <quality, efficiency, reliability, or speed>
   - Tradeoff: <cost or `none material`>

## Strengths to preserve

- <proven effective control or `none identified`>

## Residual uncertainty

- <missing evidence or `none`>
```

Omit an empty recommendation rather than manufacturing one. Keep findings
separate from recommendations so a strong outcome can legitimately result in
few or no changes.
