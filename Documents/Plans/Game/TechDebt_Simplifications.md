# Tech Debt: Simplifications

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source

## Changes

### Projects/BrokenEngineSandbox/Source/Game.h
- Convert `mMenuMusicPlaylist` and `mGameMusicPlaylist` (lines 132-133) from `std::vector<common::crc_t>` to `static constexpr std::array<common::crc_t, 4>`. This avoids heap allocation and aligns with the allocation-tracking policy. Update `GetNextMusicTrack()` and all `PlayMusic(playlist.at(0))` calls to use array access [~10m]

### Projects/BrokenEngineSandbox/Source/Game.cpp + Game.h
- Guard `SoundSettings` struct (line 628), `kpcSoundSettingsPath` (line 636), `SaveSoundSettings()` (line 638), `LoadSoundSettings()` (line 650), and `ResetSoundSettings()` (line 667) with `#ifdef BT_CLIENT` / `#endif`. Also guard the declarations in Game.h (lines 96-98). These are only called from `BT_CLIENT` code paths [~5m]

## Verification Notes
- Playlist conversion: Verified — `data::kAudio*Crc` constants are constexpr, `.at()` and `.size()` work on `std::array`. Consider namespace-scope `constexpr` arrays or `static constexpr` class data members
- Sound settings guard: Callers verified as client-only (Main.cpp:177,312 both inside `#ifdef BT_CLIENT`, SoundMenuScreen.cpp is client-only UI). Safe to guard
