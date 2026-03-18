# Tech Debt: Dead Code

Source: /external-tech-debt and /external-architecture-review on Engine/Source/Network/Server

## Changes

### Engine/Source/Network/Server/Server.cpp
- Remove unused `pcAddress` resolution in `Connect()` (lines 125-126): `char pcAddress[64]` and `enet_address_get_host_ip()` call — result is never used [~5m]
- Remove unused `pcAddress` resolution in `Disconnect()` (lines 147-148): same dead pattern [~5m]
- Remove unused `#include "Network/NetworkCursor.h"` (line 6): no Read/Write cursor functions are used in this file [~2m]

### Engine/Source/Network/Server/ServerSend.cpp
- Remove unused `#include "Network/NetworkCursor.h"` (line 6): no Read/Write cursor functions are used in this file [~2m]

## Verification Notes
- All items verified against source. Line numbers confirmed accurate.
- TechDebt_Duplication.md was deleted during verification: proposed extracting a 2-line `std::ostringstream` serialization idiom into a shared method, which violates KISS/YAGNI for negligible DRY benefit.
