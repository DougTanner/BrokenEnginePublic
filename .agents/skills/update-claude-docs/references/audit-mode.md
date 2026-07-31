# Audit Mode

## Discovery

Glob `**/AGENTS.md` and sibling `CLAUDE.md` files in the authorized audit scope. Exclude `ThirdParty/`, `Documents/Plans/`, `Documents/Features/`, and `Engine/Source/Graphics/Managers/*.AGENTS.md`. Do not include `CLAUDE.local.md` or another local override unless explicitly requested.

Grade only AGENTS.md. Check stubs separately and bidirectionally: every directory AGENTS.md has a sibling CLAUDE.md containing exactly `@AGENTS.md`, every such CLAUDE.md has a same-directory AGENTS.md, and linked `*.AGENTS.md` files have no stub requirement.

## Rubric

Score each criterion from zero through its weight, for a total of 100.

| Criterion | Weight | Check |
|---|---:|---|
| Commands and workflows | 20 | Are applicable workflows current and executable? A leaf with no local commands earns full credit; builds route to `/compile`. |
| Architecture clarity | 20 | Can a reader understand subsystem purpose, ownership, and relationships in one read? |
| Non-obvious patterns | 15 | Are relevant allocation, determinism, guard, SOA, coordinate, ordering, or hardware constraints present? |
| Conciseness | 15 | Are inventories and duplicated prose absent? Is a leaf at most 2,000 and a cross-cutting hub at most 4,000 `bt-token-v1`; is the effective chain below the 15,000 target, with a warning above 20,000? |
| Currency | 15 | Do paths, APIs, imports, and present-tense behavior match the current tree without changelog narration? |
| Actionability | 15 | Does each instruction enable a concrete decision or check? |

Grades: A = 90–100, B = 70–89, C = 50–69, D = 30–49, F = 0–29. Treat grade C or below as needing update.

Also check that Collection/SOA conventions appear where relevant; shader docs describe techniques instead of interface inventories; architecture diagrams are linked instead of copied; sibling and parallel hierarchies do not duplicate rules; paths and authoritative source exemplars resolve; `@` prefixes denote live imports only; tone is direct; and stub pairing is valid in both directions.

## Quality Report

Emit this report before any authorized audit-and-fix edits:

```markdown
## AGENTS.md Quality Report

### Summary
- Files found: X
- Average score: X/100
- Files needing update: X (grade C or below)
- Stub findings: X

### File-by-File Assessment

#### <path/AGENTS.md>
**Score: XX/100 (Grade: X)**

| Criterion | Score | Notes |
|---|---:|---|
| Commands and workflows | X/20 | ... |
| Architecture clarity | X/20 | ... |
| Non-obvious patterns | X/15 | ... |
| Conciseness | X/15 | ... |
| Currency | X/15 | ... |
| Actionability | X/15 | ... |

**Issues:** <list or none>
**Recommended improvements:** <list or none>
```

## Assessment Examples

Flag member-by-member lists, file inventories, shader binding enumerations, method call-chain narration, stale paths, duplicated parent rules, edit-history prose, all-caps emphasis, and a CLAUDE.md containing anything beyond its import stub.

Prefer concise architecture statements such as “Uses SOA layout for cache-efficient iteration over many entities” or “Orchestrates the server loop; see the architecture document for phase detail.” An authoritative source exemplar is acceptable only when its label names the concern and applicability variant, its stable file and symbol resolve, and the invariant remains in documentation.

In audit-and-fix mode, preserve useful structure, change only authorized scope, and apply the main skill's content, removal, token, authoritative-exemplar, and stub rules. Do not perform unrelated cleanup merely because the audit found it.
