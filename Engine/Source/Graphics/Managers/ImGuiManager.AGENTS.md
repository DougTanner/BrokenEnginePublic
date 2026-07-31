# ImGuiManager

Global: `gpImGuiManager`

Integrates Dear ImGui with a dedicated load-preserving overlay pass. It records the UI command buffer each frame, waits for Main, signals presentation, and signals the per-framebuffer fence as the frame's final graphics submission.

## UI Ownership and Layout

The manager deliberately owns engine and game screen objects, the sanctioned engine-to-game dependency exception described by the Engine hub. Screen behavior belongs in UI documentation (`../../Ui/Screens/AGENTS.md`).

Theme geometry resets to ImGui defaults before scaling so it can be reapplied safely. UI dimensions use a 2160-pixel reference height through `UiScale()`; font scale composes framebuffer and user scaling. That reference height is a repository-wide convention rather than a manager detail: every authored pixel constant in engine and game UI code is written for a 2160-high screen and multiplied by `UiScale()` at use. The authoring rules belong to the game UI hub (`../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md`). Opaque-region registration is rectangular, so rounded opaque windows must remain visually compatible with rectangular scene occlusion.

## Input and Assets

The Win32 backend publishes physical cursor state during frame preparation. Synthetic agent mouse position is reissued afterward so it wins last-writer ordering. Physical-input suppression parks the cursor unless an agent pin owns it and always clears backend gamepad navigation state.

The single default font is loaded from a pack chunk. Validate its declared byte range before passing the resident data to ImGui.
