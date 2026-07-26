# TweaksScreen - Runtime Parameter UI Base

Client-only, debug-input-gated ImGui screen for engine and game wrapper settings. It persists section visibility, positions, collapse state, and active subtabs; the game layer owns disk I/O.

## Registration and Layout Contracts

- Sections register once at startup — engine sections first, then game — before the graphics device builds the ImGui screen and before persisted settings load. Registration assigns the dense index identifying a section's window, its bit in the visibility and collapse masks, and its persisted layout slot; the 32-bit mask backing caps the section count. Registration stays out of the screen constructor because device loss reconstructs that object in place.
- The registry is written only during that startup pass and is immutable while rendering, so render dispatch and label lookup need no synchronization. A section's display label is separate from its persisted stable key, so relabeling a section never discards saved layout.
- Section layout persists as an engine-owned POD embedded by value in the game settings struct, gated by a CRC over the registered stable keys in order. A mismatch discards the saved layout and keeps constructor defaults, so adding, removing, or reordering sections resets window layout instead of misapplying it.
- Whole game-owned sections enter through registration; sub-tabs inside an engine section use the base extension hooks.
- Each slider registrar maps static, globally unique keys to wrappers. Map storage is program-lifetime and allocation suppression is required during its construction. A display label may differ from its key to avoid ImGui ID collisions.
- Wrapper declaration order and each section's slider order stay aligned. The debug audit runs once per TweaksScreen lifetime, is re-armed by graphics reconstruction, visits every subtab, and reports missing or orphaned registrations.
- Tabbed sections persist their active tab. Loading force-selects the saved tab for one frame, after which normal rendering updates the stored selection.
- A render path uses at most one ImGui table. When a game hook owns a table, its engine parent renders outside one. Sliders inside multi-column tables use the cell-width multiplier rather than the full-window default.
- While dragging, only the owning section remains visible; the toggle bar and saved window layouts stay stable.

## See Also

- Game screens (`../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md`) - Screen conventions
- Game Tweaks implementation (`../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/`) - Extension hooks
