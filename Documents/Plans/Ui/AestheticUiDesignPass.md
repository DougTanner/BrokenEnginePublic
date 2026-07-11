# Aesthetic UI Design Pass — Remaining Screens

## Summary

Apply the approved Main Menu visual language to the remaining player-facing ImGui surfaces: Pause, Graphics, Sound, Modal, and HUD. The Main Menu is complete and is the fixed style anchor for hierarchy, optical balance, restrained chrome, type weight, spacing, and subordinate actions; it is not part of this plan's implementation scope.

Rendered pixels are authoritative. Every design decision must be judged from native-resolution screenshots of what the player sees. ImGui windows, item bounds, calculated centers, and other layout metrics are diagnostic aids only and cannot establish visual correctness.

## Scope and source locations

- Pause: panel/dim treatment and heading/action balance in `Projects/BrokenEngineSandbox/Source/Ui/Screens/PauseMenuScreen.cpp`.
- Graphics: density, two-column balance, heading, controls, and Back action in `Projects/BrokenEngineSandbox/Source/Ui/Screens/GraphicsMenuScreen.cpp`. Cover both the normal screen and the denser Main Menu Time-of-Day variant.
- Sound: whitespace, hierarchy, control widths, and action proportion in `Projects/BrokenEngineSandbox/Source/Ui/Screens/SoundMenuScreen.cpp`.
- Modal: message wrap width, vertical rhythm, and centered OK action in `Projects/BrokenEngineSandbox/Source/Ui/Screens/ModalScreen.cpp`.
- HUD: chrome and information density in `Projects/BrokenEngineSandbox/Source/Ui/Screens/HudScreen.cpp`, while preserving fixed hover extent and left/right symmetry.
- Shared dimensions and helpers: `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h` and `MenuUtils.cpp`. Add or change shared values only when more than one remaining screen genuinely shares the rule; keep screen-specific values local and named.
- Layout contract: `Documents/UserInterfaceDesign.txt`.
- Leaf documentation: `Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md` only where implementation makes its current descriptions false.

Out of scope: further Main Menu changes, color-theme redesign, textured chrome, new widgets or screens, TweaksScreen, profiling overlays, DeathMenu composition, and replacement of ImGui.

## Execution

1. Launch the Debug client through the agent harness in persisted native-resolution borderless fullscreen. For each in-scope surface, capture a true-color screenshot and `describe_ui` label set before editing. Use an existing reachable Modal path; do not expand this plan into network-disconnect behavior.
2. Critique each screenshot against the approved Main Menu anchor and an absolute shipped-game standard. Judge visible hierarchy, optical alignment, perceived weight, scale ratios, whitespace, density, and whether every visible element earns its space. Do not use before/after improvement or centered ImGui bounds as acceptance evidence.
3. Recompose one screen at a time with the smallest client-only ImGui geometry changes that resolve the visible problems. After every material adjustment, recapture at native resolution and repeat the pixel-level critique before moving on.
4. Graphics must remain usable across the supported Font Size range. Fix the known Font Size `3.0` overflow: `Back` is currently off-screen and the minus control is partly clipped. At minimum, capture and inspect Graphics at Font Size `0.5`, `1.0`, and `3.0`; at `3.0`, every control and label must remain fully visible, reachable, and compositionally coherent without breaking the two-column relationship or the Time-of-Day variant.
5. Preserve all mechanical invariants while tuning presentation: Pause dim/input behavior; Graphics control semantics and column relationships; Sound control behavior; Modal wrapping/action behavior; HUD activation, fixed hover extent, and left/right symmetry.
6. Keep every widget label byte-identical. Compare each screen's final `describe_ui` label set with its baseline because labels are part of the agent-harness automation API.
7. Synchronize `Documents/UserInterfaceDesign.txt` with any changed layout rules, named dimensions, exceptions, and verification guidance. Update the screen leaf `AGENTS.md` only when needed for accuracy.
8. Run the complete repository C++ Code Change Process: implementation self-audit; affected-code sweep; independent correctness and adversarial reviews; style review; documentation and architecture checks; vcxproj verification; client/server compile; agent-harness runtime verification; and paired session audits. Route non-trivial residuals to separate plan files rather than expanding this pass.
9. Present the final native-resolution screenshots to the user. Completion requires explicit user approval of Pause, Graphics, Sound, Modal, and HUD as a coherent family anchored by the approved Main Menu.

## Acceptance criteria

- Native-resolution screenshots, not abstract geometry, demonstrate that each remaining screen is visually balanced and consistent with the approved Main Menu.
- Pause, Graphics, Sound, Modal, and HUD each pass a fresh subtractive critique with no unjustified chrome or spacing.
- Graphics at Font Size `3.0` keeps `Back`, the minus control, and every other control fully on-screen and usable; Font Size `0.5` and `1.0` remain visually sound.
- Graphics normal and Main Menu Time-of-Day variants retain clear, balanced columns.
- HUD preserves activation behavior, fixed hover extent, and left/right symmetry.
- Pause, Sound, and Modal preserve their existing interaction behavior.
- Widget labels match their baseline byte-for-byte.
- Client and server builds pass, harness logs show no new errors, and the full C++ review process is complete.
- `Documents/UserInterfaceDesign.txt` and affected leaf documentation accurately describe the landed rules.
- The user explicitly approves the final screenshot set.

## Constraints and invariant exposure

- Changes are client-only ImGui layout and composition. Do not alter deterministic PostRender/CRC state, `kiVersion`, `.pack` or manifest layout, replay/save format, protocol/wire behavior, client/server guard scope, or allocation-tracked runtime behavior.
- The approved Main Menu is the visual authority for this pass. Do not reopen it unless the user explicitly requests a separate correction.
- Do not rename labels, invent missing surfaces, or remove working behavior to simplify layout.
- Native-resolution screenshots must be captured after restoring representative language and Font Size settings; record the tested settings with the acceptance evidence.
