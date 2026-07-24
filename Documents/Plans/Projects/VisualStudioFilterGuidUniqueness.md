<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T16:15:04.325Z","dependsOn":[]} -->
# Restore Client Visual Studio Filter GUID Uniqueness

## Context

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` assigns the same `UniqueIdentifier` to multiple `<Filter>` elements. Exactly two collision groups exist in the current file, and no other identifier repeats:

- `{a1b2c3d4-e5f6-7890-abcd-ef1234567890}` — used by `<Filter Include="Engine\Graphics\Render">`, `<Filter Include="Engine\Memory">`, and `<Filter Include="Engine\Ui\Screens\TweaksScreen">`.
- `{b2c3d4e5-f6a7-8901-bcde-f12345678901}` — used by `<Filter Include="Engine\Graphics\Debug">` and `<Filter Include="Engine\Data\Shaders\Wind">`.

The `/update-vcxproj` hygiene pass and its fresh non-C++ review found the violation while verifying unrelated NetworkMessages membership. The duplicates predate this plan (present at baseline `19fdfc63203e1c3af60f28ff6630876bb5555486`) and were outside that session's acceptance boundary. `BrokenEngineSandboxServer.vcxproj.filters` contains no duplicate identifiers and needs no change.

## Design

Replace the `UniqueIdentifier` value of exactly three `<Filter>` elements, retaining the first occurrence of each colliding identifier in file order:

1. `<Filter Include="Engine\Graphics\Render">` — retain `{a1b2c3d4-e5f6-7890-abcd-ef1234567890}` unchanged.
2. `<Filter Include="Engine\Memory">` — replace its `UniqueIdentifier` with a freshly generated GUID.
3. `<Filter Include="Engine\Ui\Screens\TweaksScreen">` — replace its `UniqueIdentifier` with a freshly generated GUID.
4. `<Filter Include="Engine\Graphics\Debug">` — retain `{b2c3d4e5-f6a7-8901-bcde-f12345678901}` unchanged.
5. `<Filter Include="Engine\Data\Shaders\Wind">` — replace its `UniqueIdentifier` with a freshly generated GUID.

Each new value is a valid GUID in the existing `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` brace-wrapped lowercase format, distinct from the other new values and from every identifier already in the file, compared case-insensitively. Generate them with any standard GUID generator (e.g. `[guid]::NewGuid()` in PowerShell); the specific values are a trivial local detail.

The edit changes only the text between `<UniqueIdentifier>` and `</UniqueIdentifier>` inside the three named `<Filter>` elements. Every `Filter Include` name, every `<ClCompile>`/`<ClInclude>`/`<None>` item's `<Filter>` membership, all other filter metadata, and the paired `BrokenEngineSandbox.vcxproj` are untouched. This is IDE-view-only filter identity; client/server source membership does not change.

Risk tier: **Tier 1 — mechanical** project metadata. It exposes no runtime, build-output, determinism, wire, serialization, or threading invariant.

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` — client Visual Studio `<Filter>` identities; the only file modified.

## Scope contract

This listed scope is both target and ceiling: make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

**In scope** (the only permitted edits):

- In `BrokenEngineSandbox.vcxproj.filters`, the `<UniqueIdentifier>` element text of exactly these three `<Filter>` elements: `Engine\Memory`, `Engine\Ui\Screens\TweaksScreen`, `Engine\Data\Shaders\Wind`.

**Out of scope** (naming the file above grants no permission beyond the three elements listed):

- Every other `<Filter>` element and `<UniqueIdentifier>` in `BrokenEngineSandbox.vcxproj.filters`, including the two retained identifiers, and all item-group `<Filter>` membership entries in that file.
- `BrokenEngineSandboxServer.vcxproj.filters` (currently duplicate-free) and `BrokenEngineSandboxServer.vcxproj`.
- `BrokenEngineSandbox.vcxproj` compilation entries, source code, project structure, and any non-identity filter metadata.
- Any in-flight NetworkMessages / wire-format source or project-membership work; unrelated source-membership entries already present at implementation time must be preserved as-is.

## Acceptance criteria

- XML inspection shows every `<Filter>` `UniqueIdentifier` in `BrokenEngineSandbox.vcxproj.filters` occurring exactly once, compared case-insensitively.
- A pre/post diff of the file shows changes only on the three `<UniqueIdentifier>` lines of the elements named in scope; `Filter Include` values and all source-item `<Filter>` assignments are byte-identical.
- `/update-vcxproj` validates the client project/filter pair after the edit (XML validity, mirrored filters, unique filter GUIDs).

## Notes

- No dependencies or coordination entries: this is an independently landable, IDE-view-only repair.
