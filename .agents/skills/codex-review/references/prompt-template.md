# Codex Review Prompt Template

Fixed wording for `../scripts/New-CodexReviewPrompt.ps1`. Each fragment below is
copied into the generated prompt byte for byte between its markers; the only
substitution is `{{ASSIGNED_SKILL}}`, replaced by the assigned skill name. The
manager-authored `-ScopeFile` text and the collected evidence are the only other
prompt content, and neither is authored here.

Change a fragment only through the Change Workflow: a dropped prohibition line
silently weakens every review this skill dispatches, and the fixtures compare the
generated prompt against these bytes.

## Fragment: role instruction

<!-- fragment: role-instruction -->
Read and execute the Broken Engine `{{ASSIGNED_SKILL}}` skill as the delegated reviewer for the change described below. Its instructions are in `.agents/skills/{{ASSIGNED_SKILL}}/SKILL.md` in this worktree; read that file first and follow it. If no such skill file exists, the role is defined directly by the scope section (b), which then carries the full review contract to execute. Review only the scope in section (b), from the evidence in section (c), and answer in the form section (d) requires.
<!-- end-fragment: role-instruction -->

## Fragment: guardrail block

<!-- fragment: guardrails -->
A finding is actionable only if it names a concrete reachable failure;
NEVER propose speculative refactors, abstractions, defensive validation, or scope beyond the changed bytes and the assigned skill's scope;
NEVER edit files;
NEVER load a screenshot, capture, image, or other binary payload into the review context unless the assigned skill's scope is the runtime criterion that payload settles — rely on the harness role's reported verdict and cited path.
<!-- end-fragment: guardrails -->

## Fragment: output contract

<!-- fragment: output-contract -->
Return the assigned skill's normal concise handoff, then append one final line with a verdict token — `PASS`, `CHANGES-REQUIRED: <n>`, or `BLOCKED: <reason>`. The token supplements the skill-native status vocabulary (such as `NEEDS_ACTION`); it never replaces the skill's format.
<!-- end-fragment: output-contract -->
