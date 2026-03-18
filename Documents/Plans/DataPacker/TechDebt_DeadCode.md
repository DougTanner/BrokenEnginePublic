# Tech Debt: Dead Code

Source: /external-tech-debt on DataPacker/Source

## Changes

### DataPacker/Source/Main.cpp
- Remove unused `kiDataPackerVersion` constant (line 15) [~2m]
- Remove `sbSingleThread` variable (line 17) and the conditional block that checks it (lines 70-73) [~2m]

### DataPacker/Source/Texture.h
- Remove unused `miChannels` member (line 51) [~2m]

### DataPacker/Source/Texture.cpp
- Remove `miChannels = 4;` assignment (line 40) [~2m]

## Verification Notes
All items verified against source. Line numbers confirmed accurate. All references are truly dead (declared/assigned but never read).
