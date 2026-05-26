# Split Game.cpp (over the /reduce-file threshold)

## Context

`Projects/BrokenEngineSandbox/Source/Game.cpp` is ~1495 lines, well past the project's
~1000-line `/reduce-file` hard threshold. This is a **pre-existing** condition — the recent
~12-line main-menu-island-browser change (`BuildMenuIslandPlacement` + the `kCycleMenuIsland`
branch in `ProcessDebugInput`) did not cause it and is not the subject of this plan. The
`code-review` skill flagged the file for `/reduce-file` during that change's step-9 audit.

The file is genuine code (not comment-dense), so a split is warranted. It already falls into
clean seams: shared frame-grid orchestration, client-only fleet navigation/selection, and the
big client-only persisted-settings load/save block.

## Design

Keep `game::Game`'s class declaration in `Game.h` unchanged — this is purely a relocation of
member-function *definitions* across translation units. All split files stay in
`Projects/BrokenEngineSandbox/Source/` and reopen `namespace game`.

Proposed seams (verified against current line ranges):

1. **`GameSettings.cpp` — client-only (`#if defined(BT_CLIENT)`), new TU.**
   The entire persisted-settings block, currently `Game.cpp:995-1298`, all already inside one
   `#if defined(BT_CLIENT)` guard:
   - `SoundSettings` POD + `kpcSoundSettingsPath` + `SaveSoundSettings` / `LoadSoundSettings` / `ResetSoundSettings`
   - `GraphicsSettings` POD + `kpcGraphicsSettingsPath` + `SaveGraphicsSettings` / `LoadGraphicsSettings` / `ResetGraphicsSettings`
   - `TweaksSettings` POD + `kpcTweaksSettingsPath` + `SaveTweaksSettings` / `LoadTweaksSettings`
   - `ClientStateSettings` POD + `kpcClientStatePath` + `SaveClientState` / `LoadClientState` / `CaptureClientStateAndSaveIfChanged`

   These are the four file-scope settings PODs and their static `Save*`/`Load*`/`Reset*` methods
   plus the one non-static `CaptureClientStateAndSaveIfChanged`. They pull in the
   `Ui/GraphicsSettingsWrappersBase.h` / `SoundSettingsWrappersBase.h` / `SunMoonWrappersBase.h` /
   `MiscWrappersBase.h` / `TweaksScreen.h` / `ProfileManager.h` includes that today exist only to
   serve this block — move those includes with it so `Game.cpp` sheds them.

2. **`GameFleet.cpp` — client-only (`#if defined(BT_CLIENT)`), new TU.**
   The fleet navigation/selection block, currently `Game.cpp:146-406`, all inside two adjacent
   `#if defined(BT_CLIENT)` guards:
   - `AutoSelectFirstAliveMember`, `FleetCount`, `FocusedFleetIndex`, `FocusNextFleet`,
     `FocusPrevFleet`, `CanFocusNextFleet`, `CanFocusPrevFleet`, `FocusedFleet`,
     `SelectPlayerInFleet`, `FocusedPlayerInFleetIndex`, `SyncFleets`
   - `GetClientPlayerPosition` (the separate `#if defined(BT_CLIENT)` block at `:386-406`)

   `SyncFleets` is the largest single method (~134 lines) and dominates this group.

3. **`Game.cpp` keeps** the shared core and frame-grid orchestration so the primary TU stays the
   coordinator:
   - Construction/teardown + lifecycle: `Game()`, `~Game()`, `Reset`, `CreateNewFrame`,
     `ChangeFrame`, `ShouldTrapCursor`, `ShouldUseCrosshair`, `ShouldShowInGameUi`,
     `InitFramePostRender`, `BuildMenuIslandPlacement`, the camera-shake constants
   - Shared client-player tracking: `ClientPlayerId`, `IsClientPlayer`, `AddClientPlayer`,
     `RemoveClientPlayer`, `PlayerCount`, `ClientPlayerIndex`, `RestoreReplayMeta`
   - Multi-frame grid: `ComputeActiveSet`, `EnsureNextFrames` (`BT_SERVER`), `BuildFrameInputs`,
     `CreateFrameAtCoord`, `HarvestTransfers`, `ApplyTransferStatusChanges`, free function
     `SpawnTransfer`
   - Input: `ProcessMenuInput`, `ProcessDebugInput`, `GetNextMusicTrack` (`BT_CLIENT`)

   **Optional second-pass seam (only if `Game.cpp` is still over threshold after 1 & 2):** the
   input block — `ProcessMenuInput` (`:899-993`, shared) and `ProcessDebugInput` (`:1325-1482`,
   `if constexpr (kbDebugInput)` with mixed client/server bodies) — split into `GameInput.cpp`
   (shared TU, since `ProcessMenuInput` has no full-file guard). Removing groups 1 & 2 sheds
   ~395 lines, so the primary TU should land near ~1100 lines; measure first and decide whether
   this second cut is needed to clear ~1000.

### vcxproj / filters wiring (the load-bearing constraint)

Per [VisualStudio2026/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md):

- `GameSettings.cpp` and `GameFleet.cpp` are **fully `#if defined(BT_CLIENT)`** → add to the
  **client** project only: `BrokenEngineSandbox.vcxproj` + `BrokenEngineSandbox.vcxproj.filters`
  (filter `Game`, matching the existing `Game.cpp` entry). Do **not** add them to the server
  project, or it compiles empty TUs.
- If the optional `GameInput.cpp` lands, it is **shared** (`ProcessMenuInput` is unguarded) → add
  to **all four** files: both `.vcxproj` and both `.vcxproj.filters`.
- `Game.cpp` is already registered in all four files (`BrokenEngineSandbox.vcxproj:650`,
  `.filters:773`; `BrokenEngineSandboxServer.vcxproj:480`, `.filters:458`) — leave those entries.

## Out of scope

- Any behavioral change. This is a pure definition relocation; the same methods, in the same order
  of effect, on the same `game::Game` class. No signature changes, no member moves, no logic edits.
- `Game.h` — the class declaration, member layout, and inline methods stay put. (Only thing that
  may change: if a settings POD or `kpc*Path` constant currently sits in the `.cpp` is needed by
  the new TU, it travels with its block — none are declared in the header today, so the header
  should not need edits.)
- The `ScopedSuppressAllocationTracking` / `// Heap:` annotations move verbatim with their methods.
- Splitting the frame-grid orchestration (`ComputeActiveSet` etc.) into its own TU — left in the
  primary `Game.cpp` as the coordinator unless a future measurement shows it dominant.
- The optional `GameInput.cpp` cut unless post-split measurement shows the primary TU still over
  threshold.

## Acceptance criteria

- `Game.cpp` and every new TU each under the ~1000-line `/reduce-file` threshold.
- Client (Debug + Release) and server (Release) all build clean; no duplicate-symbol or
  missing-symbol linker errors from the relocation.
- Client-only TUs (`GameSettings.cpp`, `GameFleet.cpp`) appear only in the client project; the
  server project file set is unchanged except for any shared TU.
- No diff in runtime behavior (settings round-trip, fleet focus/selection, save/load) vs. pre-split.

## Notes

Found by the step-9 audit / `code-review` of the main-menu-island-browser change. Mechanical,
compile-checked refactor; the only real risk is the four-file `.vcxproj`/`.filters` wiring and
keeping the client-only TUs out of the server project. Confirm the post-split line count of the
primary `Game.cpp` before deciding whether the optional `GameInput.cpp` cut is needed.
