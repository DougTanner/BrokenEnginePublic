# Architecture: Missing Direct Include

Source: /external-architecture-review on Engine/Source/Graphics/Managers

## Changes

### Engine/Source/Graphics/Managers/ImGuiManager.h
- Line 34 declares `TweaksScreen mTweaksScreen` but the header does not directly include TweaksScreen.h — relies on transitive include via Pch.h -> Engine.h -> TweaksScreen.h. Add `#include "Ui/Screens/TweaksScreen/TweaksScreen.h"` after the other screen includes (after line 9) for self-containment [~5m]

## Verification Notes
- The transitive include comes through PCH -> Engine.h, not through any of the other screen headers. This is more robust than a screen-header chain but still not ideal — headers should be self-contained
