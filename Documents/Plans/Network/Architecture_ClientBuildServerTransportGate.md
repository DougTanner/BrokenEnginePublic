# Architecture: Server Transport in the Client Build — Gate or Re-justify

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). The five Server transport files ship unwrapped in **both** vcxprojs, justified by `Server/CLAUDE.md:14`: "shared headers reference `ClientConnection`". That claim is no longer supported by the code — every reference to `ClientConnection`/`gpServer` outside `Engine/Source/Network/Server/` lives in server-vcxproj-only game files, and `Engine.h:89-93` includes `Server.h` only under `BT_SERVER`, so no client TU sees the types through aggregation. The client binary compiles and links ~1,100 lines of dead server transport (`Server.cpp` + `ServerReceive.cpp` + `ServerSend.cpp`). Enforcement is also asymmetric: Client/ files are double-protected (`BT_CLIENT` wrap **and** absent from the server vcxproj) while Server/ files have zero protection — a game-type leak into `ServerReceive.cpp` would silently ship in the client.

## Design

### Verify, then gate (or re-document)
- Re-run the reachability check at execution: grep `ClientConnection`, `gpServer`, `engine::Server` across Engine and game sources; confirm all hits outside `Engine/Source/Network/Server/` are in server-vcxproj-only files, and that the client `BrokenEngineSandbox.vcxproj` entries at lines ~407-408 / ~600-602 are the only client-side carriers. [~15m]
- If confirmed (expected): remove `Server.cpp`, `ServerReceive.cpp`, `ServerSend.cpp`, `Server.h`, `ServerTypes.h` from the client vcxproj/.filters, and wrap the three `.cpp` files in `#if defined(BT_SERVER)` so the gate is structural, matching the Client/ convention (per `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md`, fully-wrapped files appear only in the matching vcxproj — wrap and remove must land together). [~30m]
- Update `Engine/Source/Network/Server/CLAUDE.md:14` to state the real rule. If the user instead wants the files kept in the client build (e.g. to keep both halves compiling in the more-frequently-built client config), keep the vcxproj as-is and rewrite the doc to say that — the divergence between stated reason and code is the defect either way. [~10m]

## Critical files

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (+ `.filters`)
- `Engine/Source/Network/Server/Server.cpp`, `ServerReceive.cpp`, `ServerSend.cpp` (gain `BT_SERVER` wrap)
- `Engine/Source/Network/Server/CLAUDE.md` (stale rationale)

## Out of scope

- `ServerSessionBase.*` / `NetworkDiscoveryResponder.*` — already correctly server-only.
- The shared protocol files (`NetworkManager`, `NetworkProtocol`, `NetworkCursor`, `NetworkSerialization`, `NetworkSimulation`) — correctly in both builds.
- Any behavior change; this is build-graph + guard scope only.

## Acceptance criteria

- Client and server projects build clean; the client binary no longer contains the Server transport TUs.
- `Server/CLAUDE.md` states a rationale that matches the build configuration.

## Notes

- **Invariant exposure**: none at runtime — build wiring + `#ifdef` scope only. Compile errors are the failure mode, making this self-verifying.
- Grill decision: gate (recommended) vs re-document. If gating, decide whether `Server.h`/`ServerTypes.h` also get header-level wraps (consistent with Client.h) or rely on exclusion from the client vcxproj + `Engine.h`'s existing `BT_SERVER` span.

## Verification Notes

All items verified against source (2026-06-10):
- Client vcxproj (`BrokenEngineSandbox.vcxproj`) carries exactly the five files at the cited lines: `Server.h` (:407), `ServerTypes.h` (:408), `Server.cpp` (:600), `ServerReceive.cpp` (:601), `ServerSend.cpp` (:602). Server vcxproj carries the same plus `ServerSessionBase.cpp` (:455-458, :352-353).
- Reachability re-run: every `ClientConnection`/`gpServer`/`engine::Server` hit outside `Engine/Source/Network/Server/` is in `Projects/.../Network/Server/*` or `Projects/.../Server/ServerDisplay.cpp` — none of which appear in the client vcxproj (grep-confirmed zero matches). `Server.h` is included only by the three Server TUs, `ServerDisplay.cpp` (server-only), and `Engine.h:91` inside the `BT_SERVER` span (`Engine.h:89-92`).
- `Server/CLAUDE.md:14` is the exact stale line ("shared headers reference `ClientConnection` ... do not wrap these files or remove them from the client vcxproj").
- Dead-line count checks out: 377 + 425 + 276 = 1,078 lines across the three TUs.
- The existing `#if defined(BT_SERVER)` island around `game::gpServerSession->SendTimespeedToNewClient` (`ServerReceive.cpp:308-310`) is the only game-symbol use in the three TUs — wrapping the whole files subsumes it (the island can be flattened when gating lands).
