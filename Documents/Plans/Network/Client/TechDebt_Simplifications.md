# Tech Debt: Simplifications

Source: /external-tech-debt on Engine/Source/Network/Client

## Changes

### Engine/Source/Network/Client/ClientSessionBase.cpp
- Review 13 `DT TEMP` / `DT: TEMP` diagnostic log lines (lines 95, 120, 163, 206, 208, 257, 259, 272, 274, 288, 296, 298, 305) for removal or promotion to permanent logging [~10m]

### Engine/Source/Network/Client/ClientReceive.cpp
- Review 1 `DT TEMP` diagnostic log line (line 317) for removal or promotion to permanent logging [~2m]

### Engine/Source/Network/Client/ClientSend.cpp
- Review 1 `DT TEMP` diagnostic log line (line 186) for removal or promotion to permanent logging [~2m]

## Verification Notes
- O(n²) subscription queue drain item was removed — `mSubscriptionQueue` is bounded to ~16 elements (coord slot count), making the optimization a KISS/YAGNI violation
- DT TEMP log items require the implementer to decide disposition (remove vs. promote) for each line before acting
