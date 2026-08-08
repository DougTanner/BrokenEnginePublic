# Game UI Screens

ImGui menus, HUD, modal surfaces, and the game extension of engine TweaksScreen. Visual placement and hierarchy follow `../../../../../Documents/UserInterfaceDesign.txt`; this document owns runtime contracts that code changes must preserve.

## Shared Contracts

- Screen bodies are client-only except the shared menu utilities and death-flow placeholder used by both projects. Headers must remain parseable in their project affinity.
- Screens gate themselves from authoritative game/UI state because `ImGuiManager` invokes main and modal surfaces independently of in-game screen gating.
- Use the common 2160-height UI scale for fonts, style geometry, authored pixel dimensions, and anchors. Measure text under the active font and add style padding; do not rescale measured dimensions.
- A screen registers its rendered rectangle for world-render occlusion only while its background is fully opaque. A screen that selects its own background alpha independently of the opaque-UI setting stays unregistered, even when that alpha is fully opaque.
- Networked controls remain disabled through `NetworkUiControl` until authoritative state resolves the request.
- Convert localized UTF-32 strings into workbuffer-backed UTF-8 at the consuming expression; do not retain the result beyond its workbuffer lifetime.
- The HUD is the only game UI that changes network subscription state: its fleet and player focus controls tell the client session which cells it wants, tagged with an explicit reason. A rebuilt focus button that drops that call leaves the client drawing a cell it never subscribed to. Its nav-delay slider likewise sends one fleet request on drag release, not one per frame, and marks itself pending until the server answers.
- Opening the Graphics screen from the main menu seeds the sun-angle override from the live camera, so Time of Day starts where the sky already is; the camera keeps returning that override while the screen is up. The pause-menu entry point does not seed it, so opening Graphics in game snaps the sun — a real, existing asymmetry. Any new entry point has to choose one of the two behaviors; nothing at compile time links them.
- The game TweaksScreen is a developer tool gated twice: it compiles in only under the `kbDebugInput` switch, and even then draws only while the runtime ImGui toggle is on. It is not a player-facing screen.
- Settings controls bind through the shared menu helpers. Tweaks sliders instead follow the engine TweaksScreen contract (`../../../../../Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md`).
- Control and header label text is the harness automation API and does not get renamed. Several Graphics rows nonetheless repeat the same option labels, so harness automation that resolves a UI item by its label text gets an ambiguous result for those buttons and has to target them by coordinates. Rows that share labels stay distinguishable to ImGui by scoping each row to its wrapper, not by making the labels unique. Graphics slider rows draw their own label to the left of the bar, so the slider itself carries a hidden-label id ("##" plus the same name): the snapshot records that id, and a query for the visible name resolves through the registry's substring tier.

## Layout and Rendering

- Rendered target-resolution output is authoritative for judging whether spacing looks centered, not just whether it measures centered; window bounds and text metrics are diagnostics.
- Menu font pushes must be balanced within the owning ImGui window. Construct scoped font helpers before `Begin` or entirely inside the window so `End` sees the expected stack.
- Shared helpers own scaling, localization conversion, wrapper bindings, common button sizing, and menu chrome. Screen-specific layout values stay local and named.
- Panels take their fill from the window background; accent drawing adds border and strip geometry over it and never substitutes a fill.
- Hover animation is caller-owned state and composes ImGui alpha, including disabled controls, without heap allocation.

## Ownership

- `ImGuiManager` owns screen invocation and submission; this directory owns game-state gating and interaction semantics.
- Engine TweaksScreen owns the section registry, persistence, tables, and slider mapping. Game Tweaks files register their own whole sections through it and implement extension hooks for sub-tabs of engine sections.
- Making the agent UI snapshot visible to readers belongs to `../../../../../Engine/Source/Agent/AGENTS.md`; visual inspection and input command semantics are outside this directory.
