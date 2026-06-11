# vcxproj / Filters / Build-Config Reconciliation

## Context

The CLAUDE.md refresh surfaced several project-file and build-configuration inconsistencies. None changes
runtime behavior; each is a build-graph or IDE-organization fix. Grouped because they are the same kind of work
(`.vcxproj` / `.vcxproj.filters` / include-path hygiene) and several require a "categorize vs register vs
remove" decision rather than a mechanical edit.

### Items

1. **Game Debug include-path asymmetry** (build-config finding 17): both game vcxprojs' **Debug**
   `AdditionalIncludeDirectories` duplicate `..\..\Data` and **omit** the `..\..\..\Data` entry that the
   **Profile/Release** configs have. Likely unintended drift between configs. Decide: add the missing
   `..\..\..\Data` to Debug (match Profile/Release) and/or drop the duplicate `..\..\Data` — confirm which entry
   is the correct one by checking what resolves the game `Data/` shader include path (the game PCH adds the game
   Data dir to include paths; the report's game `Data/Shaders` note documents this resolution). Apply to both
   the client and server game vcxproj Debug configs.
2. **ThirdParty Prebuilts unused include path** (build-config finding 18): the Prebuilts vcxproj's
   `..\..\..\..\Common` include path appears unused by any wrapper (legacy candidate). Confirm no Prebuilts
   wrapper includes a `Common/` header, then remove the entry.
3. **StackWalker orphan / miscategorization** (conflict 25): `ThirdParty/CLAUDE.md` inventories StackWalker
   under "Engine / runtime", but the only **compiled** unit is `Source/DataPacker/StackWalker.cpp`;
   `Prebuilts/Source/Engine/StackWalker.cpp` is an **orphan registered in no vcxproj**. Decide one of: (a)
   delete the orphan `Engine/StackWalker.cpp` if the DataPacker copy is authoritative, (b) register the Engine
   copy if the engine is meant to compile its own, or (c) at minimum re-categorize the ThirdParty doc to match
   where it actually compiles. (The doc note belongs to the ThirdParty CLAUDE.md, but the *registration/deletion*
   decision is a code/project change — this plan owns that decision; the doc follows.)
4. **`ThirdParty.vcxproj.filters` line 123: `cmft.cpp` has no `<Filter>` element** (conflict 26) — it renders at
   the project root instead of under a by-consumer filter, contradicting the file's organization. Add the
   appropriate `<Filter>` (DataPacker/by-consumer) element so it nests correctly. Pure IDE-view fix.
5. **`HexShieldWrappers.cpp` vestigial in the server vcxproj** (conflict 27): `BrokenEngineSandboxServer.vcxproj`
   includes `HexShieldWrappers.cpp`, but no server-compiled code references any `HexShieldWrappers` global
   (grep-verified; readers are client-only `MainUniforms.cpp` / `TweaksScreenHexShield.cpp`). Decide: remove the
   entry from the server vcxproj/filters if the TU is effectively empty/unreferenced server-side — **but first
   confirm** the file is not fully `#if defined(BT_CLIENT)`-wrapped (if it is, it must not be in the server
   project at all, per the VisualStudio2026 client/server rule; if it compiles in both but is server-unused, it
   is harmless dead weight). The grill resolves whether to remove it.
6. **`RawInputManager.h` missing from the server vcxproj `ClInclude`** (Input/Memory deep-analysis run): the
   header is partial-guard — the `RawInput` struct + button enums (`RawInputManager.h:6-48`) compile in both
   builds and game `Input.h:3` includes it in the server build — and `VisualStudio2026/CLAUDE.md` says headers
   follow the same affinity (partial-guard → both projects), but it is listed only in the client vcxproj
   (`BrokenEngineSandbox.vcxproj:399`). The server filters' existing empty `Engine\Input` filter is *not*
   supporting evidence — `VisualStudio2026/CLAUDE.md:44` documents it as leftover cruft; the justification is
   the affinity rule plus the header actually compiling into the server build. Add the `ClInclude` (+ matching
   `.filters` entry, which makes that filter non-empty — the CLAUDE.md:44 leftover-filters sentence then needs
   its `Engine\Input` mention dropped via the standard doc step) to `BrokenEngineSandboxServer.vcxproj`. Pure
   IDE-view fix, no build effect.

## Design

Treat each as an independent project-file edit. For the **decision** items (3 StackWalker, 5 HexShieldWrappers)
resolve intent in the grill before editing; for the **mechanical** items (1, 2, 4) verify the current entries,
then edit.

- Edit `.vcxproj` and the matching `.vcxproj.filters` together (an entry in one must mirror the other).
- For include-path changes (1, 2), do a clean rebuild of the affected config to confirm nothing relied on the
  removed/duplicated path.
- For StackWalker (3), the cleanest outcome is usually deleting the unregistered orphan and pointing the
  ThirdParty doc at the DataPacker copy — but confirm the engine doesn't *intend* to compile its own (the
  report says the engine/runtime categorization is the doc's, not the build's).
- Keep each edit minimal; do not reorganize unrelated filter entries.

## Out of scope

- Adding/removing actual **source files** beyond the orphan StackWalker decision — no new TUs.
- The `IslandTerrainResidency.cpp` / `NavBuildSplit` file-wiring — owned by their own split plans.
- Runtime behavior — every item is build-graph/IDE-view only; no `#ifdef`/code change (except confirming a
  file's client/server guard state to decide vcxproj membership).
- The `ThirdParty/CLAUDE.md` and `VisualStudio2026/CLAUDE.md` doc wording — those follow via the standard
  update-claude-docs step once the project files are settled; the *categorization decision* is here, the doc
  edit is not.
- The shader-source `<None>` / server-omits-Shader.h conventions already documented — unchanged unless an item
  above touches them.

## Acceptance criteria

- Game Debug include paths match the Profile/Release convention (correct `Data/` entry present, duplicate
  resolved); both game configs build clean (1).
- The unused Prebuilts `Common` include path is removed and Prebuilts still builds (2).
- StackWalker has exactly one authoritative compiled location, and the orphan is either registered or deleted
  (no TU registered in zero projects); categorization is consistent (3).
- `cmft.cpp` nests under a proper filter in the IDE view (4).
- `HexShieldWrappers.cpp`'s server-vcxproj membership is resolved consistently with the client/server-guard rule
  (removed if vestigial, or justified if it must compile in both) (5).
- `RawInputManager.h` appears in both projects' `ClInclude` lists with matching `.filters` entries (6).

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and
  `BrokenEngineSandboxServer.vcxproj` (+ their `.filters`) — Debug include paths (1), `HexShieldWrappers.cpp`
  server entry (5), `RawInputManager.h` server `ClInclude` (6).
- `ThirdParty/Prebuilts/Platforms/VisualStudio2026/*.vcxproj` — `..\..\..\..\Common` include path (2).
- `ThirdParty/Platforms/VisualStudio2026/ThirdParty.vcxproj` and `ThirdParty.vcxproj.filters` (`:123`
  `cmft.cpp` filter) — (4).
- `ThirdParty/Source/DataPacker/StackWalker.cpp` (compiled) and `ThirdParty/Prebuilts/Source/Engine/
  StackWalker.cpp` (orphan) — (3).
- `ThirdParty/CLAUDE.md`, `Projects/.../Platforms/VisualStudio2026/CLAUDE.md` — read-only reference for the
  conventions; doc edits follow the standard step, not this plan.

## Notes

- Build-graph / IDE-view only — no runtime, CRC, or determinism exposure (Risks ~0-1; the include-path changes
  carry a small "did anything rely on it" check, hence not 0).
- Two decision items (StackWalker, HexShieldWrappers) need a grep + intent confirmation; the other three are
  mechanical once verified.
- Coordinate with `Frame/IslandTerrainSplit.md` / `Frame/NavBuildSplit.md` only insofar as all touch vcxproj
  files — but they edit *different* entries (new file additions vs these reconciliations), so no hard ordering
  constraint; just avoid stomping each other's edits if co-scheduled.
