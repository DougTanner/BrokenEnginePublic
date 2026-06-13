# Decision: Parent Aggregation/Scope Rules vs Documented Leaf Exceptions

## Context

**Decision plan (present options).** Several broadly-stated parent/root CLAUDE.md rules have **documented,
load-bearing exceptions** now recorded at leaf CLAUDE.md files. The parent rules currently read as absolutes,
creating tension with the leaves. The question for each is the same: should the parent rule gain an "unless
noted at the subsystem hub/leaf" qualifier, or should the leaf exception be removed/changed? These are
**documentation-governance decisions** with no code change — resolved by interviewing the user. Grouped because
they are one recurring shape ("absolute parent rule + sanctioned leaf exception"). Each sub-decision is
independent; the user may rule differently per item.

This plan's deliverable is the **resolved wording** for each rule, not code. All file/line references the user
needs are captured below so no other context is required.

### Decision 1 — `ExternalHeaders.h` "all external headers" rule (conflict 1)

`Common/CLAUDE.md` states standard-library / external `#include`s go in `Common/ExternalHeaders.h` (the root
CLAUDE.md restates: "`#include <header>` additions go in `Common/ExternalHeaders.h`, not in individual source
files"). But **DataPacker-only third-party headers** (gli, cmft, SPIRV-Cross, tinygltf, openexr, bc7enc_rdo,
stb) are deliberately included **locally in DataPacker `.cpp`s** to keep offline-tool headers out of the engine
PCH. `ThirdParty/CLAUDE.md` now documents this exception.

- **Options:** (A) narrow the `Common/CLAUDE.md` / root rule to "**engine-consumed** external headers go in
  `ExternalHeaders.h`; DataPacker-only third-party headers are included locally (see `ThirdParty/CLAUDE.md`)";
  (B) keep the rule absolute and treat DataPacker as a stated carve-out cross-linked from both docs; (C) leave
  as-is (accept the tension).
- **Recommendation: (A)** — the narrowing is accurate (the rule's real intent is the *engine* PCH), and it
  removes the contradiction at the source rather than papering over it. The root CLAUDE.md line is a restatement;
  whichever wording lands, keep root and `Common/CLAUDE.md` consistent.

### Decision 2 — `BT_CLIENT` "narrowest practical scope" vs vcxproj+`Engine.h` gating (conflict 3)

Root CLAUDE.md: "Gate client-only code with `#ifdef BT_CLIENT` at the narrowest practical scope." But
`AnimationData.*`, `OneShotCommandBuffer.*`, `Screenshot.*`, `Graphics.h`, `GraphicsUtils.h` are client-only
with **no `#if` wrap** — they rely on **client-vcxproj membership + `Engine.h`'s `BT_CLIENT` span**. The leaf
docs describe this as an established complementary mechanism.

- **Options:** (A) add to the root rule "…or, for whole files/headers, via client-vcxproj membership + the
  `Engine.h` `BT_CLIENT` aggregation span (the established complementary mechanism)"; (B) require an explicit
  `#if defined(BT_CLIENT)` wrap on these files for belt-and-suspenders; (C) leave as-is.
- **Recommendation: (A)** — the vcxproj+`Engine.h` mechanism is real and intentional (the
  `VisualStudio2026/CLAUDE.md` client/server file-membership rule depends on it); the root rule should
  acknowledge both forms of gating rather than implying every client-only line needs an `#ifdef`. Option B adds
  redundant wraps for no safety gain (the file already can't compile into the server).

### Decision 3 — `Engine.h` "single aggregation header / every subsystem exposes itself through it" exceptions (conflict 4)

Root + `Engine/Source` hub state `Engine.h` is the single aggregation header and every subsystem exposes itself
through it. There are **three deliberate, load-bearing exceptions** now documented at leaves:
- Game `Input.h` directly includes `Engine/Source/Input/RawInputManager.h` (server build must see the RawInput
  struct shape).
- `NetworkCursor.h` is intentionally **not** aggregated (must be included directly).
- Ui wrapper headers are excluded so default-value edits don't recompile the world (only `NetworkUiControl.h` +
  TweaksScreen headers are aggregated).

- **Options:** (A) add an "…unless noted at the subsystem hub" qualifier to the parent rule and keep the three
  leaf notes as the authoritative exception list; (B) enumerate the three exceptions inline in the parent;
  (C) leave as-is.
- **Recommendation: (A)** — the qualifier keeps the parent rule honest without duplicating the exception list
  (which the leaves own and are the right place to maintain). The deciding test "does this break the single
  -aggregation invariant?" is preserved; the exceptions are sanctioned and discoverable at the hubs.

### Decision 4 — Engine→game compile-time-constant read (`keNetworkSimulation`) not in sanctioned forms (conflict 5)

Engine NetworkSimulation code reads `keNetworkSimulation`, a **compile-time constant in the game's `Pch.h`**.
This is consistent in spirit with the sanctioned `game::gp*` reads ("Engine reading `game::gp*` globals is by
design"), but the parent's stated sanctioned forms don't explicitly cover a **compile-time constant** (vs a
runtime `gp*` global).

- **Options:** (A) extend the sanctioned-forms wording to include "engine reading game-layer compile-time
  constants (e.g. `keNetworkSimulation` in game `Pch.h`)"; (B) relocate the constant to an engine-visible
  header if it's truly engine-consumed; (C) leave as-is (treat it as covered in spirit).
- **Recommendation: (A)** — same by-design rationale as the `gp*` reads; the form is just `constexpr` instead of
  a global. Documenting it closes the "is this a layer violation?" question that the refresh raised. Option B is
  heavier and may fight the game-owns-its-toggles pattern (the game `Pch.h` defines the sim toggles by design).

## Design

This plan produces **documentation wording decisions**, not code. For each of the four sub-decisions:
1. Confirm the option choice with the user (the recommendations above are starting points).
2. Apply the chosen wording to the parent rule and keep any restating doc (root CLAUDE.md, hub) consistent.
3. Keep the leaf exception notes as the authoritative detail where the recommendation is "qualify the parent".

Apply via the standard `update-claude-docs` step at execution; this plan only records the problem, options, and
recommendations so the grill can resolve them without re-discovery.

## Out of scope

- **Any code change** — these are all doc-rule wordings. No `#ifdef` added/removed, no header relocated (unless
  the user explicitly picks a "relocate" option, which would then be its own code plan).
- The other doc-framing conflicts (deposit-shader EWNS, layer-violation framing, Managers-singleton convention)
  — those were the separate `ParentRuleFramingReconciliation.md` decision plan (**landed and removed**).
- The `## Out of scope` backfill and Plans/Features canonical-framing governance — separate plan.
- The DataPacker LOG float-spec exception — separate small decision plan.
- Re-architecting `Engine.h` aggregation or the client/server gating mechanism — these decisions only adjust
  *wording* to match the established mechanisms, not change them.

## Critical files

- **Decision 1:** `Common/CLAUDE.md` (ExternalHeaders rule), root `CLAUDE.md` (the restating "Standard library
  headers" bullet), `ThirdParty/CLAUDE.md` (documents the DataPacker-local exception).
- **Decision 2:** root `CLAUDE.md` ("Gate client-only code…narrowest practical scope"),
  `Projects/.../Platforms/VisualStudio2026/CLAUDE.md` (the vcxproj-membership mechanism), the leaf docs for
  `AnimationData`/`OneShotCommandBuffer`/`Screenshot`/`Graphics.h`/`GraphicsUtils.h`.
- **Decision 3:** root `CLAUDE.md` + `Engine/Source/CLAUDE.md` (aggregation rule); leaves: game `Input.h` /
  Input hub, `Network/CLAUDE.md` (NetworkCursor.h), Ui hub (wrapper-header exclusion).
- **Decision 4:** `Engine/Source/Network/CLAUDE.md` (NetworkSimulation), root `CLAUDE.md` (sanctioned engine→game
  forms), game `Pch.h` (`keNetworkSimulation` definition).

## Notes

- **Decision plan (present options)** — deliverable is resolved CLAUDE.md wording; no build/runtime/CRC impact
  (Risks = 0). Resolve each sub-decision in `/external-grill-plan` before any doc edit.
- All four share a shape ("absolute parent rule + sanctioned leaf exception → add a qualifier"); the user may
  apply a uniform ruling or decide per-item. Note: **root CLAUDE.md is involved in 1-4** — those edits land
  under the normal doc process (root-CLAUDE.md changes are gated), not silently.
