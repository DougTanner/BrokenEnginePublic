<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: Include Dependencies

## Context

Source: /external-architecture-review on `Tools/` recursively. The AgentTools (WorktreeCli, AgentHarness, shared ToolCommon sources) build without a precompiled header, and `Tools/ToolCommon/AGENTS.md` requires shared standard-library and third-party consumption headers to be centralized in `Tools/ToolCommon/ToolCliCommon.h`. The review found one verified unused include and declarations that compile only through incidental transitive standard-library headers (via `CoordinationStore.h` and `tinygltf/json.hpp`). This plan removes the unused include and makes the transitive dependencies direct, with no behavior change.

## Scope contract

The listed scope is both target and ceiling. The implementer makes the smallest complete change and adds no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (include lines) the named change requires.

**In scope** — only the include blocks of the four files below, exactly as specified in Design:

- `Tools/WorktreeCli/LandingLockCommands.h` — the `#include <string>` line only.
- `Tools/WorktreeCli/LandingLockLifecycle.h` — the include block only (currently the single `#include "CoordinationStore.h"` line).
- `Tools/WorktreeCli/PlanScheduler.cpp` — the include block only (verification step; see Design).
- `Tools/ToolCommon/ToolCliCommon.h` — the standard-library include block only.

**Out of scope**

- Redesigning the intentional `ToolCliCommon.h` aggregation hub.
- Moving repository-specific helpers or changing project membership.
- Formatting or naming cleanup unrelated to include ownership.
- Any change to declarations, definitions, function bodies, or namespaces in the named files.
- Include changes in any file not listed above (including the translation units that merely benefit from the `ToolCliCommon.h` addition).

## Design

### 1. `Tools/WorktreeCli/LandingLockCommands.h` — remove unused include [~5m]

Remove `#include <string>` (line 3). The header's only declaration, `int RunLandingLockCommand(int iArgumentCount, wchar_t* pArgumentValues[])`, uses built-in types only.

### 2. `Tools/WorktreeCli/PlanScheduler.cpp` — verify unused `<cctype>` is absent [~5m]

The original review found an unused `#include <cctype>`; at the current tree it is already gone (the include block is `PlanScheduler.h`, `CoordinationStore.h`, `ToolCliCommon.h`, then `<algorithm>`, `<fstream>`, `<functional>`, `<iostream>`, `<map>`, `<set>`). Whitespace handling in `TrimLineEnding` uses no character-classification API. Verify `<cctype>` is still absent and make no edit; if it has reappeared, remove it.

### 3. `Tools/WorktreeCli/LandingLockLifecycle.h` — direct includes for named standard types [~10m]

The declarations in this header (the `LandingLease` struct and the five function declarations in `toolcli::landing`) name `std::string`, `std::wstring`, `int64_t`, `uint64_t`, `std::optional`, and `nlohmann::json`, but the header includes only `"CoordinationStore.h"` and receives all of those transitively. Add to its include block:

- `<cstdint>` (for `int64_t`, `uint64_t`)
- `<optional>` (for `std::optional`)
- `<string>` (for `std::string`, `std::wstring`)
- `"ToolCliCommon.h"` (for `nlohmann::json`, per ToolCommon's centralized shared-consumption rule — do not include `tinygltf/json.hpp` directly)

Keep `#include "CoordinationStore.h"` (needed for `coordination::Locator`). Order includes per the existing house pattern (standard headers, then project headers).

### 4. `Tools/ToolCommon/ToolCliCommon.h` — explicit `<exception>` for shared `std::exception` consumption [~5m]

The PCH-less translation units that catch `std::exception` — `Tools/ToolCommon/CoordinationStore.cpp`, `Tools/WorktreeCli/BuildCommand.cpp`, `Tools/AgentHarness/AgentHarness.cpp` — currently receive it transitively through `tinygltf/json.hpp`. All three already include `ToolCliCommon.h`, the designated centralization point. Add `<exception>` to the sorted standard-library include block of `ToolCliCommon.h` (alphabetically before `<filesystem>`). Do not edit the three consuming `.cpp` files.

## Critical files

- `Tools/WorktreeCli/LandingLockCommands.h`
- `Tools/WorktreeCli/LandingLockLifecycle.h`
- `Tools/WorktreeCli/PlanScheduler.cpp`
- `Tools/ToolCommon/ToolCliCommon.h`

## Risk tier

Tier 1 — mechanical, behavior-preserving include hygiene with no public signature or invariant exposure. Invariant exposure: none. No client/server, determinism, or project-membership impact; `ToolCliCommon.h` is compiled into both AgentTools, so both must compile after the change.

## Acceptance criteria

- `Tools/WorktreeCli/LandingLockCommands.h` no longer includes `<string>`.
- `Tools/WorktreeCli/PlanScheduler.cpp` does not include `<cctype>`.
- `Tools/WorktreeCli/LandingLockLifecycle.h` directly includes `<cstdint>`, `<optional>`, `<string>`, and `"ToolCliCommon.h"`.
- `Tools/ToolCommon/ToolCliCommon.h` includes `<exception>` in its standard-library block.
- WorktreeCli and AgentHarness compile cleanly (both are PCH-less; use `/compile`).
- No diff outside the include blocks of the four critical files.
