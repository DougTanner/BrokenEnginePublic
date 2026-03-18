# Tech Debt: Dead Code & Unused Dependencies

Source: /external-tech-debt on Engine/Source/Network/Client

## Changes

### Engine/Source/Network/Client/ClientReceive.cpp
- Remove unused `#include "Memory/MemoryManager.h"` (line 5) — `ScopedSuppressAllocationTracking` is already available through the Pch.h include chain (Client.cpp and ClientSend.cpp use it without this include) [~2m]

## Verification Notes
- Confirm via build that removing the include does not break compilation
