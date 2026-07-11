# Aesthetic UI Design Pass

## Context

A mechanical layout pass just landed over the player-facing UI: shared layout
constants centralized in `Ui/Screens/MenuUtils.h`, the layout contract written
up in `Documents/UserInterfaceDesign.txt`, and every screen swept for the
*mechanical* defects — widget overlaps, clipping, screen overflow, dead space —
all screenshot-verified at 4K. That pass established correctness, not beauty.

The user reviewed the result and judged the main menu **still visually poor**.
This follow-up is a deliberately **subjective, aesthetic** design pass: the goal
is making the UI *attractive from a design perspective* — proportion, hierarchy,
balance, restraint — explicitly **beyond** mechanical rule-compliance.

This is not a bugfix and not a rules audit. The prior pass answered "is anything
broken?"; this one answers "would a designer ship this?" The two are different
standards and the second is the one that matters here.

The R/B screenshot channel-swap bug (which made earlier captures unreliable for
color judgement) has been fixed — screenshots now show true colors, so visual
critique from the agent harness is trustworthy.

### Nature of the work (read this before starting)

This work is **subjective visual design judgment**, not rule enforcement. The
executor iterates a tight loop, per screen:

1. Screenshot at native-resolution borderless fullscreen via the agent harness.
2. Critique the image against an absolute "would a designer ship this?" bar.
3. Adjust geometry / remove elements.
4. Re-screenshot and repeat until the screen reads as intentionally designed.

There is no numeric acceptance test. "Done" is the user approving the captures.

### Methodology — lessons from the landed pass (do not repeat these mistakes)

The prior pass anchored its judgment to the *previous/broken* state ("better
than before" = ship it). That before/after anchoring is the flaw that produced a
mechanically-correct but ugly result. Codify the corrected method:

- **Absolute standard, no before/after anchoring.** Judge each screen as if
  seeing it fresh in a shipped game. "Improved over the broken version" is not a
  pass. The only question is whether *this* frame looks designed.
- **Subtractive critique per screen.** For every element on a screen, ask what
  it contributes. If the answer is "nothing," remove it. Chrome, panels, gaps,
  and decorative boxes are guilty until they justify themselves. Fewer, better-
  placed elements beat more.
- **Explicit proportion / alignment / hierarchy check per screen.** Name, for
  each screen: what is the primary element, what is subordinate, is the primary
  element sized and placed to read first, is everything that should align
  actually aligned, are the size ratios between tiers deliberate.
- **View captures at full resolution.** Inspect the native-res image, not a
  downscaled thumbnail — fixed-pixel fonts and spacing only show true
  proportion at 1:1. Practical note for an agent executor: reading a 3840px PNG
  renders it downscaled (~2000px). Proportion/alignment/hierarchy judgments
  survive that; for fine detail (label clipping, 1-2px misalignment, kerning),
  crop the region of interest to a small image first and read that at 1:1.

## Design

### Optional first step — design research (recommended, the user requested it)

The user is not a designer and suggested grounding the aesthetic choices in
external guidance first. Make this an explicit **optional** first step:

- Research game-UI / title-screen design guidelines: visual hierarchy, spacing/
  scale systems (modular scale, whitespace ratios), and RTS/strategy-genre
  main-menu conventions (this is a top-down RTS-scale game — its menus should
  read in that idiom).
- Distill the findings into **additions to `Documents/UserInterfaceDesign.txt`**.
  That document currently codifies *mechanics* (scale system, sizing rules,
  placement, rhythm) but says nothing about *aesthetics*. If research yields
  durable principles (e.g. a title/subtitle/action scale ratio, a whitespace
  rhythm, "menus float over the scene, no decorative container"), add them as
  new numbered sections so the next screen inherits the standard instead of
  re-deriving taste.

Whether to do this research, and how deep, is a pre-staged grill decision (see
Notes). It is optional so a light-touch executor is not forced into a research
detour if the user would rather iterate directly on screenshots.

### Mandatory seed findings — the user's main-menu critique (all still open)

A partial fix was attempted and reverted; these four are the **starting items**,
each to be resolved in `MainMenuScreen.cpp` (and `MenuUtils.h` for any new
constant):

1. **Language row is far too large.** The language-select buttons span almost
   the entire bottom of the screen. They must become a **subordinate footer
   cluster** — smaller font scale and tighter padding so the language list reads
   as secondary to the menu buttons above. Current row: `ScopedMenuFont
   languageFont(kfMenuUiScale * 0.75f)` and per-button width `fLangButtonWidth +=
   ImGui::GetStyle().FramePadding.x * 4.0f` (`MainMenuScreen.cpp:138,160`).
   Reduce the scale multiplier and/or the padding multiplier; if the 0.75f
   changes, update `UserInterfaceDesign.txt` §2's sanctioned-exception note.

2. **Menu buttons are not centered** under the header / within their column.
   Align the button column with the heading (and each other) so the block reads
   as one centered unit rather than drifting.

3. **"BROKEN ENGINE" title is slightly too big.** It renders at
   `kfMenuUiScale * kfMenuHeadingScale` = `2.0 * 1.6` via `MenuHeading`
   (`MenuUtils.cpp` `MenuHeading`; `kfMenuHeadingScale` in `MenuUtils.h:10`).
   Bring it down modestly. Note: `MenuHeading` is **shared** by Pause, Graphics,
   and Sound — decide whether to lower the shared `kfMenuHeadingScale` (affects
   every heading) or give the main-menu title its own scale; verify the other
   screens' headings still read well under whatever is chosen (screenshot them).

4. **Remove the panel box behind the main-menu buttons entirely.** The
   `DrawPanelBackground(...)` call at `MainMenuScreen.cpp:39` (plus the
   `GetWindowPos`/`GetWindowSize` fetch feeding it) serves no purpose — the title
   and buttons should float directly over the live 3D scene. Delete the call.
   Update `UserInterfaceDesign.txt` §5 (which states MainMenu "composes over the
   live 3D scene") if removing the box changes what that section documents, and
   check no other main-menu code depends on the fetched window rect.

### Then: the same critique on every other player-facing screen

After the four seed items, apply the identical subtractive/proportion/hierarchy
critique with fresh eyes to each remaining player screen:

- **PauseMenuScreen** — the dim + chrome panel; is the panel earning its box, is
  the heading/button proportion right.
- **GraphicsMenuScreen** — two-column settings panel; column balance is already
  a mechanical commitment, judge the *aesthetic* density and heading proportion.
- **SoundMenuScreen** — volume sliders; proportion and whitespace.
- **ModalScreen** — connection-rejection / desync modal; sizing and button
  proportion.
- **HudScreen** — in-game fleet/focused-player panels; judge chrome and density
  (respecting the documented symmetry + fixed hover-zone commitments — those are
  mechanical invariants, not up for aesthetic removal).
- **Language row** (covered by seed item 1, but re-judge in context once the menu
  above it is fixed).

Each screen gets its own before/after native-res screenshots.

### Execution notes (session knowledge from the mechanical pass — saves rediscovery)

- **Harness workflow that works** (follow the agent-harness skill for lock/cleanup):
  Debug server `--agent-port 27100` + Debug client `--agent-port 27101` with **no
  `--windowed`** (= persisted borderless fullscreen at native 4K; fonts are fixed
  pixel sizes, so only native res shows true text proportion). Screenshot params:
  `{"format":"png","maxWidth":3840,"path":"<repo>/Temp/<name>.png"}`.
- **Reaching each screen (Debug builds auto-connect):** main menu = launch the
  client with **no server running** — it idles at the menu with a disabled
  SCANNING... button (also a layout state worth judging) — or in-session via
  `key ESC` → `click "MAIN MENU"`. Graphics has a main-menu variant (extra Time
  of Day row — judge that one, it is the density worst case). HUD right panel
  only appears with a focused owned player: `click "[+]##Fleet"` then
  `click "[+]##Player"` (spawns complete on an unpaused tick).
- **ModalScreen is currently unreachable at runtime**: its trigger path (connect
  to a dead server from the main menu) crashes the client — filed as
  `Documents/Plans/Engine/Bugfix_RenderFrameEmptySnapshotRingOnReconnect.md`
  (score 0, top of queue). Critique Modal from code/geometry only, or execute
  that bugfix first (recommended if it is still open — it is one guard).
- **Reference captures already on disk** in `Temp/`: `ui_after_*.png` (post-
  mechanical-pass state of every screen, but with R/B-swapped colors) and
  `ui_colorfix_mainmenu.png` (true colors, current main menu, no server). Do not
  trust the `ui_after_*` set for color judgement — layout only.
- **Starting values from the reverted partial attempt** (untested suggestions,
  not decisions — iterate visually): title scale ~1.35 (vs kfMenuHeadingScale
  1.6); language-row font scale ~0.5 (vs 0.75) with `FramePadding.x * 2` width
  padding (vs `* 4`). The language row's uniform width must keep measuring 中文
  under the CJK font (`mpChineseFont`) — that mechanism exists in
  `MainMenuScreen.cpp`, preserve it when shrinking.
- **Main-menu button width floor** is `kfPrimaryButtonMinWidthPixels = 760`
  (4K px). With the box gone and buttons centered, that floor is a tunable
  aesthetic choice, not a fixed constraint — judge it in the screenshots.

### Where new values live

Any new dimensional value (a reduced title scale, a footer padding, a centering
offset) becomes a **named `k*` constant in `MenuUtils.h`** per the contract's §4
— no inline magic literal in a screen `.cpp`. Update the `UserInterfaceDesign.txt`
constants list (§4) and the CLAUDE.md notes where a documented value changes.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp` —
  **primary.** All four seed items (`MainMenuScreen::Render`): `DrawPanelBackground`
  removal (`:39`), button centering, title size, language-row scale/padding
  (`:135-187`).
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h` — new named layout
  constants; `kfMenuHeadingScale` / any new title/footer scale constant.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp` — `MenuHeading`
  (if the title scale is decoupled from the shared heading), any chrome helper
  touched by panel/box removal.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/PauseMenuScreen.cpp`,
  `GraphicsMenuScreen.cpp`, `SoundMenuScreen.cpp`, `ModalScreen.cpp`,
  `HudScreen.cpp` — per-screen aesthetic critique + adjustments.
- `Documents/UserInterfaceDesign.txt` — update §2 (language-row 0.75f exception
  if that scale changes), §4 (constants list), §5 (MainMenu box removal), §9
  (heading scale if changed); add new numbered aesthetic sections if the research
  step yields durable principles.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md` — sync any per-screen
  note that a change invalidates.

## Out of scope

- **Theme colors / palettes / chrome color tables.** `ImGuiManager` `ThemePalettes`
  and `MenuUtils` `kMenuChromes` are explicitly out — this pass is layout/
  proportion/composition, not recoloring. (Removing a *box* is layout; recoloring
  a box is not.)
- **TweaksScreen** and all its subtabs — a developer tool, a sanctioned contract
  exception, not player-facing.
- **New UI features / new screens / new widgets** — no capability additions;
  this is a polish pass over existing screens only.
- **ImGui replacement** or any change to the underlying UI toolkit.
- **Mechanical layout defects** — overlaps/clipping/overflow were fixed by the
  prior pass; if a *new* one is introduced by an aesthetic edit, fix it, but do
  not re-audit for pre-existing mechanical bugs.

## Acceptance criteria

Subjective by design:

- **The user approves the screenshots.** This is the primary and final gate;
  there is no numeric substitute.
- The four mandatory main-menu seed items are resolved: language row is a
  subordinate footer cluster; menu buttons are centered under the header; the
  "BROKEN ENGINE" title is proportioned down; the panel box behind the main-menu
  buttons is gone (title/buttons float over the 3D scene).
- Every other player screen has had the subtractive/proportion/hierarchy critique
  applied and its native-res before/after captures reviewed.
- `describe_ui` widget label sets are byte-identical to before, OR any rename is a
  deliberate, verified harness-API change (see Constraints).
- `UserInterfaceDesign.txt` is updated wherever a documented rule/constant/
  exception changed.

## Notes

**Constraints carried over from the mechanical pass (still binding):**

- **Widget labels are a harness automation API** (`UserInterfaceDesign.txt` §10):
  the agent harness clicks/hovers/queries by visible label. Keep them byte-
  identical, or change one knowingly and verify — never as a side effect of moving
  or resizing a button.
- **New dimensional values become named constants in `MenuUtils.h`** per §4 — no
  inline magic layout literals in screen `.cpp`s.
- **`UserInterfaceDesign.txt` is the source of truth** and must be updated where a
  rule/constant/exception changes (§2 language-row 0.75f multiplier if the scale
  moves, §4 constants, §5 if the MainMenu box is removed, §9 heading scale).
- **C++ Code Change Process applies at execution** (CLAUDE.md). This plan carries
  a **Verification** dimension inherently — every change is screenshot-verified
  live via the agent-harness skill (native-res borderless fullscreen), which is
  both the design loop and the step-9 runtime check.

**State-invariant exposure:** none. Client-only ImGui screen geometry — no
determinism/CRC sim path, no `kiVersion`/`.pack` layout, no replay, no wire
format, no client/server guard-scope change (the screen `.cpp`s are already
whole-file `BT_CLIENT`-wrapped). No allocation-tracked-path risk beyond the
existing `LOG`/workbuffer discipline the screens already follow.

**Pre-stage for `/external-grill-plan` (the open decisions):**

1. **How much web research up front?** Options: (a) none — iterate directly on
   screenshots against the absolute standard; (b) a light single-pass survey of
   game title-screen / RTS-menu conventions to seed the taste; (c) a deeper
   `/deep-research` pass distilled into `UserInterfaceDesign.txt`. Recommend (b):
   enough to ground choices, not a detour. Since the user self-identifies as
   not-a-designer, some external grounding is likely valuable.
2. **Should the aesthetic standard be codified into `UserInterfaceDesign.txt` as
   new numbered sections?** Options: (a) yes — add aesthetic principles (scale
   ratios, whitespace rhythm, "float over scene / no decorative container") as new
   §12+ so future screens inherit taste, not just mechanics; (b) no — keep the
   contract mechanics-only and treat this pass as one-off tuning. Recommend (a) if
   the research step runs, so the distilled principles have a home; the document
   today is explicitly mechanics-only ("not a style guide").
