# Game UI Screens

ImGui menus, HUD, modal surfaces, and the game extension of engine TweaksScreen. Visual placement and hierarchy follow `../../../../../Documents/UserInterfaceDesign.txt`; this document owns runtime contracts that code changes must preserve.

## Shared Contracts

- Screen bodies are client-only except the shared menu utilities and death-flow placeholder used by both projects. Headers must remain parseable in their project affinity.
- Screens gate themselves from authoritative game/UI state because `ImGuiManager` invokes main and modal surfaces independently of in-game screen gating.
- Use the common 2160-height UI scale for fonts, style geometry, authored pixel dimensions, and anchors. Measure text under the active font and add style padding; do not rescale measured dimensions.
- Opaque-background screens register their rendered rectangle for world-render occlusion. Transparent overlays do not.
- Networked controls remain disabled through `NetworkUiControl` until authoritative state resolves the request.
- Convert localized UTF-32 strings into workbuffer-backed UTF-8 at the consuming expression; do not retain the result beyond its workbuffer lifetime.
- Settings controls bind through the shared menu helpers. Tweaks sliders instead follow the engine TweaksScreen contract (`../../../../../Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md`).

## Layout and Rendering

- Rendered target-resolution output is authoritative for optical balance; window bounds and text metrics are diagnostics.
- Menu font pushes must be balanced within the owning ImGui window. Construct scoped font helpers before `Begin` or entirely inside the window so `End` sees the expected stack.
- Shared helpers own scaling, localization conversion, wrapper bindings, common button sizing, and menu chrome. Screen-specific layout values stay local and named.
- Opaque panels preserve their themed background when drawing accents. Transparent pause/main-menu surfaces remain unregistered.
- Hover animation is caller-owned state and composes ImGui alpha, including disabled controls, without heap allocation.

## Ownership

- `ImGuiManager` owns screen invocation and submission; this directory owns game-state gating and interaction semantics.
- Engine TweaksScreen owns the section registry, persistence, tables, and slider mapping. Game Tweaks files register their own whole sections through it and implement extension hooks for sub-tabs of engine sections.
- Agent UI snapshot publication belongs to `../../../../../Engine/Source/Agent/AGENTS.md`; visual inspection and input command semantics are outside this directory.
