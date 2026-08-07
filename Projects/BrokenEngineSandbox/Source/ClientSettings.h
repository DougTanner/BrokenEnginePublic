#pragma once

#if defined(BT_CLIENT)

namespace game
{

void SaveGameSettings();
void LoadGameSettings();
void ResetGameSettings();

void SaveSoundSettings();
void LoadSoundSettings();
void ResetSoundSettings();

void SaveGraphicsSettings();
bool LoadGraphicsSettings();
void ResetGraphicsSettings();

void SaveTweaksSettings();
void LoadTweaksSettings();

void SaveClientState();
void LoadClientState();

} // namespace game

#endif // BT_CLIENT
