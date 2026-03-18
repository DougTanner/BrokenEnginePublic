# Tech Debt: Dead Code & Unused Includes

Source: /external-tech-debt on Engine/Source/Frame (non-recursive)

## Changes

### Engine/Source/Frame/Collision.cpp
- Remove `#include "Frame/HealthDamage.h"` (line 4) — unused and layer violation (engine including game header). Collision.cpp accesses damage via `CollisionLayer::pfDamages` pointers, not HealthDamage.h constants [~2m]

### Engine/Source/Frame/FrameBase.cpp
- Remove `#include "Frame/Collections/Players/Players.h"` (line 3) — unused. The only game-specific reference is `game::Frame::kfBaseAreaMinX` in the constructor, which comes transitively via other includes [~2m]

## Verification Notes
All items verified against source.
