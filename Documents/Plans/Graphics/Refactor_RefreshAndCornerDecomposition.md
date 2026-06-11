# Refactor: Refresh Ladder + Corner-Unproject Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics` (non-recursive). Two functions are dominated by
copy-paste repetition: the ~173-line settings-poll ladder in `Graphics::Refresh` and four identical
corner-unproject blocks in `CameraBase::CalculateMatricesAndVisibleArea`. Both reduce mechanically with no
behavior change.

## Design

### Engine/Source/Graphics/Graphics.cpp — Graphics::Refresh (:350-523)
- Extract a per-setting poll helper (member template or local lambda) covering the dominant pattern:
  `auto [v, vPrev, bChanged] = gX.Changed<T>(); if (bChanged) { LOG(...); [mDestroyFlags.Set(F);]
  meDestroyType = std::max(Tier, meDestroyType); }` — instances at `:352-357` (multisampling), `:364-369`
  (sample count), `:392-397` (anisotropy), `:399-404` (max anisotropy), `:406-411` (sample shading),
  `:413-418` (min sample shading), `:420-425` (mip lod bias), `:427-432` (wireframe), `:434-438` (debug
  texture, no LOG), `:440-448` (water shape detail), `:450-458` (shadow render multiplier), `:488-496`
  (WaterSkyboxOne multiplier), `:500-508` (terrain elevation multiplier). [~1h]
- Helper contract (behavior-preserving constraints):
  - Every wrapper's `Changed()` must still be called exactly once per `Refresh()` — it mutates the
    wrapper's last-seen state, so no short-circuiting or conditional polling.
  - The existing manager null-gates stay attached to their settings (`gpBufferManager` `:441`,
    `gpTextureManager` `:451,:464,:474,:481,:489`, `gpInstanceManager` `:498`).
  - `common::Wb` float formatting in LOGs stays (allocation-tracker discipline).
- Leave the genuinely special cases as straight-line code: present mode with its enum-string workbuffer LOG
  (`:371-379`), the framebuffer-extent check with its zero-size early return (`:381-390`), the sample-count
  clamp (`:359-362`), the multi-wrapper OR groups (lighting `:460-469`, blur-reblur action `:471-477`,
  object shadows `:479-486`, smoke group with its dual LOG `:510-521`) — group members still poll via the
  helper's no-action form if that falls out naturally, otherwise leave them as-is.

### Engine/Source/Graphics/CameraBase.cpp — CalculateMatricesAndVisibleArea (:53-101)
- Replace the four copy-pasted corner-unproject blocks (top-left `:55-65`, top-right `:67-77`, bottom-left
  `:79-89`, bottom-right `:91-101`) with a loop over a four-entry table of
  `{screen x, screen y, XMFLOAT4* target}` — targets `f4VisibleTopLeft` / `f4VisibleTopRight` /
  `f4VisibleBottomLeft` / `f4VisibleBottomRight`; the unproject-ray + `XMPlaneIntersectLine` body appears
  once. [~30m]

## Critical files
- `Engine/Source/Graphics/Graphics.cpp`
- `Engine/Source/Graphics/CameraBase.cpp`

## Out of scope
- Any change to which destroy tier / `DestroyFlags` a setting maps to, poll ordering side effects, or LOG
  text content.
- Renumbering the `DestroyFlags` bit gaps (`Graphics.h:37-46`, values 0x0001/0x0002/0x0080-0x0200 unused) —
  cosmetic; the flags are runtime-only and the gaps are harmless history.
- `InVisibleArea` / `AabbIntersectsVisibleArea` signatures — `Graphics/Refactor_ApiAndHotPathCleanups.md`.
- The shared-constant items in the same functions (`Graphics/Architecture_SharedConstantDuplication.md`).

## Acceptance criteria
- `Refresh()` and the corner block compile to the same observable behavior: identical destroy-tier
  escalation per setting, every wrapper polled once per call, identical LOG lines; visible-area corners
  bit-identical (same call sequence per corner).

## Notes
- Client-only, render/settings path; no determinism/CRC exposure. The corner loop preserves FP semantics by
  keeping the identical per-corner call sequence — only the source text is deduplicated.
- Co-schedule with the other plans editing `Graphics.cpp` / `CameraBase.cpp` (see Order.md File Groups) so
  line citations stay fresh.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- `Graphics::Refresh` spans `Graphics.cpp:350-523` (173 lines as claimed). Every helper-pattern instance
  re-checked at its cited range: `:352-357`, `:364-369`, `:392-397`, `:399-404`, `:406-411`, `:413-418`,
  `:420-425`, `:427-432`, `:434-438` (no LOG — correct), `:440-448`, `:450-458`, `:488-496`, `:500-508` —
  all exact. Special cases confirmed: present-mode workbuffer LOG `:371-379`, framebuffer-extent
  early-return `:381-390`, sample-count clamp `:359-362`, OR groups at `:460-469`, `:471-477`, `:479-486`,
  smoke group `:510-521` with dual LOG `:516-517`. Null gates exact: `gpBufferManager` `:441`,
  `gpTextureManager` `:451,:464,:474,:481,:489`, `gpInstanceManager` `:498` (encloses both the
  terrain-elevation and smoke blocks).
- Helper contract confirmed against `Wrapper::Changed<T>()` (`Ui/WrapperBase.h:58-64`): it assigns
  `mfPrevious = mfCurrent` on every call — it DOES mutate last-seen state, so the once-per-Refresh /
  no-short-circuit constraint is correct as written (no wording fix needed).
- Corner blocks exact: top-left `:55-65`, top-right `:67-77`, bottom-left `:79-89`, bottom-right `:91-101`;
  targets `f4VisibleTopLeft/TopRight/BottomLeft/BottomRight` are `XMFLOAT4` members (`CameraBase.h:19-22`)
  stored via `XMStoreFloat4`; each block runs the identical two-`XMVector3Unproject` +
  `XMPlaneIntersectLine` sequence, so the table-loop preserves per-corner FP call order as claimed.
- Out-of-scope citations hold: `DestroyFlags` gaps (`Graphics.h:37-46`, missing 0x0001/0x0002/0x0080-0x0200);
  cross-referenced plans exist.
