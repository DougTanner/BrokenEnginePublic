# Architecture: CurveData Dead Change-Detection Machinery

## Context

Source: /external-architecture-review on `Engine/Source/Ui` (non-recursive). `CurveData` carries a
`Wrapper`-style single-consumer change-detection sub-interface — `Changed()` (`CurveData.h:40-48`),
`ComputeHash()` (`:201-209`), and the tracking members `mPreviousSize`/`mfPreviousHash` (`:214-215`) — with
**zero callers repo-wide**. The sole runtime consumer (`Graphics/Render/LightingUniforms.cpp:40-47`) bakes
`Evaluate()` samples into the uniform every frame without polling `Changed()`, and the TweaksScreen never
polls it either. `Engine/Source/Ui/CLAUDE.md` (Key Systems, CurveData bullet) documents the contract as if
live ("Change detection mirrors `Wrapper`'s single-consumer contract"). Interface complexity not pulling its
weight — and the float-accumulator hash is weak/commutative-ish, so if it ever silently gained a consumer it
could miss edits.

## Design

### Engine/Source/Ui/CurveData.h
- Delete `CurveData::Changed()` (`:40-48`), `CurveData::ComputeHash()` (`:201-209`), the members
  `mPreviousSize` / `mfPreviousHash` (`:214-215`), **and the ctor initialization of both members**
  (`:27-28` — `mPreviousSize = mPoints.size(); mfPreviousHash = ComputeHash();`), which are the only other
  references repo-wide. YAGNI: per-frame re-evaluation is the established consumption model; wire-up would
  add a consumer contract nothing needs. [~10m]

### Engine/Source/Ui/CLAUDE.md
- Drop the change-detection sentence from the **CurveData / CurveWidget** bullet so the doc matches the
  remaining interface (Fritsch-Carlson evaluation, 16-point cap, endpoint X-lock, shared drag static). [~5m]

## Critical files
- `Engine/Source/Ui/CurveData.h`
- `Engine/Source/Ui/CLAUDE.md`

## Out of scope
- `CurveData::Evaluate()` and the Fritsch-Carlson math — load-bearing for `LightingUniforms.cpp:47`; untouched.
- `CurveWidget` — separately owned by `Ui/Architecture_LibraryReplacement.md` (ImPlot rebuild); no overlap
  with the deleted members (the widget mutates `mPoints` via the public interface, not the hash state).
- Adding a *working* change-detection path (e.g. for a future bake-once consumer) — build it when a consumer
  exists.

## Acceptance criteria
- Repo-wide grep for `ComputeHash`, `mPreviousSize`, `mfPreviousHash`, and `.Changed()` on `CurveData`
  instances confirms zero references before deletion; client builds clean (`CurveData.h` is
  `BT_CLIENT`-wrapped, so the server build is untouched by construction).

## Notes
- Client-only header (`CurveData.h:3/220` guard); no determinism/CRC, `kiVersion`, replay, or network
  exposure. Dead-code removal, compile-checked. No grill decisions.

## Verification Notes (2026-06-10)
- Re-derived the zero-callers claim by repo-wide grep: `ComputeHash` / `mPreviousSize` / `mfPreviousHash`
  appear only inside `CurveData.h` itself (ctor `:27-28`, `Changed()` body `:42-46`, definitions
  `:201-209/:214-215`). No `.Changed()` call on any `CurveData` instance exists anywhere — the only
  `Changed()` callers repo-wide are `Wrapper::Changed<T>()` consumers.
- Confirmed the sole runtime consumer `Graphics/Render/LightingUniforms.cpp:40-48` re-bakes
  `Evaluate()` samples every frame with no `Changed()` poll; `TweaksScreenLighting.cpp` mutates curves via
  `CurveWidget` only. The `Engine/Source/Ui/CLAUDE.md` CurveData bullet does document the change-detection
  contract as live — the doc edit in Design is required.
- Rewrite during verification: added ctor lines `:27-28` to the deletion list (the original Design omitted
  them; leaving them would not compile after the member deletion).
- No overlap with `Ui/Architecture_LibraryReplacement.md` (CurveWidget never touches the hash state) or
  `Ui/Refactor_StyleMechanics.md` (dead `iCount` at `:151/:171` — different lines in the same file;
  co-schedule so line citations stay fresh).
