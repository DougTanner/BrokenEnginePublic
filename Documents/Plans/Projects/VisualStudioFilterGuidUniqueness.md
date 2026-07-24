<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T16:15:04.325Z","dependsOn":[]} -->
# Restore Client Visual Studio Filter GUID Uniqueness

## Context

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` assigns the same `UniqueIdentifier` to multiple `Filter` elements. The two collisions are `{a1b2c3d4-e5f6-7890-abcd-ef1234567890}` for `Engine\Graphics\Render`, `Engine\Memory`, and `Engine\Ui\Screens\TweaksScreen`, and `{b2c3d4e5-f6a7-8901-bcde-f12345678901}` for `Engine\Graphics\Debug` and `Engine\Data\Shaders\Wind`.

The `/update-vcxproj` hygiene pass and its fresh non-C++ review found the violation while verifying unrelated NetworkMessages membership. The duplicates are present at baseline `19fdfc63203e1c3af60f28ff6630876bb5555486`; they are outside the active `Documents/Plans/Network/Architecture_WireFormatPairing.md` acceptance boundary. The server filters contain no duplicate identifiers.

## Design

- Retain one existing identity in each collision group and replace only the other three client `Filter` `UniqueIdentifier` values with valid GUIDs that are unique case-insensitively across the file.
- Preserve every `Filter Include` name, every source item's `Filter` membership, and the paired `.vcxproj` build metadata. Do not change client/server source membership while correcting IDE-only filter identity.
- Treat this as Change Workflow Tier 1: mechanical project metadata. It exposes no runtime, build-output, determinism, wire, serialization, or threading invariant.

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` — client Visual Studio `Filter` identities.

## Out of scope

- The active wire-format pairing change and its NetworkMessages source/project membership.
- `BrokenEngineSandboxServer.vcxproj.filters`, which currently has unique identifiers.
- `.vcxproj` compilation entries, source code, project structure, and non-identity filter metadata.

## Acceptance criteria

- XML inspection shows exactly one case-insensitive occurrence of every `Filter` `UniqueIdentifier` in `BrokenEngineSandbox.vcxproj.filters`.
- Comparing the pre- and post-change file confirms that `Filter Include` values and source-item `Filter` assignments are unchanged; only the three non-retained duplicate identity values differ.
- `/update-vcxproj` validates the client project/filter pair after the edit.

## Notes

- No dependencies or Coordination entries: this is an independently landable, IDE-view-only repair. It must preserve any unrelated source-membership additions already present when implemented.
