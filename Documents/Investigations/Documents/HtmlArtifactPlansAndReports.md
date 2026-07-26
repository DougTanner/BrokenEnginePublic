# HTML Artifact Plans and Reports

Status: exploratory / investigation. It lives in `Documents/Investigations/` because it presents options rather than a decision-complete implementation, so it is never a scheduler input. It becomes a Plan only once the open questions below are answered and it moves to `Documents/Plans/<area>/` with byte-zero `broken-engine-plan/v1` metadata.

## Context

Current Anthropic guidance recommends richer reference forms than plain markdown — HTML artifacts for implementation plans and change reports, on the grounds that a rendered document with structure, tables, and diagrams communicates decisions better than prose, and that leading with the decisions a reader is most likely to change accelerates review.

This repository stores every plan as markdown under two trees, and every review artifact as inline agent output.

## Open questions

1. Which artifacts would benefit? The plausible candidates are the `/verify-changes` acceptance table, `/session-audit` findings, and Tier-3 implementation plans — all currently inline text that the user reads once. A plan is read by agents far more often than by the user, which argues against HTML for plans specifically.
2. Who consumes it? Agents parse markdown natively and cheaply. An HTML artifact is strictly better for the user and strictly worse for the next agent that must read it. The split suggests HTML for reports (human-terminal) and markdown for plans (agent-consumed), not a wholesale move.
3. Is the reading cost acceptable? An artifact lives outside the repository, so an agent must fetch it rather than read a tracked file.

## Known conflicts

- Hard blocker for executable plans. `Documents/Plans/**/*.md` is parsed by WorktreeCli, which requires the byte-zero `broken-engine-plan/v1` metadata marker as the first bytes of the file (`Documents/Plans/AGENTS.md`). `Tools/WorktreeCli/PlanScheduler.cpp` is the sole parser. An HTML plan would need either a parallel scheduler path or a markdown stub carrying the marker and pointing at the artifact — the second is feasible but doubles the number of files per plan.
- Root `AGENTS.md` `## Directives` forbids adding a second format without explicit consent: "keep one current format, path, or behavior and remove obsolete compatibility code." Running markdown and HTML plans side by side is exactly the dual-format state that rule exists to prevent.
- Artifacts are hosted, not tracked. A plan that disappears from Git history is not auditable against a landed commit.

## Possible approach

Scope this to reports only — `/verify-changes` tables and audit output — and leave `Documents/Plans/` untouched. That captures the human-facing benefit without touching the scheduler contract or the one-format rule.

## Out of scope

Any change to the byte-zero marker, `PlanScheduler.cpp`, or the `Documents/Plans/` file format.
