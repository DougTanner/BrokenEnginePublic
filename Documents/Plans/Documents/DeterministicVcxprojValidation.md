<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-29T18:00:59.823Z","dependsOn":[]} -->
# Make Visual Studio project validation one deterministic command

## Context

`/update-vcxproj` currently asks a mechanic to reconstruct project/filter validation with XML inspection, searches, and ad hoc PowerShell. During the three-GUID repair landed as `4f74a207`, that mechanic used about 3m42s and 1.174M exposed tokens, including retries and broad output, even though it changed no bytes.

The required signal is deterministic: the project and filters XML must parse, item membership must mirror, referenced filters must exist, and filter GUIDs must be valid and unique. A first-party script can provide that signal once without transferring XML bodies into agent context.

## Design

Add `.agents/skills/update-vcxproj/scripts/Test-VcxprojPair.ps1` with required `-ProjectPath <string[]>`. Each input is an absolute or repository-relative first-party `.vcxproj`; the script resolves its adjacent `.vcxproj.filters` without guessing another pair.

Emit one compact `broken-engine-vcxproj-validation/v1` JSON object. Exit `0` means pass, `2` means deterministic validation failure, and `1` means invalid input or an internal/read failure.

For each pair:

- parse both files using the MSBuild XML namespace;
- compare `ClCompile`, `ClInclude`, and `None` by item type and ordinal `Include` identity;
- reject missing, extra, or duplicate project/filter entries;
- require each filters-file item to carry one filter assignment;
- require every referenced filter and each path ancestor to have exactly one declaration;
- require filter declarations to be unique;
- require exactly one brace-wrapped GUID `UniqueIdentifier` per declaration;
- require GUID uniqueness case-insensitively.

Declared but currently empty filters are valid; the client and server files intentionally contain them. Success output contains pair paths and bounded counts. Failure output contains sorted structured violations with a stable code and the decisive item/filter/GUID identity, never the XML body.

Update `/update-vcxproj` so the mechanic retains responsibility for affinity/membership judgment and authorized edits, then invokes this script once per affected pair. Do not recreate its structural checks manually after a passing result.

## Critical files

- `.agents/skills/update-vcxproj/scripts/Test-VcxprojPair.ps1` — validator and typed result.
- `.agents/skills/update-vcxproj/SKILL.md` — route structural verification through the script.
- The five first-party Visual Studio project/filter pairs — positive acceptance inputs, not expected edits.

## Out of scope

- Automatically editing project membership, filters, or GUIDs.
- Deciding client/server/DataPacker/AgentTools affinity.
- Rejecting intentionally empty filter declarations or reformatting XML.
- ThirdParty projects and new unit-test projects.

## Risk tier and invariants

**Tier 2 — scoped tool behavior.** The script becomes the observable verifier used by project-membership hygiene. It must preserve all current structural signals, remain read-only, and return bounded deterministic results. No build output or engine runtime invariant is exposed.

## Acceptance criteria

- One invocation over the five first-party pairs passes: DataPacker, BrokenEngineSandbox client, BrokenEngineSandbox server, AgentHarness, and WorktreeCli.
- Scratch copies fail with stable violations for malformed XML, a missing mirror item, a duplicate item, a missing filter/ancestor declaration, a malformed or missing GUID, and a case-insensitive duplicate GUID.
- The `4f74a207` client pair passes in one invocation and reports globally unique filter identifiers.
- Success stdout contains counts and identities only; it contains no XML body.
- `/update-vcxproj` no longer prescribes the equivalent manual XML/search sequence.
- The changed skill passes `/validate-skill`. No unit-test project is added.

## Notes

This Plan centralizes existing checks; it does not weaken or broaden project hygiene. `Documents/Plans/Documents/TierOneNextPlanFastPath.md` depends on this deterministic signal so a mechanical project edit does not require a separate exploratory mechanic pass.
