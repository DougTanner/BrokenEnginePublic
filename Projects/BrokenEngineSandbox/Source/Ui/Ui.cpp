#include "Ui.h"

#include "Graphics/Graphics.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Ui/Widget.h"
#include "Ui/UiManager.h"

#include "Game.h"

using namespace DirectX;
using namespace engine;

using enum engine::WidgetFlags;

namespace game
{

using enum FrameFlags;
using enum UiState;

constexpr float kfUiScale = 1.3f;

engine::Widget BuildUi()
{
	return
	VStack({},
	{
		MainMenu(),

		InGameMenu(),

		GraphicsMenu(),
		SoundMenu(),
	#if defined(ENABLE_DEBUG_INPUT)
		TweaksMenu(),
	#endif

	#if !defined(ENABLE_RECORDING)
	#if defined(BT_DEBUG)
		InGameDebug(),
	#endif
		InGame(),
		GameHud(),
	#endif
		DeathMenu(),
	});
}

// Main menu title
constexpr uint32_t kuiMainMenuTitleTextColor = 0xEEEEEEFF;

// Main menu buttons
constexpr XMFLOAT2 kf2MainMenuButtonSize = {0.2f, 0.045f};
constexpr float kfMainMenuButtonTextSize = 1.0f;
constexpr uint32_t kuiMainMenuButtonTextColor = 0xEEEEEEFF;
constexpr uint32_t kuiMainMenuButtonBackgroundColor = 0xFFFFFF33;

constexpr float kfMainMenuButtonsSpacerHeight = 0.018f;

// Main menu language select
constexpr float kfLanguageButtonsArea = 0.46f;
constexpr float kfLanguageButtonsSpacerWidth = 0.01f;
constexpr XMFLOAT2 kf2LanguageButtonSize = {0.06f, 0.02f};
constexpr float kfLanguageButtonTextSize = 0.9f;
constexpr uint32_t kuiLanguageButtonTextColor = 0xEEEEEEFF;
constexpr uint32_t kuiLanguageButtonSelected = 0xEEEEEE55;

Widget MainMenu()
{
	return VStack({.Enabled = []() { return gpGame->meUiState == kPause && gpGame->InMainMenu(); }},
	{
		HStack({},
		{
			Spacer({.f2Size = {0.11f, 0.0f}}),

			VStack({.flags = {}, .f2Size = {0.4f, 0.0f}},
			{
				Spacer({.f2Size = {0.0f, 0.35f}}),

				Button(kStringContinue, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.Enabled = []()
				{
					return gpGame->mbSavedFrame;
				},
				.OnClick = [](DirectX::XMFLOAT2)
				{
					gpGame->ChangeFrame(FrameFlags::kGame);
					gpGame->meUiState = kNone;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringPlay, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](DirectX::XMFLOAT2)
				{
					gpGame->ChangeFrame({FrameFlags::kGame, FrameFlags::kFirstSpawn});
					gpGame->meUiState = kNone;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringGraphics, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](DirectX::XMFLOAT2)
				{
					gpGame->meUiState = kGraphics;
					engine::gSunAngleOverride.Set(gpGame->CurrentFrame().fSunAngle);
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringSound, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](DirectX::XMFLOAT2)
				{
					gpGame->meUiState = kSound;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringQuit, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](DirectX::XMFLOAT2)
				{
					gpGame->Quit();
				}}),

				Spacer({.f2Size = {0.0f, 0.2f}}),
			}),
			Spacer(),
		}),
		LanguageMenu(),
		Spacer({.f2Size = {0.0f, 0.02f}}),
	});
}

Widget LanguageMenu()
{
	return HStack({.f2Size = {1.0f, 0.05f}},
	{
		Spacer(),
		Button(U"ENGLISH", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kEnglish;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kEnglish ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"中文", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = {0.05f, 0.025f}, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kChinese;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kChinese ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"ESPAÑOL", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kSpanish;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kSpanish ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"PORTUGUÊS", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kPortuguese;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kPortuguese ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"FRANÇAIS", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kFrench;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kFrench ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"DEUTSCH", {.flags = {kCenterVertical, kFocusOutline, kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](DirectX::XMFLOAT2)
		{
			geLanguage = kGerman;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kGerman ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer(),
	});
}

Widget InGameMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kPause && !gpGame->InMainMenu(); }},
	{
		Spacer(),
		VStack({.flags = {kCenterVertical, kMatchChildWidth}, .f2Size = {0.4f, 0.5f}, .f4Border = {0.01f, 0.01f, 0.01f, 0.01f}, .uiBackground = 0x2C67F6FF},
		{
			Spacer(),
			Button(kStringResume, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->meUiState = kNone;
			}}),
			Spacer(),
			Button(kStringRestart, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->RemoveAutosave();
				gpGame->ChangeFrame(FrameFlags::kMainMenu);
				gpGame->ChangeFrame({FrameFlags::kGame, FrameFlags::kFirstSpawn});
				gpGame->meUiState = kNone;
			}}),
			Spacer(),
			Button(kStringGraphics, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->meUiState = kGraphics;
			}}),
			Spacer(),
			Button(kStringSound, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->meUiState = kSound;
			}}),
			Spacer(),
			Button(kStringMainMenu, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->mbSavedFrame = true;
				gpGame->ChangeFrame(FrameFlags::kMainMenu);
				gpGame->meUiState = kPause;
			}}),
			Spacer(),
			Button(kStringQuit, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->Quit();
			}}),
			Spacer(),
		}),
		Spacer(),
	});
}

Widget GraphicsMenu()
{
	return VStack({.Enabled = []() { return gpGame->meUiState == kGraphics; }},
	{
		Spacer({.f2Size = {0.0f, 0.04f}}),
		HStack({.f2Size = {0.0f, 0.035f}},
		{
			Spacer({.f2Size = {0.45f, 0.0f}}),
			Text(U"", {.flags = {kTextAlignLeft}, .f2Size = {0.2f, 0.0f}, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.Text = []()
			{
				static std::u32string sText;
				sText = U"FPS: ";
				sText += common::ToU32string(std::to_string(engine::gpGraphics->mRendersInTheLastSecond.Get()));
				return std::u32string_view(sText);
			}}),
			Spacer({.f2Size = {0.35f, 0.0f}}),
		}),
		Spacer({.f2Size = {0.0f, 0.01f}}),
		HStack({},
		{
			VStack({},
			{
				Toggle(U"FULLSCREEN", {.pWrapper = &gFullscreen}),
				Spacer(),
				HStack({.f2Size = {0.0f, kfSliderToggleHeight}},
				{
					Spacer(),
					Text(U"PRESENTATION MODE", {.flags = kMatchTextWidth, .f2Size = {0.01f, 0.0f}, .fTextSize = 0.5f, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor}),
					Spacer(),
				}),
				RadioButtons<VkPresentModeKHR>({U"IMMEDIATE", U"MAILBOX", U"FIFO"}, {.flags = kMatchTextWidth, .f2Size = {0.01f, 0.0f}, .pWrapper = &gPresentMode}),
				Toggle(U"REDUCE INPUT LAG", {.pWrapper = &gReduceInputLag}),
				Spacer(),
				Toggle(U"MULTISAMPLING", {.pWrapper = &gMultisampling}),
				RadioButtons<VkSampleCountFlagBits>({U"2", U"4", U"8", U"16"}, {.f2Size = {kfSliderToggleHeight / gpSwapchainManager->mfAspectRatio, 0.0f}, .pWrapper = &gSampleCount}),
				Spacer(),
				Toggle(U"ANISOTROPY", {.pWrapper = &gAnisotropy}),
				Slider({.flags = kCaptureHides, .pWrapper = &gMaxAnisotropy}),
				Spacer(),
				Toggle(U"SAMPLE SHADING", {.pWrapper = &gSampleShading}),
				Slider({.flags = kCaptureHides, .pWrapper = &gMinSampleShading}),
				Spacer(),
			#if defined(BT_DEBUG)
				Slider(U"MIP LOD BIAS", {.pWrapper = &gMipLodBias}),
				Spacer(),
			#endif
			#if defined(ENABLE_WIREFRAME)
				Spacer(),
				Toggle(U"WIREFRAME", {.pWrapper = &gWireframe}),
			#endif
				Spacer(),
			}),
			VStack({},
			{
				Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride, .Enabled = []() { return gpGame->meUiState == kGraphics && gpGame->CurrentFrame().flags & kMainMenu; }}),
				Spacer(),
				Slider(U"MINIMUM AMBIENT", {.flags = kCaptureHides, .pWrapper = &gMinimumAmbient}),
				Spacer(),
				HStack({.f2Size = {0.0f, kfSliderToggleHeight}}, { Text(U"TERRAIN & SHADOW & WATER DETAIL", {.flags = {kMatchTextWidth, kCenterHorizontal}, .f2Size = {0.01f, 0.0f}, .fTextSize = 0.5f, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor}), }),
				RadioButtons<float>({U"1/16", U"1/8", U"1/4"}, {.flags = kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gWorldDetail}),
				Spacer(),
			#if defined(BT_DEBUG)
				Slider(U"TERRAIN ELEVATION TEXTURE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainElevationTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN COLOR TEXTURE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainColorTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN NORMAL TEXTURE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainNormalTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN AMBIENT OCCLUSION TEXTURE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainAmbientOcclusionTextureMultiplier}),
				Spacer(),
			#endif
				Toggle(U"SMOKE", {.pWrapper = &gSmoke}),
				Slider(U"SMOKE PIXELS", {.flags = kCaptureHides, .pWrapper = &gSmokeSimulationPixels}),
				Spacer(),
				Slider(U"SMOKE AREA", {.flags = kCaptureHides, .pWrapper = &gSmokeSimulationArea}),
				Spacer(),
			}),
		}),
	});
}

Widget SoundMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kSound; }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"MASTER VOLUME", {.flags = {}, .pWrapper = &gMasterVolume}),
			Spacer(),
			Slider(U"MUSIC VOLUME", {.flags = {}, .pWrapper = &gMusicVolume}),
			Spacer(),
			Slider(U"SOUND VOLUME", {.flags = {}, .pWrapper = &gSoundVolume}),
			Spacer(),
			Button(kStringDefaults, {.flags = {kCenterHorizontal, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = 0.5f, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				Game::ResetSoundSettings();
			}}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Spacer(),
		}),
	});
}

#if defined(ENABLE_DEBUG_INPUT)

// #define ENABLE_TEST
// #define ENABLE_GLTF_TWEAKS
// #define ENABLE_TERRAIN_TWEAKS
// #define ENABLE_WATER_SPECULAR_TWEAKS
// #define ENABLE_WATER_LOW_TWEAKS
// #define ENABLE_WATER_MEDIUM_TWEAKS
#define ENABLE_LIGHTING_TWEAKS
// #define ENABLE_WATER_LIGHTING_TWEAKS
// #define ENABLE_SHADOW_TWEAKS
// #define ENABLE_MISC_TWEAKS
// #define ENABLE_HEX_SHIELD_TWEAKS
// #define ENABLE_TURRET_TWEAKS

#if defined(ENABLE_TEST)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TEST ONE", {.flags = kCaptureHides, .pWrapper = &gTestOne}),
			Spacer(),
			Slider(U"TEST TWO", {.flags = kCaptureHides, .pWrapper = &gTestTwo}),
			Spacer(),
		}),
	});
}
#elif defined(ENABLE_GLTF_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"GLTF EXPOSURE", {.flags = kCaptureHides, .pWrapper = &gGltfExposuse}),
			Spacer(),
			Slider(U"GLTF GAMMA", {.flags = kCaptureHides, .pWrapper = &gGltfGamma}),
			Spacer(),
			Slider(U"GLTF AMBIENT", {.flags = kCaptureHides, .pWrapper = &gGltfIblAmbient}),
			Spacer(),
			Slider(U"GLTF DIFFUSE", {.flags = kCaptureHides, .pWrapper = &gGltfDiffuse}),
			Spacer(),
			Slider(U"GLTF SPECULAR", {.flags = kCaptureHides, .pWrapper = &gGltfSpecular}),
			Spacer(),
			Spacer(),
			Slider(U"GLTF SMOKE", {.flags = kCaptureHides, .pWrapper = &gGltfSmoke}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"BRDF", {.flags = kCaptureHides, .pWrapper = &gGltfBrdf}),
			Spacer(),
			Slider(U"BRDF POWER", {.flags = kCaptureHides, .pWrapper = &gGltfBrdfPower}),
			Spacer(),
			Slider(U"IBL", {.flags = kCaptureHides, .pWrapper = &gGltfIbl}),
			Spacer(),
			Slider(U"IBL POWER", {.flags = kCaptureHides, .pWrapper = &gGltfIblPower}),
			Spacer(),
			Slider(U"SUN", {.flags = kCaptureHides, .pWrapper = &gGltfSun}),
			Spacer(),
			Slider(U"SUN POWER", {.flags = kCaptureHides, .pWrapper = &gGltfSunPower}),
			Spacer(),
			Slider(U"LIGHTING", {.flags = kCaptureHides, .pWrapper = &gGltfLighting}),
			Spacer(),
			Slider(U"LIGHTING POWER", {.flags = kCaptureHides, .pWrapper = &gGltfLightingPower}),
			Spacer(),
		}),
	});
};
#endif

#if defined(ENABLE_TERRAIN_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"SNOW MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainSnowMultiplier}),
			Spacer(),
			Slider(U"BEACH HEIGHT", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachHeight}),
			Spacer(),
			Slider(U"BEACH SAND SIZE", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachSandSize}),
			Spacer(),
			Slider(U"BEACH SAND BLEND", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachSandBlend}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE ONE", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeOne}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE TWO", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeTwo}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE THREE", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeThree}),
			Spacer(),
			Slider(U"BEACH NORMALS BLEND", {.flags = kCaptureHides, .pWrapper = &gTerrainBeachNormalsBlend}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ISLAND HEIGHT", {.flags = kCaptureHides, .pWrapper = &gIslandHeight}),
			Spacer(),
			Slider(U"ROCK MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gTerrainRockMultiplier}),
			Spacer(),
			Slider(U"ROCK SAND SIZE", {.flags = kCaptureHides, .pWrapper = &gTerrainRockSize}),
			Spacer(),
			Slider(U"ROCK SAND BLEND", {.flags = kCaptureHides, .pWrapper = &gTerrainRockBlend}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE ONE", {.flags = kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeOne}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE TWO", {.flags = kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeTwo}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE THREE", {.flags = kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeThree}),
			Spacer(),
			Slider(U"ROCK NORMALS BLEND", {.flags = kCaptureHides, .pWrapper = &gTerrainRockNormalsBlend}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_SPECULAR_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SIZE", {.flags = kCaptureHides, .pWrapper = &gLightingSampledNormalsSize}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SIZE MOD", {.flags = kCaptureHides, .pWrapper = &gLightingSampledNormalsSizeMod}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SPEED", {.flags = kCaptureHides, .pWrapper = &gLightingSampledNormalsSpeed}),
			Spacer(),
			Slider(U"DEPTH REFLECTION FEATHER", {.flags = kCaptureHides, .pWrapper = &gWaterDepthReflectionFeather}),
			Spacer(),
			Slider(U"SUN BIAS", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxSunBias}),
			Spacer(),
			Slider(U"NORMAL SOFTEN", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxNormalSoften}),
			Spacer(),
			Slider(U"NORMAL BLEND WAVE", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxNormalBlendWave}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"INTENSITY", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxIntensity}),
			Spacer(),
			Slider(U"ADD", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxAdd}),
			Spacer(),
			Slider(U"SKYBOX 1", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxOne}),
			Spacer(),
			Slider(U"SKYBOX POWER 1", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxOnePower}),
			Spacer(),
			Slider(U"SKYBOX TWO", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxTwo}),
			Spacer(),
			Slider(U"SKYBOX POWER TWO", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxTwoPower}),
			Spacer(),
			Slider(U"SKYBOX THREE", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxThree}),
			Spacer(),
			Slider(U"SKYBOX POWER THREE", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSkyboxThreePower}),
			Spacer(),
			Slider(U"HEIGHT DARKEN TOP", {.flags = kCaptureHides, .pWrapper = &gWaterHeightDarkenTop}),
			Spacer(),
			Slider(U"HEIGHT DARKEN BOTTOM", {.flags = kCaptureHides, .pWrapper = &gWaterHeightDarkenBottom}),
			Spacer(),
			Slider(U"HEIGHT DARKEN CLAMP", {.flags = kCaptureHides, .pWrapper = &gWaterHeightDarkenClamp}),
			Spacer(),
		}),
	});
};
#endif

#if defined(ENABLE_WATER_LOW_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			RadioButtons<int64_t>({U"15", U"31", U"63", U"127", U"255"}, {.flags = kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gLowCount}),
			Spacer(),
			Slider(U"LOW MAX", {.flags = kCaptureHides, .pWrapper = &gLowMax}),
			Spacer(),
			Slider(U"ANGLE", {.flags = kCaptureHides, .pWrapper = &gLowAngle}),
			Spacer(),
			Slider(U"WAVELENGTH", {.flags = kCaptureHides, .pWrapper = &gLowWavelength}),
			Spacer(),
			Slider(U"AMPLITUDE", {.flags = kCaptureHides, .pWrapper = &gLowAmplitude}),
			Spacer(),
			Slider(U"SPEED", {.flags = kCaptureHides, .pWrapper = &gLowSpeed}),
			Spacer(),
			Slider(U"STEEPNESS", {.flags = kCaptureHides, .pWrapper = &gLowSteepness}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ANGLE ADJUST", {.flags = kCaptureHides, .pWrapper = &gLowAngleAdjust}),
			Spacer(),
			Slider(U"WAVELENGTH ADJUST", {.flags = kCaptureHides, .pWrapper = &gLowWavelengthAdjust}),
			Spacer(),
			Slider(U"AMPLITUDE ADJUST", {.flags = kCaptureHides, .pWrapper = &gLowAmplitudeAdjust}),
			Spacer(),
			Slider(U"SPEED ADJUST", {.flags = kCaptureHides, .pWrapper = &gLowSpeedAdjust}),
			Spacer(),
			Spacer(),
			Slider(U"BEACH DIRECTIONAL FADE BOTTOM", {.flags = kCaptureHides, .pWrapper = &gBeachDirectionalFadeBottom}),
			Spacer(),
			Slider(U"BEACH DIRECTIONAL FADE HEIGHT", {.flags = kCaptureHides, .pWrapper = &gBeachDirectionalFadeHeight}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_MEDIUM_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			RadioButtons<int64_t>({U"15", U"31", U"63", U"127", U"255"}, {.flags = kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gMediumCount}),
			Spacer(),
			Slider(U"WAVELENGTH", {.flags = kCaptureHides, .pWrapper = &gMediumWavelength}),
			Spacer(),
			Slider(U"AMPLITUDE", {.flags = kCaptureHides, .pWrapper = &gMediumAmplitude}),
			Spacer(),
			Slider(U"SPEED", {.flags = kCaptureHides, .pWrapper = &gMediumSpeed}),
			Spacer(),
			Slider(U"STEEPNESS", {.flags = kCaptureHides, .pWrapper = &gMediumSteepness}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ANGLE ADJUST", {.flags = kCaptureHides, .pWrapper = &gMediumAngleAdjust}),
			Spacer(),
			Slider(U"WAVELENGTH ADJUST", {.flags = kCaptureHides, .pWrapper = &gMediumWavelengthAdjust}),
			Spacer(),
			Slider(U"AMPLITUDE ADJUST", {.flags = kCaptureHides, .pWrapper = &gMediumAmplitudeAdjust}),
			Spacer(),
			Slider(U"SPEED ADJUST", {.flags = kCaptureHides, .pWrapper = &gMediumSpeedAdjust}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_LIGHTING_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"TEXTURE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gLightingTextureMultiplier}),
			Spacer(),

			Slider(U"BLUR DISTANCE", {.flags = kCaptureHides, .pWrapper = &gLightingBlurDistance}),
			Spacer(),
			Slider(U"BLUR DIRECTIONALITY", {.flags = kCaptureHides, .pWrapper = &gLightingBlurDirectionality}),
			Spacer(),
			Slider(U"BLUR JITTER", {.flags = kCaptureHides, .pWrapper = &gLightingBlurJitter}),
			Spacer(),
			Slider(U"DOWNSCALE", {.flags = kCaptureHides, .pWrapper = &gLightingBlurDownscale}),
			Spacer(),
			Slider(U"COMBINE INDEX", {.flags = kCaptureHides, .pWrapper = &gLightingCombineIndex}),
			Spacer(),
			Slider(U"BLUR FIRST DIVISOR", {.flags = kCaptureHides, .pWrapper = &gLightingBlurFirstDivisor}),
			Spacer(),
			Slider(U"BLUR DIVISOR", {.flags = kCaptureHides, .pWrapper = &gLightingBlurDivisor}),
			Spacer(),
			Slider(U"COMBINE DECAY", {.flags = kCaptureHides, .pWrapper = &gLightingCombineDecay}),
			Spacer(),

			Slider(U"COMBINE POWER", {.flags = kCaptureHides, .pWrapper = &gLightingCombinePower}),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"DIRECTIONAL", {.flags = kCaptureHides, .pWrapper = &gLightingDirectional}),
			Spacer(),
			Slider(U"INDIRECT", {.flags = kCaptureHides, .pWrapper = &gLightingIndirect}),
			Spacer(),
			Slider(U"TERRAIN", {.flags = kCaptureHides, .pWrapper = &gLightingTerrain}),
			Spacer(),
			Slider(U"TERRAIN ADD", {.flags = kCaptureHides, .pWrapper = &gLightingAddTerrain}),
			Spacer(),
			Slider(U"OBJECTS", {.flags = kCaptureHides, .pWrapper = &gLightingObjects}),
			Spacer(),
			Slider(U"OBJECTS ADD", {.flags = kCaptureHides, .pWrapper = &gLightingObjectsAdd}),
			Spacer(),
			Slider(U"TIME OF DAY MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gLightingTimeOfDayMultiplier}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_LIGHTING_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
		}),
		VStack({},
		{
			Slider(U"NormalSoften", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularNormalSoften}),
			Spacer(),
			Slider(U"NormalBlendWave", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularNormalBlendWave}),
			Spacer(),
			Slider(U"Diffuse", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularDiffuse}),
			Spacer(),
			Slider(U"Direct", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularDirect}),
			Spacer(),
			Slider(U"Specular", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecular}),
			Spacer(),
			Slider(U"SpecularIntensity", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularIntensity}),
			Spacer(),
			Slider(U"SpecularAdd", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularAdd}),
			Spacer(),
			Slider(U"SpecularOne", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularOne}),
			Spacer(),
			Slider(U"SpecularTwo", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularTwo}),
			Spacer(),
			Slider(U"SpecularThree", {.flags = kCaptureHides, .pWrapper = &gLightingWaterSpecularThree}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_SHADOW_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"FEATHER NOON", {.flags = kCaptureHides, .pWrapper = &gShadowFeatherNoon}),
			Spacer(),
			Slider(U"FEATHER NOON OFFSET", {.flags = kCaptureHides, .pWrapper = &gShadowFeatherNoonOffset}),
			Spacer(),
			Slider(U"FEATHER SUNSET", {.flags = kCaptureHides, .pWrapper = &gShadowFeatherSunset}),
			Spacer(),
			Slider(U"FEATHER SUNSET OFFSET", {.flags = kCaptureHides, .pWrapper = &gShadowFeatherSunsetOffset}),
			Spacer(),
			Slider(U"FEATHER POWER", {.flags = kCaptureHides, .pWrapper = &gShadowFeatherPower}),
			Spacer(),
			Slider(U"DISTANCE FALLOFF", {.flags = kCaptureHides, .pWrapper = &gShadowDistanceFallof}),
			Spacer(),
			Slider(U"BLUR SIGMA", {.flags = kCaptureHides, .pWrapper = &gShadowBlurSigma}),
			Spacer(),
			Slider(U"AFFECT AMBIENT", {.flags = kCaptureHides, .pWrapper = &gShadowAffectAmbient}),
			Spacer(),
			Slider(U"HEIGHT FADE TOP", {.flags = kCaptureHides, .pWrapper = &gShadowHeightFadeTop}),
			Spacer(),
			Slider(U"HEIGHT FADE BOTTOM", {.flags = kCaptureHides, .pWrapper = &gShadowHeightFadeBottom}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"OBJECT SHADOW RENDER MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsRenderMultiplier}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsBlurMultiplier}),
			Spacer(),
			Slider(U"OBJECT SHADOW NOON", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsNoon}),
			Spacer(),
			Slider(U"OBJECT SHADOW SUNSET", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsSunset}),
			Spacer(),
			Slider(U"OBJECT SHADOW SUNSET STRETCH", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsSunsetStretch}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR DISTANCE NOON", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsBlurDistanceNoon}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR DISTANCE SUNSET", {.flags = kCaptureHides, .pWrapper = &gObjectShadowsBlurDistanceSunset}),
			Spacer(),
			Slider(U"SMOKE SHADOW INTENSITY", {.flags = kCaptureHides, .pWrapper = &gSmokeShadowIntensity}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_MISC_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"MISC0", {.flags = kCaptureHides, .pWrapper = &gMisc0}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ISLAND HEIGHT", {.flags = kCaptureHides, .pWrapper = &gIslandHeight}),
			Spacer(),
			Slider(U"WATER DEPTH", {.flags = kCaptureHides, .pWrapper = &gWaterDepth}),
			Spacer(),
			Slider(U"WATER TERRAIN HEIGHT", {.flags = kCaptureHides, .pWrapper = &gWaterTerrainHeight}),
			Spacer(),
			Slider(U"WATER TERRAIN FADE", {.flags = kCaptureHides, .pWrapper = &gWaterTerrainFade}),
			Spacer(),
			Slider(U"DEPTH REFLECTION FEATHER", {.flags = kCaptureHides, .pWrapper = &gWaterDepthReflectionFeather}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_HEX_SHIELD_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"GROW", {.flags = kCaptureHides, .pWrapper = &gHexShieldGrow}),
			Spacer(),
			Slider(U"EDGE DISTANCE", {.flags = kCaptureHides, .pWrapper = &gHexShieldEdgeDistance}),
			Spacer(),
			Slider(U"EDGE POWER", {.flags = kCaptureHides, .pWrapper = &gHexShieldEdgePower}),
			Spacer(),
			Slider(U"EDGE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gHexShieldEdgeMultiplier}),
			Spacer(),
			Spacer(),
			Slider(U"WAVE MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gHexShieldWaveMultiplier}),
			Spacer(),
			Slider(U"WAVE DOT MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gHexShieldWaveDotMultiplier}),
			Spacer(),
			Slider(U"WAVE INTENSITY MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gHexShieldWaveIntensityMultiplier}),
			Spacer(),
			Slider(U"WAVE INTENSITY POWER", {.flags = kCaptureHides, .pWrapper = &gHexShieldWaveIntensityPower}),
			Spacer(),
			Slider(U"WAVE FALLOFF POWER", {.flags = kCaptureHides, .pWrapper = &gHexShieldWaveFalloffPower}),
			Spacer(),
			Spacer(),
			Slider(U"DIRECTION FALLOFF POWER", {.flags = kCaptureHides, .pWrapper = &gHexShieldDirectionFalloffPower}),
			Spacer(),
			Slider(U"DIRECTION MULTIPLIER", {.flags = kCaptureHides, .pWrapper = &gHexShieldDirectionMultiplier}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_TURRET_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"FIRING OFFSET", {.flags = kCaptureHides, .pWrapper = &gTurretFiringOffset}),
			Spacer(),
			Slider(U"FIRING Z OFFSET", {.flags = kCaptureHides, .pWrapper = &gTurretFiringZOffset}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
		}),
	});
}
#endif

#endif // ENABLE_DEBUG_INPUT

#if defined(BT_DEBUG)
Widget InGameDebug()
{
	return VStack({.flags = {kExcludeFromLayout}, .f2Size = {0.5f, 0.5f}},
	{
		Text({.flags = {kCenterHorizontal}, .fTextSize = 0.125f, .fShadowOffset = 0.025f, .uiShadowColor = 0x000000AA,
		.Text = []()
		{
			return std::u32string_view(gDebugText);
		},
		.TextColor = []()
		{
			return 0xFFFFFFFF;
		},
		.ShadowColor = []()
		{
			return 0x000000FF;
		}}),
		Spacer({.f2Size = {0.0f, 0.4f}}),
	});
}
#endif

Widget InGame()
{
	return VStack({.flags = {kExcludeFromLayout}, .f2Size = {0.5f, 0.5f}, .Enabled = []() { return !(gpGame->CurrentFrame().flags & kDeathScreen); }},
	{
		Text({.flags = {kCenterHorizontal}, .fTextSize = 0.125f, .fShadowOffset = 0.025f, .uiShadowColor = 0x000000AA,
		.Text = []()
		{
			static std::u32string sText;
			sText = TranslatedString(kStringWave);
			sText += U": ";
			sText += common::ToU32string(std::to_string(gpGame->CurrentFrame().iWave));
			return std::u32string_view(sText);
		},
		.TextColor = []()
		{
			float fAlpha = gpGame->CurrentFrame().fWaveDisplayTimeLeft / Frame::kfWaveDisplayTime;
			return 0xFFFFFF00 | static_cast<uint32_t>(255.0f * fAlpha);
		},
		.ShadowColor = []()
		{
			float fAlpha = gpGame->CurrentFrame().fWaveDisplayTimeLeft / Frame::kfWaveDisplayTime;
			return 0x00000000 | static_cast<uint32_t>(255.0f * fAlpha * fAlpha);
		}}),
		Spacer({.f2Size = {0.0f, 0.4f}}),
	});
}

constexpr float kfShieldWidthPerPoint = kfUiScale * 0.002f;
constexpr float kfArmorWidthPerPoint = kfUiScale * 0.004f;

float ShieldWidth()
{
	return std::max(kfShieldWidthPerPoint * gpGame->CurrentFrame().player.fShield, 0.001f);
}

float ArmorWidth()
{
	return std::max(kfArmorWidthPerPoint * gpGame->CurrentFrame().player.fArmor, 0.001f);
}

float ShieldArmorMaxWidth()
{
	return std::max(std::max(kfShieldWidthPerPoint * Player::MaxShield(gpGame->CurrentFrame()), kfArmorWidthPerPoint * Player::MaxArmor(gpGame->CurrentFrame())), 0.001f);
}

Widget GameHud()
{
	static constexpr float kfBottomPadding = kfUiScale * 0.035f;
	static constexpr float kfMidPadding = kfUiScale * 0.02f;

	static constexpr float kfIconSize = kfUiScale * 0.02f;
	static constexpr float kfSecondaryIconSize = kfUiScale * 0.03f;

	static constexpr float kfShieldArmorContainerHeight = kfUiScale * 0.025f;
	static constexpr float kfShieldArmorHeight = kfUiScale * 0.005f;
	static constexpr float kfShieldArmorMaxWidth = kfUiScale * 0.00075f;

	static constexpr float kfEnergyDotDistance = kfUiScale * 0.02f;
	static constexpr float kfEnergyDotSize = kfUiScale * 0.006f;
	static constexpr float kfEnergyDotCount = kfUiScale * 128.0f;
	static constexpr float kfSecondaryDotDistance = kfUiScale * 0.025f;
	static constexpr float kfSecondaryDotSize = kfUiScale * 0.0075f;

	return VStack({.Enabled = []() { return gpGame->meUiState == kNone &&
	                                        !(gpGame->CurrentFrame().flags & kDeathScreen); }},
	{
		Spacer(),
		HStack({.f2Size = {0.0f, 2.0f * kfShieldArmorContainerHeight}},
		{
			Spacer(),
			Rotary({.flags = {kCenterVertical, kBackgroundTexture}, .f2Size = {kfIconSize / gpSwapchainManager->mfAspectRatio, kfIconSize},
			.BackgroundTexture = []()
			{
				return data::kTexturesUiBC7EnergyIconpngCrc;
			},
			.RotaryInfo = []()
			{
				return RotaryInfo {static_cast<int64_t>(kfEnergyDotCount), static_cast<int64_t>((gpGame->CurrentFrame().player.fEnergy / Player::MaxEnergy(gpGame->CurrentFrame())) * kfEnergyDotCount), kfEnergyDotDistance, kfEnergyDotSize, 0x44EEFFFF, 0x00000000};
			}}),
			Spacer({.f2Size = {kfMidPadding, 0.0f}}),
			VStack({
			.Size = []()
			{
				return XMFLOAT2 {ShieldArmorMaxWidth(), 2.0f * kfShieldArmorContainerHeight};
			}},
			{
				HStack({.f2Size = {0.0f, kfShieldArmorContainerHeight}},
				{
					// Shield
					HStack({.flags = {kCenterHorizontal, kCenterVertical},
					.Size = []()
					{
						return XMFLOAT2 {kfShieldWidthPerPoint * Player::MaxShield(gpGame->CurrentFrame()), kfShieldArmorHeight};
					}},
					{
						Spacer({.flags = {kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
						Spacer(),
						Spacer({.flags = {kBackground}, .uiBackground = 0x0088FFFF,
						.Size = []()
						{
							return XMFLOAT2 {ShieldWidth() - kfShieldArmorMaxWidth, kfShieldArmorHeight};
						}}),
						Spacer(),
						Spacer({.flags = {kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
					}),
					Spacer({.flags = {kCenterHorizontal, kCenterVertical, kBackgroundTexture},
					.Size = []()
					{
						return XMFLOAT2 {kfIconSize / gpSwapchainManager->mfAspectRatio, kfIconSize};
					},
					.BackgroundTexture = []()
					{
						return data::kTexturesUiBC7ShieldIconpngCrc;
					}}),
				}),
				HStack({.f2Size = {0.0f, kfShieldArmorContainerHeight}},
				{
					// Armor
					HStack({.flags = {kCenterHorizontal, kCenterVertical},
					.Size = []()
					{
						return XMFLOAT2 {kfArmorWidthPerPoint * Player::MaxArmor(gpGame->CurrentFrame()), kfShieldArmorHeight};
					}},
					{
						Spacer({.flags = {kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
						Spacer(),
						Spacer({.flags = {kBackground}, .uiBackground = 0xFF2222FF,
						.Size = []()
						{
							return XMFLOAT2 {ArmorWidth() - kfShieldArmorMaxWidth, kfShieldArmorHeight};
						}}),
						Spacer(),
						Spacer({.flags = {kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
					}),
					Spacer({.flags = {kCenterHorizontal, kCenterVertical, kBackgroundTexture}, .f2Size = {kfIconSize / gpSwapchainManager->mfAspectRatio, kfIconSize},
					.BackgroundTexture = []()
					{
						return data::kTexturesUiBC7ArmorIconpngCrc;
					}}),
				}),
			}),
			Spacer({.f2Size = {kfMidPadding, 0.0f}}),
			Rotary({.flags = {kCenterVertical, kBackgroundTexture}, .f2Size = {kfSecondaryIconSize / gpSwapchainManager->mfAspectRatio, kfSecondaryIconSize},
			.BackgroundTexture = []()
			{
				return data::kTexturesUiBC7MissileIconpngCrc;
			},
			.RotaryInfo = []()
			{
				auto [iCurrent, iCapacity] = Player::SecondaryCapacity(gpGame->CurrentFrame());
				return RotaryInfo {iCurrent, iCapacity, kfSecondaryDotDistance, kfSecondaryDotSize, 0xFF6F0FFF, 0x999999EE};
			}}),
			Spacer(),
		}),
		Spacer({.f2Size = {0.0f, kfBottomPadding}}),
	});
}

Widget DeathMenu()
{
	static constexpr float kfShadowOffset = 0.025f;

	static constexpr float kfRecapTextSize = 0.05f;
	static constexpr float kfRecapSpacerSize = 0.025f;

	static constexpr float kfTipTextSize = 0.035f;
	static constexpr float kfNextTipTextSize = 0.04f;

	return HStack({.Enabled = []() { return gpGame->meUiState == kNone && (gpGame->CurrentFrame().flags & kDeathScreen); }},
	{
		VStack({},
		{
			Spacer({.f2Size = {0.0f, 0.1f}}),
			Text({.flags = {kCenterHorizontal, kMatchTextWidth}, .f2Size = {0.1f, 0.1f}, .uiTextColor = kuiMainMenuTitleTextColor, .fShadowOffset = kfShadowOffset, .uiShadowColor = 0x000000AA,
			.Text = []()
			{
				return TranslatedString(kStringGameOver);
			}}),
			Spacer({.f2Size = {0.0f, 0.05f}}),
			HStack({.flags = {kCenterHorizontal, kMatchChildHeight}, .f2Size = {0.0f, 0.0f}},
			{
				Spacer(),
				Text(kStringWave, {.flags = {kCenterVertical, kMatchTextWidth}, .f2Size = {0.0f, kfRecapTextSize}, .uiTextColor = kuiMainMenuTitleTextColor, .fShadowOffset = kfShadowOffset, .uiShadowColor = 0x000000AA}),
				Text({.flags = {kCenterVertical, kMatchTextWidth}, .f2Size = {0.0f, kfRecapTextSize}, .uiTextColor = kuiMainMenuTitleTextColor, .fShadowOffset = kfShadowOffset, .uiShadowColor = 0x000000AA,
				.Text = []()
				{
					return gpGame->WaveText();
				}}),
				Spacer(),
			}),
			Spacer({.f2Size = {0.0f, kfRecapSpacerSize}}),
			Button(kStringRestart, {.flags = {kCenterHorizontal, kMatchTextWidth, kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = game::kfDefaultShadowOffset, .uiShadowColor = game::kuiDefaultShadowColor,
			.OnClick = [](DirectX::XMFLOAT2)
			{
				gpGame->RemoveAutosave();
				gpGame->ChangeFrame(FrameFlags::kMainMenu);
				DEBUG_BREAK();
				// gpGame->meUiState = kSetup;
			}}),
			Spacer({.f2Size = {0.0f, 0.1f}}),
		}),
	});
}

} // namespace game
