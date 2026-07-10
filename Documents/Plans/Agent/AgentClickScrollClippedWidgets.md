# click / hover / set_slider Silently No-Op on Scroll-Clipped Widgets

## Summary

**What this plan does:** Makes the agent harness's `click`/`hover`/`set_slider` commands fail with an explicit "target not visible (scrolled out of view)" error when the resolved widget is scroll-clipped, instead of driving the mouse to an off-screen rect center and reporting success. One visibility guard in `AgentInput::StabilizeTarget` (shared by all three commands) using the `ImGuiItemStatusFlags_Visible` bit the registry already captures, a new `AgentScriptStatus::kClipped`, and its error mapping in the client command dispatch. Optionally (grill decision) also surfaces a `visible` boolean per item in `describe_ui`.

**Why it's good for the codebase:** Turns a verified silent false-success (GraphicsMenu "Opaque UI" checkbox at rect y=[1546,1614] on a 904-px framebuffer: click "succeeds", `checked` unchanged) into an honest failure the harness can act on — scroll, then retry. Any agent-driven verification workflow that trusts `ok:true` from these commands is currently unreliable on scrollable menus; ~10 lines close that hole with no new machinery.

## Context
- Source: Documents/Plans/Agent/AgentClickScrollClippedWidgets.md (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 1 / Score 1 (marked `[CLAIMED]` in Step 2)
- Notes: Harness validation run: `click`/`hover`/`set_slider` on a scroll-clipped widget silently no-op with `found:true` — registry keeps clipped items with off-screen rects (e.g. rect y 1546 vs 904 framebuffer), `StabilizeTarget` never checks visibility. Fail with "not visible" via the captured `ImGuiItemStatusFlags_Visible` bit; consider a `visible` flag in `describe_ui`. Client-only harness
- Relevance: Fully — every cited mechanism verified against current source (hook order `imgui.cpp:11243` vs clip test `:11247` vs `Visible` set `:11292`; registry `iStatusFlags = 0` at `AgentUiRegistry.cpp:100`, OR-accumulate at `:120`; `StabilizeTarget` at `AgentInput.cpp:96-146` with no visibility check)
- Dependency resolution: none (user-specified target; no `## Dependencies` edges)
- Changes since the plan was written:
  - Favorable drift: `miResolvedStatusFlags` is already assigned for ALL script kinds at the `miStableCount >= 2` success point (`AgentInput.cpp:142`), not only in the kSetSlider path as the plan assumed — the guard is a single edit at one site, no per-kind capture work needed.
  - Concurrent session: `Agent/AgentSetSliderCtrlTyping.md` is claimed by a parallel session and also edits `AgentInput.cpp` (kSetSlider phase machine, `:239-297` region) — disjoint from `StabilizeTarget`; expect line drift only, no logic conflict.

## Verified mechanism (current line numbers)

`ImGuiTestEngineHook_ItemAdd` (`AgentUiRegistry.cpp:223-232`) fires from `IMGUI_TEST_ENGINE_ITEM_ADD` at `imgui.cpp:11243`, **before** the clip test (`:11247`), so `HookItemAdd` (`AgentUiRegistry.cpp:90-105`) records every item — clipped or not — with its full logical rect and `iStatusFlags = 0` (`:100`). The `ImGuiItemStatusFlags_Visible` bit (`imgui_internal.h:997`) is set at `imgui.cpp:11292` after the clip test; a visible interactive widget then emits `IMGUI_TEST_ENGINE_ITEM_INFO`, which `HookItemInfo` OR-accumulates (`AgentUiRegistry.cpp:120`). A fully-clipped widget short-circuits after `ItemAdd` returns false, so its `ITEM_INFO` never fires and the registry item never gains the `Visible` bit. `AgentInput::StabilizeTarget` (`AgentInput.cpp:96-146`) resolves the label, drives the mouse to the rect center (`:118-119`), and returns success without ever checking visibility; only kSetSlider reads `miResolvedStatusFlags` at all (`Inputable`, `:230`).

## Execution steps

1. `Engine/Source/Agent/AgentInput.h` — add `kClipped` to `AgentScriptStatus` (after `kNotInputable`, `:39`) with a one-line comment: target resolved but lacks `ImGuiItemStatusFlags_Visible` (scroll-clipped out of view).
2. `Engine/Source/Agent/AgentInput.cpp` `StabilizeTarget` — inside the `miStableCount >= 2` success block (`:139-143`), after `miResolvedStatusFlags = rItem.iStatusFlags;`, gate: if `(miResolvedStatusFlags & ImGuiItemStatusFlags_Visible) == 0`, `Finish(AgentScriptStatus::kClipped)` and return false. Guard comment must note the caveat: the `Visible` bit is populated only via `ITEM_INFO` (all interactive targets — checkboxes, sliders, buttons — emit it); non-interactive items that never emit `ITEM_INFO` read as not-visible, but those are not valid targets anyway. This one site covers kClick, kHover, and kSetSlider — no per-kind edits.
3. `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — in the `BeginScriptAndDefer` deferred poll's status mapping (`:313-328`, alongside `kTimeout`/`kNotFound`/`kAmbiguous`/`kNotInputable`), map `kClipped` to `throw std::runtime_error("target not visible (scrolled out of view)")`.
4. *(Decided at grill: yes)* `AgentCommandsClient.cpp` `BuildDescribeUi` — add `{"visible", (rItem.iStatusFlags & ImGuiItemStatusFlags_Visible) != 0}` to the per-item JSON (`:163-172`).
5. `[auto-folded sibling]` `.claude/skills/agent-harness/SKILL.md:139` (`click` entry) — replace the caller workaround tail ("**Check the item's rect is inside `framebuffer` first** — clicking a scroll-clipped widget reports success but hits nothing (`Documents/Plans/Agent/AgentClickScrollClippedWidgets.md`)") with the new behavior: clicking a scroll-clipped (off-screen) widget errors with "target not visible (scrolled out of view)" — scroll it into view first. Removes the plan-file pointer (file is deleted on completion). Keep the error phrasing in sync with step 3's actual string.
6. *(In scope — step 4 decided yes)* `.claude/skills/agent-harness/SKILL.md:136` (`describe_ui` entry) — append `"visible"` to the items field list and replace the tail sentence "Scroll-clipped items keep their off-screen rect (may exceed `framebuffer`) with no visibility flag." with: "Scroll-clipped items keep their off-screen rect (may exceed `framebuffer`); `visible:false` flags them."

## Additional candidate locations

- `AgentInput.cpp:173/212/239` re-pin of `mf2TargetCenter` per action phase — **Oversight / Identical / high** — **non-candidate** (sweep-confirmed): runs only after `StabilizeTarget` returns true, so the single stabilization gate short-circuits them; no extra guards.
- `AgentCommandsClient.cpp:156-173` `BuildDescribeUi` clipped items emitted with no visibility field — **Oversight / Related / high** — **already in plan** as the optional step 4; not new scope.
- `.claude/skills/agent-harness/SKILL.md:139` click workaround doc — **Oversight / Related / high** — **Folded** (gate verdict: mechanical, self-contained, doc-only; step 5).
- `.claude/skills/agent-harness/SKILL.md:136` describe_ui field-list doc — **Oversight / Related / high** — **Surfaced**, but resolved by the plan's own grill decision (step 4): fold step 6 iff the `visible` field is added; no separate user decision needed.
- Sweep confirmed no other code consumers (CandidateLabels lists labels only; `describe_scene`'s `WorldToScreen` is reporting-side with its own `InVisibleArea` filter) and no post-plan drift (the harness landed in one commit).

## Out of scope

- Auto-scrolling clipped targets into view (larger follow-up if the harness needs it).
- The registry's best-effort overflow drop and off-screen-rect retention (`AgentUiRegistry.cpp:93-95`) — behavior kept; the plan reads the existing data, it does not restructure the snapshot.
- Window-level occlusion (an item under another window) — scroll/clip visibility only; the `Visible` bit reflects the clip rect, not z-order.
- The set_slider Ctrl-typing defect (`AgentSetSliderCtrlTyping.md`) — independent plan, claimed by a concurrent session; do not touch the kSetSlider phase machine.
- SKILL.md `set_slider`/workflow guidance adjacent lines (`:145`, `:177`) — not stale under this change; leave to a future doc pass.

## Acceptance criteria

- `click label:"Opaque UI"` while that checkbox is scrolled out of view returns `ok:false` with the "not visible" error instead of a false `kDone`; scrolling it into view first then clicking toggles `checked`.
- (If step 4 lands) `describe_ui` reports `visible:false` for that clipped item and `visible:true` once scrolled into view.

## Notes

- **Invariant exposure.** Client-only automation path (`AgentInput`/`AgentUiRegistry`, whole-file `BT_CLIENT`). No wire/CRC/`kiVersion`/determinism change. No allocation-tracked-path concern (error path throws a `std::string`-constructed exception like its siblings at `:315-327` — same existing pattern).
- **Grill resolved (2026-07-09).** `visible` flag in `describe_ui`: YES — steps 4 and 6 are in scope. VEH-spam interaction acknowledged: the new `kClipped` throw matches the sibling error contract (`kNotFound`/`kAmbiguous`/`kNotInputable`); the first-chance-exception StackWalker spam it trips is the pre-existing defect `Agent/AgentCommandErrorExceptionSpam.md` fixes (claimed by a concurrent session) — proceed, do not diverge from the throw contract here.
