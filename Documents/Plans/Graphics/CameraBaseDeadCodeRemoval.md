<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:06:07.964Z","dependsOn":[]} -->
# Delete CameraBase dead code: ScreenToWorld and the unused f4RawAreaIn local

## Context

Two dead-code findings in `Engine/Source/Graphics/CameraBase.cpp`, both verified during an `/external-deep-analysis` run at baseline `88ef0ddb` (Phase-3 reviewer verdict CONFIRM on each) and pre-existing — the file was untouched by that analysis session:

- `CameraBase::ScreenToWorld` (definition `Engine/Source/Graphics/CameraBase.cpp:8-22`, declaration `Engine/Source/Graphics/CameraBase.h:45`) has zero callers repository-wide, including tests; history shows no former caller. It also duplicates the unproject-plus-plane-intersect pattern that `CalculateMatricesAndVisibleArea`'s corner loop implements independently, so deleting it retires the only other copy of that pattern.
- The local `XMFLOAT4 f4RawAreaIn = f4RenderVisibleArea;` at `Engine/Source/Graphics/CameraBase.cpp:113` is written and never read; that line is its sole occurrence in the repository.

The comment on `WorldToScreen` (`Engine/Source/Graphics/CameraBase.cpp:26-27`) explains the viewport/Y-sign convention by naming `ScreenToWorld` as its exact inverse; deleting `ScreenToWorld` orphans that phrasing.

## Design

Delete both dead items and repair the one comment that names the deleted method. Deleting is preferred over retention: the repository directive is to remove obsolete code rather than keep an unused mechanism alive (YAGNI), and neither item has a reader to preserve behavior for, so both removals are behavior-preserving.

Concretely: remove the `ScreenToWorld` definition and declaration; rewrite the `WorldToScreen` comment so it still states the viewport convention and that X/Y are screen pixels with projected depth in Z, without naming `ScreenToWorld`; delete the `f4RawAreaIn` declaration line. Do not touch any DirectXMath call, operand order, or W-component usage in the surviving functions — `WorldToScreen` and `CalculateMatricesAndVisibleArea` must produce bit-identical results.

## Critical files

- `Engine/Source/Graphics/CameraBase.cpp`
- `Engine/Source/Graphics/CameraBase.h`

## In scope

- The `ScreenToWorld` definition (`CameraBase.cpp:8-22`) and declaration (`CameraBase.h:45`)
- The `WorldToScreen` block comment (`CameraBase.cpp:26-27`)
- The `f4RawAreaIn` declaration (`CameraBase.cpp:113`)

## Out of scope

- Every surviving statement of `WorldToScreen` and `CalculateMatricesAndVisibleArea`, including operand order and DirectXMath call form
- The visible-area snap, LOD, and hysteresis logic and its comments (`CameraBase.cpp:105-174`)
- `CameraBase.h` members and the `InVisibleArea` helpers
- Decomposition of `CalculateMatricesAndVisibleArea` (reviewed and rejected as not warranted)

## Risk tier and invariants

Expected Change Workflow Tier 2 — a scoped behavior surface: removing the public `ScreenToWorld` declaration is a public-signature change even though the search found zero callers, and the deletion plus comment repair is otherwise behavior-preserving. A caller this Plan's search missed would make the deletion itself invalid, not merely re-tier it. The file is whole-file `BT_CLIENT`-guarded client render state, outside deterministic PostRender/CRC state.

## Acceptance criteria

- No occurrence of `ScreenToWorld` or `f4RawAreaIn` remains in the repository
- Client compiles (the file is client-only; the server does not compile it)
- The `WorldToScreen` comment still records the viewport/Y-sign convention without naming a deleted symbol
