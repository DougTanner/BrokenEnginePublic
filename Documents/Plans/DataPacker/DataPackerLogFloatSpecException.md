# Decision: DataPacker LOG Float-Format-Spec Exception to the Root Ban

## Context

**Decision plan (present options).** The root CLAUDE.md states a global ban: "never use float format specs
(`{:.Nf}`, `{:e}`, etc.) in `LOG(...)` — they heap-allocate and trip the allocation tracker. Wrap floats with
`common::Wb(value, precision)`…". The rule's *mechanism* is the **main-loop allocation tracker** (heap
allocations in the engine main loop trigger `DEBUG_BREAK()`).

But **`DataPacker/Source/Main.cpp`'s RDO-sweep LOGs use `{:4.2f}`-style float specs** directly. DataPacker is an
**offline tool** with **no main-loop allocation tracker** (`ScopedSuppressAllocationTracking` is inert in tools
builds per the Memory note: "suppression inert in tools builds"), so the heap-allocation-on-format concern that
motivates the ban does not apply there. The float-spec LOGs are almost certainly **fine** in DataPacker — but
the rule is stated **globally**, so a reader (or a future code-review pass) sees a violation.

The question: is DataPacker an **intended exception** to the float-spec ban, and if so, how should the rule say
so?

## Design

This is a **doc-rule wording decision** (the code is almost certainly fine as-is) — confirm in the grill, then
adjust the rule wording. No code change expected.

- **Option A — scope the ban to the engine/allocation-tracked builds.** Reword the root rule to "in
  allocation-tracked code (the engine client/server main loop), never use float format specs in `LOG(...)`…;
  the offline DataPacker has no allocation tracker, so float specs are permitted there." Cross-link the Memory
  note (suppression inert in tools builds) as the rationale. **Recommended** — it makes the rule's real boundary
  explicit and matches the actual mechanism; the DataPacker LOGs stay as-is.
- **Option B — keep the ban global and convert the DataPacker LOGs** to `common::Wb(...)` for uniformity. Costs
  a mechanical edit of the RDO-sweep LOGs for no functional benefit (no tracker to trip), and `common::Wb` may
  not even be the right tool in a tools build. Not recommended unless the user wants one uniform LOG style
  everywhere.
- **Option C — leave as-is** and accept that readers must know DataPacker is implicitly exempt. Rejected: the
  refresh already tripped on it once; an explicit scope (A) prevents the next false flag.

**Recommendation: A.** Verify first that the DataPacker float-spec LOGs are genuinely off any
allocation-tracked path (they are offline-tool startup/sweep logs — confirm no shared engine main-loop path
reaches them). Then scope the rule.

## Out of scope

- **The engine LOG float-spec discipline** — unchanged and still enforced; this only carves out the offline
  tool.
- Converting any **engine** LOG to/from `common::Wb` — not touched.
- The `repo-code-review` skill's §2b LOG-formatting rules — if Option A lands, that skill's wording should
  mirror the scope (note for the doc-sync step), but editing the skill is not this plan's job.
- The DataPacker RDO-sweep *values/knobs* and the stale "current production knobs" comment — that comment is the
  separate `StaleCodeCommentsSweep.md` item; this plan is only about the float-spec LOG rule scope.
- Any actual allocation-tracking change.

## Critical files

- Root `CLAUDE.md` — the "LOG formatting" bullet banning float specs (the rule to scope). **NOTE:** root
  CLAUDE.md is out of this batch's direct edit scope; the wording lands under the normal doc process when the
  decision is made.
- `DataPacker/Source/Main.cpp` — the RDO-sweep LOGs using `{:4.2f}`-style specs (read-only confirmation that
  they are offline-tool-only).
- `Engine/Source/Memory/CLAUDE.md` — the "suppression inert in tools builds" note (the rationale to cross-link).
- `Documents/` repo-code-review skill §2b — mirror the scope here too (follow-up doc-sync, not edited by this
  plan).

## Notes

- **Decision plan (present options)** — almost certainly a wording-only resolution (Option A); no code change
  expected, Risks = 0.
- The decision hinges on confirming DataPacker's RDO-sweep LOGs are truly outside any allocation-tracked path
  (they are tools-build, offline) — a quick verification, then a one-line rule scoping.
