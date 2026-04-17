# Architecture: Split Fat Hub Headers

Source: /external-architecture-review on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

## Changes

### Engine/Source/Network/NetworkDiscovery.{h,cpp}
- Single file contains both `NetworkDiscoveryResponder` (BT_SERVER) and `NetworkDiscoveryScanner` (BT_CLIENT) via in-file guards. Split into `NetworkDiscoveryResponder.{h,cpp}` (server) and `NetworkDiscoveryScanner.{h,cpp}` (client); each full-file-wrap with the appropriate guard; update vcxprojs so each file lives in only the matching project. Follows the root CLAUDE.md rule: "Files fully wrapped in `#if defined(BT_CLIENT)` must only appear in the client vcxproj; same for `BT_SERVER`." [~45m]

## Verification
- After the split, rebuild both client and server configs; expect zero changes in object-code behavior.
- Confirm each resulting file is in only the matching vcxproj filter.
- Update `Engine.h` aggregation: the current single `#include "Network/NetworkDiscovery.h"` at line 16 stays in the shared (pre-`BT_CLIENT`/`BT_SERVER`) span becomes two includes, each in the platform-gated span.

## Verification Notes
Removed the following items from the original plan:
- **`Client.h` DTO extraction** (`ReceivedCoordUpdate`, `ReceivedCoordFullState`, ...): cosmetic; no transitive-include benefit in a PCH-dominated project where every header has zero direct includes by design. Project's convention (see `Engine.h` comment at lines 6-20: load-bearing aggregation order) puts the dependency management in `Engine.h`, not headers.
- **`Server.h` DTO extraction to `ServerTypes2.h`**: same reasoning. The existing `ServerTypes.h` already handles the cross-header shared types; inventing `ServerTypes2.h` violates KISS.
- **`ClientSession.h` forward-decl refactor**: the engine already forward-declares `Client` / `NetworkDiscoveryScanner` in `ClientSessionBase.h`. `ClientSession.h` bulk-includes match the project's zero-include header convention relative to `Pch.h`.

Only the `NetworkDiscovery` split survives because it is a load-bearing fix for the `BT_CLIENT`/`BT_SERVER` file-wrap rule, not cosmetic.
