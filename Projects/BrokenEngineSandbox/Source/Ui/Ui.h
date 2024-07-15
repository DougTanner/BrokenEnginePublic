#pragma once

namespace engine
{

class Widget;

}

namespace game
{

inline constexpr uint32_t kuiDefaultTextColor = 0xEEEEEEFF;
inline constexpr float kfDefaultShadowOffset = 0.0275f;
inline constexpr uint32_t kuiDefaultShadowColor = 0x00000099;

engine::Widget BuildUi();

engine::Widget MainMenu();
engine::Widget LanguageMenu();

engine::Widget InGameMenu();
#if defined(ENABLE_DEBUG_INPUT)
engine::Widget TweaksMenu();
engine::Widget WaterSpecularMenu();
#endif

engine::Widget GraphicsMenu();
engine::Widget SoundMenu();

#if defined(BT_DEBUG)
inline std::u32string gDebugText;
engine::Widget InGameDebug();
#endif
engine::Widget InGame();
engine::Widget GameHud();
engine::Widget DeathMenu();

} // namespace game
