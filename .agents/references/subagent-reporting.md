# Delegated Reporting

Delegation uses a fresh, isolated context by default: Codex uses
`fork_turns: "none"`; Claude receives a fresh prompt. A positive Codex turn
fork is allowed only when exact authoritative conversation text cannot be
safely summarized, and the prompt states why.

A delegated review or audit runs solely inside one fresh delegated `reviewer`
that returns findings only, and the tool restrictions a skill body states are
prose boundaries rather than host enforcement.

## Task brief

Every delegation supplies one self-contained brief containing:

- assigned role and exact objective;
- owned scope and explicit exclusions;
- fixed user decisions and changes the user approved after the plan;
- governing repository paths;
- known affected artifacts, symbols, or regions;
- session baseline or other meaningful identity;
- acceptance checks and expected observations;
- task-specific prohibitions; and
- required return format.

The session baseline is the commit a session's work is diffed against, fixed
when the session starts. The baseline and every other machine-derivable
identity value in a brief are copied from `Get-AgentWorktreeSessionContext`
output in `.agents/scripts/AgentWorktreeSession.psm1`, never retyped from
memory or scrollback.

Use `none` when a field has no value. Add only fields required by the invoked
skill. Cite repository paths; do not paste root instructions, skill bodies, the
complete Plan, manager transcript, unrelated handoffs, or raw XML/logs. When a
completed Plan file has already been deleted from the tree, the manager supplies
its approved text or the Git-history path to read it from.

Workers verify supplied hints against the tree, stay within ownership, and
expand only for a concrete dependency, decision, or failure. They never route
work to another worker.

## Handoffs

Return only decision-relevant evidence:

```text
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <paths and regions, or none>
Decisive checks: <command/read and result>
Build required: <exact targets, or none>
Residuals: <actionable blocker or none>
```

Skills may extend this form, but retain `Build required` and keep `Residuals`
last. Name an existing file or log plus a narrow selector when evidence is
large. Create a file under `Temp/` only when the owning workflow requires it.
Independent review and verification use a context that did not produce the
work. A focused correction/retest also uses an independent context.

A worker ends its turn with the handoff as its final answer and never enters an
open-ended wait after delivering it; continuation goes through the host's resume
path. Any wait a worker issues mid-task carries a bounded timeout well under the
host tool cap.

## Whether a worker is still running, and interruption

Judge whether a worker is still running only from host status and explicit
progress or partial handoffs.
A running host status, elapsed time, or wait boundary does not require a
progress ping or repeated manager status command. Forward progress is a newly
reported distinct action, narrowed search, new evidence, or synthesis; a loop
is explicit repetition without narrowing or new evidence. A worker messages its
manager mid-task only to ask a blocking question or hand off a partial result,
never to narrate status; manager-to-user narration is unaffected.

Documented no-progress means no recorded worker tool call or message within a
no-activity window the manager states in advance, measured from the worker's
last recorded action; elapsed turn time alone does not establish it. When
terminal failure or documented no-progress/loop evidence exists:

1. inspect host status and available partial evidence;
2. request the handoff immediately;
3. allow a fixed response window; and
4. interrupt or replace only if the failure or no-progress evidence persists.

Prefer resuming the same worker. Otherwise supply a recovery capsule containing
objective/scope, meaningful identity, completed evidence, unresolved issue, next
action, and the skill to resume with (for example `/implement-plan`,
`/resolve-findings`, or `/update-affected-code`) so completed exploration is not
repeated.
