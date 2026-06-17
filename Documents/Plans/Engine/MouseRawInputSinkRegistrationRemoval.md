# Mouse RIDEV_INPUTSINK Registration Removal (behavior-verify)

## Context

Split out of `Engine/DeadCodeAndUnusedIncludesSweep.md` (item 8) during its `/next-plan` grill — it
was the one behavior-adjacent item in an otherwise mechanical dead-code sweep, so it was deferred to its
own playtest-gated plan rather than folded in.

`RawInputManager.cpp:43` registers the **mouse** for raw input with `dwFlags = RIDEV_INPUTSINK`. But
`HandleRawInput` (`RawInputManager.cpp:217`) handles only `RIM_TYPEKEYBOARD` (`:231`) — there is no
`RIM_TYPEMOUSE` branch, so every mouse raw-input packet that reaches the handler is discarded. The mouse
is driven by DirectXTK Mouse via legacy WndProc messages, not `WM_INPUT`. `Input/CLAUDE.md` corroborates:
"mouse raw-input packets reaching `HandleRawInput` are discarded." The mouse RAWINPUTDEVICE registration
therefore appears vestigial.

## Design

1. Confirm (grep + read) that no code path consumes raw **mouse** packets: `HandleRawInput` has no
   `RIM_TYPEMOUSE` branch, and nothing else reads the mouse `RAWINPUT` buffer.
2. Remove only the **mouse** `RAWINPUTDEVICE` entry + its `RIDEV_INPUTSINK` flag from the registration
   array (`RawInputManager.cpp:43` region); keep the keyboard (and any other) registrations untouched.
   Adjust the registered-device count accordingly.
3. **Playtest-verify** (cannot be done by an agent): after the build, confirm mouse input still works in
   the running client — camera pan/zoom, UI clicks, drag-select — both foregrounded and (if relevant)
   when focus changes. `RIDEV_INPUTSINK` only affects background delivery; removing the mouse
   registration should not change legacy `WM_MOUSE*` delivery (that would require `RIDEV_NOLEGACY`, which
   is not set), so DirectXTK Mouse should be unaffected — but verify rather than assume.

If verification is inconclusive or anything regresses, restore the registration and instead add a code
comment documenting why it is retained (the "leave it, document why" fallback).

## Out of scope

- Keyboard / gamepad raw-input registration — only the mouse entry is touched.
- Any change to `HandleRawInput`'s keyboard handling.
- The rest of the dead-code sweep (landed separately).

## Acceptance criteria

- Mouse registration removed (or, if verification fails, retained with an explanatory comment).
- Client build clean; mouse input playtested working in the running client.

## Critical files

- `Engine/Source/Input/RawInputManager.cpp` (`:43` mouse `RIDEV_INPUTSINK` registration; `:217`/`:231`
  `HandleRawInput` keyboard-only handling — read-only reference).

## Notes

- Client-only (Input is client-only). No CRC/determinism/network/`kiVersion` exposure.
- Behavior-adjacent: the registration changes what the OS delivers, so this is **playtest-gated** —
  that is the entire reason it was split out of the mechanical sweep.
