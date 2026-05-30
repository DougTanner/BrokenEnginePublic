# Send/Nav-Delay LOG Float-Wrapper Sweep

## Context

Root `CLAUDE.md` rule: "NEVER use float format specs in `LOG(...)` — wrap each float arg with `common::Wb(value, precision)`; the placeholder stays `{}`." A just-completed session hardened `Common/Log/LogDifference.{h,cpp}` and folded in 4 raw-float `LOG` sites; a follow-up sweep found 4 *more* runtime `LOG` calls in the game Network layer that pass a raw `float` directly as a `{}` argument. These were outside that session's approved scope, so they are deferred here.

`common::Wb(float, int)` (`Common/Workbuffer.h:240`) already exists with its `std::formatter` specialization (`Common/Log/LogFormatters.h:265`) — no new infrastructure. Each site's fix is mechanical: wrap the raw float in `common::Wb(value, N)`, placeholder stays `{}`.

All four floats are navigation/send delays in seconds; nearby Network convention (`ClientReconciler.cpp:184,198`) uses precision 3 for small delta-magnitude floats, so precision 3 is used for each delay.

**Main-loop vs diagnostic assessment** (the rule's `DEBUG_BREAK()` allocation tracker only arms on the main loop; raw-float `LOG` heap-allocates):
- `ClientSession::SendUpdatePlayerRequest` (`:408`, **`kVerbose`**) — emitted per player-update send on the client. The `kVerbose` level + per-input-event cadence makes this the one site that can plausibly fire on a per-frame/main-loop path → real-fix candidate (would trip the tracker when the user is steering a fleet).
- The other three are **`kDebug`** one-shot/diagnostic sends (fleet nav-delay change, server-side request processing, server fleet-delay update) — driven by discrete UI actions / inbound requests, not per-frame. Latent-hardening (rule-consistency), not a live tracker trip.

Net: 1 real per-frame fix + 3 latent-hardening, all the same one-line mechanical wrap.

## Design

Per-site change (wrap the raw float, keep `{}`):

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:408` — `ClientSession::SendUpdatePlayerRequest`, `kVerbose`. `NavDelay: {}` arg `fNavigationDelay` → `common::Wb(fNavigationDelay, 3)`. (Per-frame path — real fix.)
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:498` — `ClientSession::SendFleetNavigationDelayRequest`, `kDebug`. `Delay: {}` arg `fDelay` → `common::Wb(fDelay, 3)`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp:235` — `ServerBroadcaster::ProcessUpdatePlayerRequests`, `kDebug`. `NavDelay: {}` arg `rRequest.fNavigationDelay` → `common::Wb(rRequest.fNavigationDelay, 3)`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp:534` — `ServerFleetManager::UpdateFleetNavigationDelay`, `kDebug`. `Delay: {}` arg `fDelay` → `common::Wb(fDelay, 3)`.

No format-string changes (placeholders already `{}`); no signature, layout, wire, or CRC change.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp`
- Reference only (no edit): `Common/Workbuffer.h` (`Wb`), `Common/Log/LogFormatters.h` (`Wb` formatter).

## Out of scope

- **DataPacker offline `{:.Nf}` sites** (`ProcessBakedRegion.cpp`, `BakeRoute.cpp`, `Main.cpp`, `MigrateLegacyIntermediates.cpp`, `ExportScene.cpp` — 7 sites). DataPacker never arms the main-loop allocation tracker, so `{:.Nf}` is exempt there.
- **Already-handled sites** from the completed LogDifference session: `Common/Log/LogDifference.{h,cpp}` and the 4 raw-float sites it folded in.
- **`PlayersNavigation` / `GameBase` LOG sites** previously addressed / tracked elsewhere — not re-touched here.
- **`GameBase.cpp:371` whitespace nit** — cosmetic, unrelated.
- No precision-policy change, no new `Wb`/`WbV*` infrastructure, no format-string restructuring beyond the wrap.

## Acceptance criteria

- All 4 listed `LOG` calls pass their float through `common::Wb(value, 3)`; no remaining raw `float` `{}` arg at those sites.
- No `{:.Nf}`-style float spec introduced; placeholders stay `{}`.
- Client and server projects compile.
