# Refactor: Input Button Flags

## Context
Source: /external-refactor-clean on `Engine/Source/Input`. `RawInput` carries two parallel bool arrays for buttons (`pMouseButtons[5]`, `pGamepadButtons[8]`) where the root-CLAUDE.md "Flags over booleans" pattern calls for `common::Flags<EnumType>` — the same shape already used by `MenuInputFlags_t` one layer up (`Input.h:38`). Conversion shrinks the snapshot, makes the per-frame copy and the game-side previous-snapshot edge detection trivial bitwise ops, and aligns the two input layers on one idiom. The keyboard array stays: 255 VK codes indexed by OS-defined values are not a Flags candidate.

## Design

### Engine/Source/Input/RawInputManager.h
- Redefine `MouseButtons` (lines 8-17) and `GamepadButtons` (lines 19-33) as bitmask enums (`: uint32_t`, `common::Flags` requires an unsigned underlying type; values `1 << n`). Drop or repurpose the `kMouseButtonCount`/`kGamepadButtonCount` sentinels (no longer array sizes). [~10m]
- In `RawInput` (lines 35-48), replace `bool pMouseButtons[kMouseButtonCount]` (line 39) and `bool pGamepadButtons[kGamepadButtonCount]` (line 43) with `common::Flags<MouseButtons>` / `common::Flags<GamepadButtons>` members. [~5m]

### Engine/Source/Input/RawInputManager.cpp
- `RawInputManager::Update`: convert the five mouse-button writes (lines 152-156) and the eight gamepad-button writes (lines 183-190) to `Flags::Set(flag, state)`; the disconnect clear block (lines 205-212) becomes a single `= {}` reset. [~10m]

### Projects/BrokenEngineSandbox/Source/Input/Input.h
- Convert the edge-detect helpers `MousePressed`/`GamepadPressed` (lines 93-94) from array indexing to flag tests against `mPreviousRawInputMenu` (current set AND NOT previous). Signatures take the enum, not `int64_t`. [~10m]

### Projects/BrokenEngineSandbox/Source/Input/Input.cpp
- Update the direct reads: KBM-mode whitelist checks (line 19), `kMouseIsDown` (line 40), and the ImGui gamepad feed (lines 94-96). All call sites are compile-checked — the member names change. [~10m]

## Critical files
- `Engine/Source/Input/RawInputManager.h`
- `Engine/Source/Input/RawInputManager.cpp`
- `Projects/BrokenEngineSandbox/Source/Input/Input.h`
- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp`

## Out of scope
- `pKeyboardKeys` (stays a bool array — VK-code indexed).
- `mbGamePadConnected`/`mbHasFocus` manager members — two bools, Flags would be over-engineering (KISS).
- `MenuInput`/`CameraInput` — already idiomatic.

## Acceptance criteria
- Both client and server projects compile (`RawInput` is compiled into both builds via game `Input.h:84`'s unguarded member).
- Edge detection behavior unchanged: a button held across two frames reports pressed exactly once.

## Notes
- No determinism/CRC/serialization exposure: `RawInput` is never serialized (only `FrameInput`/`StatusChange` is) and never feeds the sim directly — display-rate menu/camera input only.
- Touches the same files as the other Input plans — see File Groups; co-schedule or refresh lines.

## Verification Notes
- `common::Flags` API verified (`Common/Flags.h`): unsigned underlying type enforced by `static_assert` (`:10`) — the planned `: uint32_t` is mandatory since the current enums have no fixed underlying type; `Set(flag, bool)` (`:39`), single-flag test `operator&(ENUM_TYPE)` (`:73`), and `= {}` reset (default ctor + copy assign) all exist. Edge detect is per-flag — `(current & flag) && !(previous & flag)` — no whole-set AND-NOT operator needed (none exists).
- Consumer list verified complete by repo-wide grep: outside the manager, only `Input.h:93-94` and `Input.cpp:19/40/94-96` read the members directly; the helper call sites (`Input.cpp:41/42/75`) already pass enum values and survive the signature change unmodified.
- `RawInput` is never serialized (no stream `Read`/`Write` anywhere in `Engine/Source/Input`; only `FrameInput` has stream operators), and `kMouseButtonCount`/`kGamepadButtonCount` have no users beyond the two array sizes.
- `Engine/DeadCodeAndUnusedIncludesSweep.md` also edits game `Input.h` (deletes `GetGamepadMode()` and `FrameInput::operator==`) — disjoint lines, co-schedule or refresh.
