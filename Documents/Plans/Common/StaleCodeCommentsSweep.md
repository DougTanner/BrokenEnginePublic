# Stale Code-Comment Sweep (Cross-Subsystem)

## Context

The repo-wide CLAUDE.md refresh found a batch of **in-code comments** that no longer match the code they
describe. These are comment-only edits (zero behavior, zero risk) collected into one sweep. Each is independent;
the sweep can land any subset. (Doc-file staleness — CLAUDE.md / architecture / plan docs — is handled by
separate plans; this plan is *source-file comments* only.)

### Items

1. **DataPacker `Main.cpp` `RunRdoSweepValidate`** labels `{4096u, 0.5f, 2}` as "current production knobs",
   but `Texture.cpp` production is **lookback 1024 / lambda 4.0 / uber 4**. Correct the comment to the real
   production knobs (or drop the "current production" claim if the sweep values are intentionally a test point).
2. **DataPacker `ExportIsland.cpp:202`** says the encoder "uses all" threads; it actually spawns
   `HardwareCoreCount() - 2`. Correct the count.
3. **`MainUniforms.cpp:176-177` and `PipelineManager.cpp:374`** claim terrain no longer uses indirect draws,
   but `CommandBufferRecordMain.cpp:257-264` issues one `vkCmdDrawIndexedIndirect` per island template. The
   comments describe a superseded intermediate state — update them to reflect that terrain **does** use indirect
   draws (per island template).
4. **`FileManager.h:181`/`:186` and `FileManager.cpp:312`** "pre-faulted" comments contradict the actual
   `MEM_COMMIT`-only allocation code (the buffer is committed, not pre-faulted/touched). Reword to "committed,
   not pre-faulted".
5. **`IslandChainPlacement.h` class comment** still describes a golden log-spiral layout; the `.cpp` implements
   a hard-turning contact-growth chain. Rewrite the class comment to match the contact-growth algorithm.
6. **`SmokeOccupancyDilate.comp` / `SmokeOccupancyDilateRemap.comp`** comments say "Manhattan-2 dilation"; the
   loops are a full ±2 square (5×5 box / Chebyshev). Correct to "5×5 box (Chebyshev-2) dilation".
7. **`WaterSkyboxOne.frag`** mirroring comment cites `Water.frag` line ranges (82-159 etc.) that have drifted.
   Replace the brittle line-range citation with a symbol/function reference (or drop the line numbers).
8. **`Players.cpp`** labels its `0x40FFFF00` shield-color literals "(RGBA)", but the engine decode order is
   **ABGR**. Correct the comment to ABGR (the literal value is unchanged — only the comment's channel-order
   claim is wrong).
9. **`SoundWrappers.cpp` header comment** says "Default volumes are 0"; most defaults are non-zero
   (0.1 / 0.075 / 0.05). Correct the comment.
10. **`SmokeSpreadTwo.comp:60-61`** comment claims Pass-Two "refines Pass-One's output in current-area coords
    (no cross-frame import)". The recorded frame order (Pass B = `SmokeSpreadTwo.comp` records *before* Pass A,
    per `CommandBufferRecordGlobal.cpp` and `Smoke/CLAUDE.md`) has Pass B read TextureOne = the *previous*
    frame's pass-A output plus deposits, **still in previous-area coordinates** (the one-sided remap happens in
    Pass A, not B). So the comment's "current-area coords (no cross-frame import)" is backwards for this pass.
    Reword to match the documented contract (Pass B reads previous-frame pass-A output at the same UV in
    previous-area coords; the remap is Pass A's job). Verify against the `Smoke/CLAUDE.md` "Recorded Frame Order"
    section, which the `.comp` headers are meant to be the authoritative statement of.
11. **`WindOccupancyDilate.comp:53`** comment says "Manhattan-2 neighbors", but the loop (`iDy=-2..2` × `iDx=-2..2`)
    is a **5×5 box (Chebyshev radius 2)**, not a Manhattan/diamond region. Correct to "5×5 box (Chebyshev-2)".
    (Same correction family as item 6's Smoke "Manhattan-2"; the `Wind/CLAUDE.md` prose already says "5x5 box
    (radius 2)" — only the in-code comment is stale.)
12. **`RawInputManager.cpp:42`** comment on the mouse `RIDEV_INPUTSINK` registration says it "adds HID mouse and
    also ignores legacy mouse messages". `RIDEV_INPUTSINK` does **not** suppress legacy messages (unlike
    `RIDEV_NOLEGACY` on the keyboard at `:47`) — legacy mouse messages still arrive and feed DirectXTK `Mouse`
    via Main.cpp's WndProc (`Input/CLAUDE.md` documents this: raw mouse packets reaching `HandleRawInput` are
    discarded; DirectXTK is fed by legacy messages). Drop the "ignores legacy mouse messages" clause. **NOTE:**
    `DeadCodeAndUnusedIncludesSweep.md` item 8 separately considers *removing* the mouse `RIDEV_INPUTSINK`
    registration entirely (behavior-gated); if that lands first this comment goes away with it — coordinate so
    the two don't conflict.
13. **`Model.frag:102`** tone-map helper comment says "ACES filmic (Stephen Hill fit)", but the constants
    (a=2.51, b=0.03, c=2.43, d=0.59, e=0.14) are the **Narkowicz** ACES approximation, not the Stephen Hill
    fit. Correct the attribution to Narkowicz. (The `Model/CLAUDE.md` "Tone mapping" note repeats the wrong
    "Stephen Hill fit" attribution — fix it there too under the standard `update-claude-docs` step, not from
    this source-comment sweep.)

## Design

For each item, edit the comment to match the current code. **No code changes** — verify the code's actual
behavior first (the report already did, but re-confirm at execution since line numbers drift), then fix the
wording. For line-range citations that have drifted (item 7, and item 3's cross-file references), prefer naming
the function/symbol over re-pinning a line number, so the comment survives the next edit.

Items 1 and 8 carry a small verification step:
- **Item 1**: confirm the real production knobs in `Texture.cpp` before editing (the sweep file may legitimately
  use different test values — the bug is only the "current production" *label*).
- **Item 8**: confirm the engine's color-literal decode order (the report says ABGR) against the actual
  `0x40FFFF00` consumption so the corrected comment is right.

Keep edits minimal and local; do not restyle surrounding comments.

## Out of scope

- **Any code change** — every item is comment-only. If fixing a comment reveals an actual code bug, that is a
  *separate* finding to route through C++ Code Change Process step 10, not this sweep.
- **CLAUDE.md / architecture-diagram / plan-doc staleness** — handled by the separate stale-docs plans
  (`StaleDocClaimsSweep.md`, the architecture-diagram fix, the Order.md/ShaderReview cleanups).
- The smoke/terrain/water *behavior* the comments describe — unchanged; only the descriptions are corrected.
- Re-tuning the RDO sweep values or the encoder thread count (item 1/2) — those are working as intended; only
  their comments are wrong.

## Acceptance criteria

- Each listed comment matches the code it describes (verified against current source).
- Drifted line-range citations (items 3, 7) are replaced with symbol references or removed.
- No source behavior changed; affected projects still build (comment edits cannot break the build, but a build
  confirms no stray edit slipped in).

## Critical files

- `DataPacker/Source/Main.cpp` (`RunRdoSweepValidate` knobs comment), `DataPacker/Source/ExportJobs/
  ExportIsland.cpp` (`:202` thread-count comment).
- `Engine/Source/Graphics/Render/MainUniforms.cpp` (`:176-177`), `Engine/Source/Graphics/Managers/
  PipelineManager.cpp` (`:374`), `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` (`:257-264`,
  the indirect-draw site the comments should reflect).
- `Engine/Source/File/FileManager.h` (`:181`/`:186`), `Engine/Source/File/FileManager.cpp` (`:312`).
- `Engine/Source/Frame/IslandChainPlacement.h` (class comment) — note: this file's `.cpp` is in the working
  set; the class comment in the header is the target.
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilate.comp`, `Engine/Data/Shaders/Smoke/
  SmokeOccupancyDilateRemap.comp`, `Engine/Data/Shaders/Water/WaterSkyboxOne.frag`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` (`0x40FFFF00` "(RGBA)" comment).
- `Projects/BrokenEngineSandbox/Source/Ui/.../SoundWrappers.cpp` (header "Default volumes are 0" comment).
- `Engine/Data/Shaders/Smoke/SmokeSpreadTwo.comp` (`:60-61` Pass-Two "current-area coords / no cross-frame
  import" comment — item 10). Read-only reference: `Engine/Data/Shaders/Smoke/CLAUDE.md` "Recorded Frame Order".
- `Engine/Data/Shaders/Wind/WindOccupancyDilate.comp` (`:53` "Manhattan-2 neighbors" comment — item 11).
- `Engine/Source/Input/RawInputManager.cpp` (`:42` mouse `RIDEV_INPUTSINK` "ignores legacy mouse messages"
  comment — item 12; coordinate with `DeadCodeAndUnusedIncludesSweep.md` item 8).
- `Engine/Data/Shaders/Model/Model.frag` (`:102` "Stephen Hill fit" ACES attribution — item 13; the
  `Model/CLAUDE.md` "Tone mapping" note carries the same wrong attribution, fixed via update-claude-docs).

## Notes

- Comment-only — zero behavior, zero risk (Risks = 0), purely a documentation-accuracy lift.
- Two items (1 RDO knobs, 8 color order) need a quick code-read to get the corrected wording right; the rest are
  direct. Item 10 (Smoke Pass-Two) needs a cross-check against the recorded pass order in
  `CommandBufferRecordGlobal.cpp` / `Smoke/CLAUDE.md` before rewording.
- The shader-comment items (6, 7, 10, 11, 13) do not require a DataPacker recompile (comments don't affect
  compilation), but a recompile is harmless if the sweep is bundled with other shader work.
- Items 12 (RawInput) and 13 (Model ACES) each have a *sibling* in another doc/plan to keep consistent: item 12
  with `DeadCodeAndUnusedIncludesSweep.md` item 8 (potential removal of the registration), item 13 with the
  `Model/CLAUDE.md` "Stephen Hill fit" attribution (fixed under update-claude-docs, not this sweep).
