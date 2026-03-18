# Tech Debt: Dead Code & Unused Includes

Source: /external-tech-debt on Engine/Source/Ui/Screens/TweaksScreen

## Changes

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSmoke.cpp
- Remove dead slider on line 21: `WrapperSlider("Smoke Decay Extra", ...)` — no Wrapper global exists, silently no-ops [~2m]
- Remove dead slider on line 22: `WrapperSlider("Smoke Decay Extra Threshold", ...)` — no Wrapper global exists, silently no-ops [~2m]
- Remove dead slider on line 40: `WrapperSlider("Wind Displacement Swirl Scale", ...)` — no Wrapper global exists, silently no-ops [~2m]
- Remove dead slider on line 41: `WrapperSlider("Wind Displacement Swirl Power", ...)` — no Wrapper global exists, silently no-ops [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTest.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTerrain.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterSpecular.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterLow.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterMedium.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterLighting.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenShadow.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSmoke.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWind.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWindDeposits.cpp
- Remove unnecessary `#include "Game.h"` (line 3) — no `game::` symbols used [~1m]

## Verification Notes

All line numbers and file paths verified correct. Dead sliders confirmed: no Wrapper globals exist for the 4 labels, and they are absent from GetSliderMap(). All 13 section files confirmed to use no `game::` symbols — only TweaksScreen.cpp needs Game.h.
