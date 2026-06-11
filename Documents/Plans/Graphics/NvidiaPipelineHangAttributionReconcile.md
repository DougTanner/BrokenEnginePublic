# Reconcile NVIDIA Pipeline-Hang Attribution (mat4-arrays vs inverse())

## Context

Two shader-doc locations attribute the same "NVIDIA pipeline-creation hang" to **different triggers**:

- `Engine/Data/Shaders/Model/ModelCommon.h:46` — "Joint matrices stored separately to avoid NVIDIA driver hang
  with **large mat4 arrays**." (i.e. the trigger is a large `mat4[]`.)
- `Engine/Data/Shaders/CLAUDE.md` ("Known Issues") — "**never call `inverse()` on mat3/mat4** in shaders —
  NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU." (i.e.
  the trigger is `inverse()`.)
- `Engine/Data/Shaders/Model/CLAUDE.md` ("Compact joint matrices") conflates both: "Split out so the joint array
  never becomes a large `mat4[]` (triggers the NVIDIA `inverse()`/pipeline-creation hang — see parent doc)."

These read as one named hang with two different stated causes. Either (a) there are genuinely **two distinct
NVIDIA compiler triggers** (large `mat4[]` arrays AND `inverse()`) that have been conflated under one name, or (b)
one attribution is wrong and the docs should converge on the real trigger. The compact-joint-matrix design
(3-row 48-byte form, CPU-precomputed inverse-transpose normal matrices) is consistent with *both* avoidances —
it avoids both a large `mat4[]` and shader-side `inverse()` — so the design itself doesn't disambiguate.

This plan **reconciles the attribution**: determine whether both triggers are real (and if so, document both
distinctly) or whether one is a mis-attribution, then make the three doc sites consistent. Doc-only — the
mitigations (compact joints, CPU-precomputed inverses, no large `mat4[]`) all stay; only the *explanation* is
reconciled.

## Design

1. **Establish whether each trigger is independently real.** Sources of truth, in order of confidence: any repo
   history / commit message that introduced each avoidance (why the compact-joint split was made; why the
   `inverse()` ban was added); the actual symptom each guards against. The `inverse()` ban is a widely-reported
   NVIDIA GLSL compiler bug; the large-`mat4[]` hang is the rationale for the 48-byte joint form. If both are
   independently documented in history as separate observed hangs, they are two triggers.
2. **Decide the canonical framing:**
   - If **both real:** keep the parent `Shaders/CLAUDE.md` "Known Issues" as the canonical list and state *both*
     triggers there as distinct items ("(1) `inverse()` on mat3/mat4; (2) large `mat4[]` arrays"); have
     `ModelCommon.h` and `Model/CLAUDE.md` reference whichever trigger their mitigation actually addresses (the
     compact joint split addresses the large-`mat4[]` trigger; the precomputed normal matrix addresses
     `inverse()`), and stop conflating them with the `inverse()`/pipeline-creation slash.
   - If **one is a mis-attribution:** correct the wrong site to the real trigger so all three agree.
3. **Apply via `update-claude-docs`** for the two CLAUDE.md sites; `ModelCommon.h:46` is a source comment edited
   directly (one-line wording, comment-only).

## Out of scope

- **Any shader/code change** — the mitigations are correct and stay (compact 48-byte joints, CPU-precomputed
  inverse/normal matrices, no large `mat4[]`, no shader `inverse()`). Only the documentation rationale is
  reconciled.
- **Re-evaluating whether the mitigations are still needed** on current drivers — out of scope; this is about
  making the *recorded explanation* internally consistent, not re-testing the driver bug.
- **The joint-matrix layout** (3-row form) — unchanged.

## Acceptance criteria

- A determination on record of whether the large-`mat4[]` hang and the `inverse()` hang are two distinct NVIDIA
  triggers or one (with the wrong attribution identified).
- The three sites (`Shaders/CLAUDE.md` Known Issues, `Model/CLAUDE.md` Compact-joint note, `ModelCommon.h:46`
  comment) give a single consistent account — either both triggers documented distinctly, or all pointing at the
  one real trigger.
- No shader/code/behavior change.

## Critical files

- `Engine/Data/Shaders/CLAUDE.md` — "Known Issues" NVIDIA `inverse()` ban (canonical list candidate).
- `Engine/Data/Shaders/Model/ModelCommon.h` (`:46`) — "large mat4 arrays" comment (source-comment edit).
- `Engine/Data/Shaders/Model/CLAUDE.md` — "Compact joint matrices" note conflating both triggers.

## Notes

- **Doc-only reconciliation** — Risks = 0, no build/runtime/CRC impact; but it carries an *investigation* step
  (are there two triggers?) so it is more than a one-line stale-claim fix, hence its own plan rather than a
  `StaleDocClaimsSweep` line.
- If history is inconclusive on whether both triggers are real, the safe outcome is to document **both**
  avoidances as guarding against the NVIDIA pipeline-creation hang without over-claiming a single cause — the
  mitigations stand either way.
- The `ModelCommon.h:46` comment edit overlaps thematically with the source-comment sweep but is gated on this
  plan's reconciliation outcome, so it lives here, not in `StaleCodeCommentsSweep.md`.
