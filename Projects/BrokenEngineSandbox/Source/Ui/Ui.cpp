#include "Ui.h"

#include "Graphics/Graphics.h"
#include "Ui/Widget.h"
#include "Ui/UiManager.h"

#include "Game.h"
#include "Graphics/Camera.h"

using namespace engine;

namespace game
{

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
	return VStack({.Enabled = []() { return gpGame->meUiState == UiState::kPause && gpGame->InMainMenu(); }},
	{
		HStack({},
		{
			Spacer({.f2Size = {0.11f, 0.0f}}),

			VStack({.flags = {}, .f2Size = {0.4f, 0.0f}},
			{
				Spacer({.f2Size = {0.0f, 0.35f}}),

				Button(kStringContinue, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.Enabled = []()
				{
					return gpGame->mbSavedFrame;
				},
				.OnClick = [](XMFLOAT2)
				{
					gpGame->ChangeFrame(FrameFlags::kGame);
					gpGame->meUiState = UiState::kNone;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringPlay, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](XMFLOAT2)
				{
					gpGame->ChangeFrame(FrameFlags::kGame);
					gpGame->meUiState = UiState::kNone;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringGraphics, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](XMFLOAT2)
				{
					gpGame->meUiState = UiState::kGraphics;
					engine::gSunAngleOverride.Set(game::gpCamera->mfSunAngle);
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringSound, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](XMFLOAT2)
				{
					gpGame->meUiState = UiState::kSound;
				}}),
				Spacer({.f2Size = {0.0f, kfMainMenuButtonsSpacerHeight}}),
				Button(kStringQuit, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
				.OnClick = [](XMFLOAT2)
				{
					gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
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
		Button(U"ENGLISH", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
		{
			geLanguage = kEnglish;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kEnglish ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"中文", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = {0.05f, 0.025f}, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
		{
			geLanguage = kChinese;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kChinese ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"ESPAÑOL", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
		{
			geLanguage = kSpanish;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kSpanish ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"PORTUGUÊS", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
		{
			geLanguage = kPortuguese;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kPortuguese ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"FRANÇAIS", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
		{
			geLanguage = kFrench;
		},
		.BackgroundColor = []()
		{
			return geLanguage == kFrench ? kuiLanguageButtonSelected : 0x00000000;
		}}),
		Spacer({.f2Size = {kfLanguageButtonsSpacerWidth, 0.0f}}),
		Button(U"DEUTSCH", {.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kFocusOutline, WidgetFlags::kBackground}, .f2Size = kf2LanguageButtonSize, .fTextSize = kfLanguageButtonTextSize, .uiTextColor = kuiLanguageButtonTextColor, .fShadowOffset = 0.03f, .uiShadowColor = 0x00000099,
		.OnClick = [](XMFLOAT2)
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
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kPause && !gpGame->InMainMenu(); }},
	{
		Spacer(),
		VStack({.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kMatchChildWidth}, .f2Size = {0.4f, 0.5f}, .f4Border = {0.01f, 0.01f, 0.01f, 0.01f}, .uiBackground = 0x2C67F6FF},
		{
			Spacer(),
			Button(kStringResume, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->meUiState = UiState::kNone;
			}}),
			Spacer(),
			Button(kStringRestart, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->RemoveAutosave();
				gpGame->ChangeFrame(FrameFlags::kMainMenu);
				gpGame->ChangeFrame(FrameFlags::kGame);
				gpGame->meUiState = UiState::kNone;
			}}),
			Spacer(),
			Button(kStringGraphics, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->meUiState = UiState::kGraphics;
			}}),
			Spacer(),
			Button(kStringSound, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->meUiState = UiState::kSound;
			}}),
			Spacer(),
			Button(kStringMainMenu, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->mbSavedFrame = true;
				gpGame->ChangeFrame(FrameFlags::kMainMenu);
				gpGame->meUiState = UiState::kPause;
			}}),
			Spacer(),
			Button(kStringQuit, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
			{
				gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
			}}),
			Spacer(),
		}),
		Spacer(),
	});
}

Widget GraphicsMenu()
{
	return VStack({.Enabled = []() { return gpGame->meUiState == UiState::kGraphics; }},
	{
		Spacer({.f2Size = {0.0f, 0.04f}}),
		HStack({.f2Size = {0.0f, 0.035f}},
		{
			Spacer({.f2Size = {0.45f, 0.0f}}),
			Text(U"", {.flags = {WidgetFlags::kTextAlignLeft}, .f2Size = {0.2f, 0.0f}, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
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
					Text(U"PRESENTATION MODE", {.flags = WidgetFlags::kMatchTextWidth, .f2Size = {0.01f, 0.0f}, .fTextSize = 0.5f, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor}),
					Spacer(),
				}),
				RadioButtons<VkPresentModeKHR>({U"IMMEDIATE", U"MAILBOX", U"FIFO"}, {.flags = WidgetFlags::kMatchTextWidth, .f2Size = {0.01f, 0.0f}, .pWrapper = &gPresentMode}),
				Spacer(),
				Toggle(U"MULTISAMPLING", {.pWrapper = &gMultisampling}),
				RadioButtons<VkSampleCountFlagBits>({U"2", U"4", U"8", U"16"}, {.f2Size = {kfSliderToggleHeight / gpSwapchainManager->mfAspectRatio, 0.0f}, .pWrapper = &gSampleCount}),
				Spacer(),
				Toggle(U"ANISOTROPY", {.pWrapper = &gAnisotropy}),
				Slider({.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMaxAnisotropy}),
				Spacer(),
				Toggle(U"SAMPLE SHADING", {.pWrapper = &gSampleShading}),
				Slider({.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMinSampleShading}),
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
				Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride, .Enabled = []() { return gpGame->meUiState == UiState::kGraphics && gpGame->CurrentFrame().interpolate.flags & FrameFlags::kMainMenu; }}),
				Spacer(),
				Slider(U"MINIMUM AMBIENT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMinimumAmbient}),
				Spacer(),
				HStack({.f2Size = {0.0f, kfSliderToggleHeight}}, { Text(U"TERRAIN & SHADOW & WATER DETAIL", {.flags = {WidgetFlags::kMatchTextWidth, WidgetFlags::kCenterHorizontal}, .f2Size = {0.01f, 0.0f}, .fTextSize = 0.5f, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor}), }),
				RadioButtons<float>({U"1/16", U"1/8", U"1/4"}, {.flags = WidgetFlags::kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gWorldDetail}),
				Spacer(),
			#if defined(BT_DEBUG)
				Slider(U"TERRAIN ELEVATION TEXTURE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainElevationTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN COLOR TEXTURE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainColorTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN NORMAL TEXTURE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainNormalTextureMultiplier}),
				Spacer(),
				Slider(U"TERRAIN AMBIENT OCCLUSION TEXTURE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainAmbientOcclusionTextureMultiplier}),
				Spacer(),
			#endif
				Toggle(U"SMOKE", {.pWrapper = &gSmoke}),
				Slider(U"SMOKE PIXELS", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeSimulationPixels}),
				Spacer(),
				Slider(U"SMOKE AREA", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeSimulationArea}),
				Spacer(),
			}),
		}),
	});
}

Widget SoundMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kSound; }},
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
			Button(kStringDefaults, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = 0.5f, .uiTextColor = kuiMainMenuButtonTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
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
#define ENABLE_WATER_MEDIUM_TWEAKS
// #define ENABLE_LIGHTING_TWEAKS
// #define ENABLE_WATER_LIGHTING_TWEAKS
// #define ENABLE_SHADOW_TWEAKS
// #define ENABLE_MISC_TWEAKS
// #define ENABLE_HEX_SHIELD_TWEAKS
// #define ENABLE_TURRET_TWEAKS
// #define ENABLE_SMOKE_TWEAKS

#if defined(ENABLE_TEST)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TEST ONE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTestOne}),
			Spacer(),
			Slider(U"TEST TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTestTwo}),
			Spacer(),
		}),
	});
}
#elif defined(ENABLE_GLTF_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"GLTF EXPOSURE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfExposuse}),
			Spacer(),
			Slider(U"GLTF GAMMA", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfGamma}),
			Spacer(),
			Slider(U"GLTF AMBIENT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfIblAmbient}),
			Spacer(),
			Slider(U"GLTF DIFFUSE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfDiffuse}),
			Spacer(),
			Slider(U"GLTF SPECULAR", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfSpecular}),
			Spacer(),
			Spacer(),
			Slider(U"GLTF SMOKE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfSmoke}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"BRDF", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfBrdf}),
			Spacer(),
			Slider(U"BRDF POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfBrdfPower}),
			Spacer(),
			Slider(U"IBL", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfIbl}),
			Spacer(),
			Slider(U"IBL POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfIblPower}),
			Spacer(),
			Slider(U"SUN", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfSun}),
			Spacer(),
			Slider(U"SUN POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfSunPower}),
			Spacer(),
			Slider(U"LIGHTING", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfLighting}),
			Spacer(),
			Slider(U"LIGHTING POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gGltfLightingPower}),
			Spacer(),
		}),
	});
};
#endif

#if defined(ENABLE_TERRAIN_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"SNOW MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainSnowMultiplier}),
			Spacer(),
			Slider(U"BEACH HEIGHT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachHeight}),
			Spacer(),
			Slider(U"BEACH SAND SIZE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachSandSize}),
			Spacer(),
			Slider(U"BEACH SAND BLEND", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachSandBlend}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE ONE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeOne}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeTwo}),
			Spacer(),
			Slider(U"BEACH NORMALS SIZE THREE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachNormalsSizeThree}),
			Spacer(),
			Slider(U"BEACH NORMALS BLEND", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainBeachNormalsBlend}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ISLAND HEIGHT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gIslandHeight}),
			Spacer(),
			Slider(U"ROCK MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockMultiplier}),
			Spacer(),
			Slider(U"ROCK SAND SIZE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockSize}),
			Spacer(),
			Slider(U"ROCK SAND BLEND", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockBlend}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE ONE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeOne}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeTwo}),
			Spacer(),
			Slider(U"ROCK NORMALS SIZE THREE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockNormalsSizeThree}),
			Spacer(),
			Slider(U"ROCK NORMALS BLEND", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTerrainRockNormalsBlend}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_SPECULAR_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SIZE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingSampledNormalsSize}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SIZE MOD", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingSampledNormalsSizeMod}),
			Spacer(),
			Slider(U"SAMPLED NORMALS SPEED", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingSampledNormalsSpeed}),
			Spacer(),
			Slider(U"DEPTH REFLECTION FEATHER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterDepthReflectionFeather}),
			Spacer(),
			Slider(U"SUN BIAS", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxSunBias}),
			Spacer(),
			Slider(U"NORMAL SOFTEN", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxNormalSoften}),
			Spacer(),
			Slider(U"NORMAL BLEND WAVE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxNormalBlendWave}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"INTENSITY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxIntensity}),
			Spacer(),
			Slider(U"ADD", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxAdd}),
			Spacer(),
			Slider(U"SKYBOX 1", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxOne}),
			Spacer(),
			Slider(U"SKYBOX POWER 1", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxOnePower}),
			Spacer(),
			Slider(U"SKYBOX TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxTwo}),
			Spacer(),
			Slider(U"SKYBOX POWER TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxTwoPower}),
			Spacer(),
			Slider(U"SKYBOX THREE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxThree}),
			Spacer(),
			Slider(U"SKYBOX POWER THREE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSkyboxThreePower}),
			Spacer(),
			Slider(U"HEIGHT DARKEN TOP", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterHeightDarkenTop}),
			Spacer(),
			Slider(U"HEIGHT DARKEN BOTTOM", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterHeightDarkenBottom}),
			Spacer(),
			Slider(U"HEIGHT DARKEN CLAMP", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterHeightDarkenClamp}),
			Spacer(),
		}),
	});
};
#endif

#if defined(ENABLE_WATER_LOW_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			RadioButtons<int64_t>({U"15", U"31", U"63", U"127", U"255"}, {.flags = WidgetFlags::kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gLowCount}),
			Spacer(),
			Slider(U"LOW MAX", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowMax}),
			Spacer(),
			Slider(U"ANGLE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowAngle}),
			Spacer(),
			Slider(U"WAVELENGTH", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowWavelength}),
			Spacer(),
			Slider(U"AMPLITUDE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowAmplitude}),
			Spacer(),
			Slider(U"SPEED", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowSpeed}),
			Spacer(),
			Slider(U"STEEPNESS", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowSteepness}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ANGLE ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowAngleAdjust}),
			Spacer(),
			Slider(U"WAVELENGTH ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowWavelengthAdjust}),
			Spacer(),
			Slider(U"AMPLITUDE ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowAmplitudeAdjust}),
			Spacer(),
			Slider(U"SPEED ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLowSpeedAdjust}),
			Spacer(),
			Spacer(),
			Slider(U"BEACH DIRECTIONAL FADE BOTTOM", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gBeachDirectionalFadeBottom}),
			Spacer(),
			Slider(U"BEACH DIRECTIONAL FADE HEIGHT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gBeachDirectionalFadeHeight}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_MEDIUM_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			RadioButtons<int64_t>({U"15", U"31", U"63", U"127", U"255"}, {.flags = WidgetFlags::kMatchTextWidth, .f2Size = {0.01f, kfSliderToggleHeight}, .pWrapper = &gMediumCount}),
			Spacer(),
			Slider(U"WAVELENGTH", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumWavelength}),
			Spacer(),
			Slider(U"AMPLITUDE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumAmplitude}),
			Spacer(),
			Slider(U"SPEED", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumSpeed}),
			Spacer(),
			Slider(U"STEEPNESS", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumSteepness}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ANGLE ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumAngleAdjust}),
			Spacer(),
			Slider(U"WAVELENGTH ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumWavelengthAdjust}),
			Spacer(),
			Slider(U"AMPLITUDE ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumAmplitudeAdjust}),
			Spacer(),
			Slider(U"SPEED ADJUST", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMediumSpeedAdjust}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_LIGHTING_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"TEXTURE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingTextureMultiplier}),
			Spacer(),

			Slider(U"BLUR DISTANCE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurDistance}),
			Spacer(),
			Slider(U"BLUR DIRECTIONALITY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurDirectionality}),
			Spacer(),
			Slider(U"BLUR JITTER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurJitter}),
			Spacer(),
			Slider(U"DOWNSCALE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurDownscale}),
			Spacer(),
			Slider(U"COMBINE INDEX", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingCombineIndex}),
			Spacer(),
			Slider(U"BLUR FIRST DIVISOR", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurFirstDivisor}),
			Spacer(),
			Slider(U"BLUR DIVISOR", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingBlurDivisor}),
			Spacer(),
			Slider(U"COMBINE DECAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingCombineDecay}),
			Spacer(),

			Slider(U"COMBINE POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingCombinePower}),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"DIRECTIONAL", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingDirectional}),
			Spacer(),
			Slider(U"INDIRECT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingIndirect}),
			Spacer(),
			Slider(U"TERRAIN", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingTerrain}),
			Spacer(),
			Slider(U"TERRAIN ADD", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingAddTerrain}),
			Spacer(),
			Slider(U"OBJECTS", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingObjects}),
			Spacer(),
			Slider(U"OBJECTS ADD", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingObjectsAdd}),
			Spacer(),
			Slider(U"TIME OF DAY MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingTimeOfDayMultiplier}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_WATER_LIGHTING_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
		}),
		VStack({},
		{
			Slider(U"NormalSoften", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularNormalSoften}),
			Spacer(),
			Slider(U"NormalBlendWave", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularNormalBlendWave}),
			Spacer(),
			Slider(U"Diffuse", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularDiffuse}),
			Spacer(),
			Slider(U"Direct", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularDirect}),
			Spacer(),
			Slider(U"Specular", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecular}),
			Spacer(),
			Slider(U"SpecularIntensity", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularIntensity}),
			Spacer(),
			Slider(U"SpecularAdd", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularAdd}),
			Spacer(),
			Slider(U"SpecularOne", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularOne}),
			Spacer(),
			Slider(U"SpecularTwo", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularTwo}),
			Spacer(),
			Slider(U"SpecularThree", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gLightingWaterSpecularThree}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_SHADOW_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"FEATHER NOON", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowFeatherNoon}),
			Spacer(),
			Slider(U"FEATHER NOON OFFSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowFeatherNoonOffset}),
			Spacer(),
			Slider(U"FEATHER SUNSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowFeatherSunset}),
			Spacer(),
			Slider(U"FEATHER SUNSET OFFSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowFeatherSunsetOffset}),
			Spacer(),
			Slider(U"FEATHER POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowFeatherPower}),
			Spacer(),
			Slider(U"DISTANCE FALLOFF", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowDistanceFallof}),
			Spacer(),
			Slider(U"BLUR SIGMA", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowBlurSigma}),
			Spacer(),
			Slider(U"AFFECT AMBIENT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowAffectAmbient}),
			Spacer(),
			Slider(U"HEIGHT FADE TOP", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowHeightFadeTop}),
			Spacer(),
			Slider(U"HEIGHT FADE BOTTOM", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gShadowHeightFadeBottom}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Spacer(),
			Slider(U"OBJECT SHADOW RENDER MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsRenderMultiplier}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsBlurMultiplier}),
			Spacer(),
			Slider(U"OBJECT SHADOW NOON", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsNoon}),
			Spacer(),
			Slider(U"OBJECT SHADOW SUNSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsSunset}),
			Spacer(),
			Slider(U"OBJECT SHADOW SUNSET STRETCH", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsSunsetStretch}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR DISTANCE NOON", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsBlurDistanceNoon}),
			Spacer(),
			Slider(U"OBJECT SHADOW BLUR DISTANCE SUNSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gObjectShadowsBlurDistanceSunset}),
			Spacer(),
			Slider(U"SMOKE SHADOW INTENSITY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeShadowIntensity}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_MISC_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"MISC0", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gMisc0}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"ISLAND HEIGHT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gIslandHeight}),
			Spacer(),
			Slider(U"WATER DEPTH", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterDepth}),
			Spacer(),
			Slider(U"WATER TERRAIN HEIGHT", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterTerrainHeight}),
			Spacer(),
			Slider(U"WATER TERRAIN FADE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterTerrainFade}),
			Spacer(),
			Slider(U"DEPTH REFLECTION FEATHER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gWaterDepthReflectionFeather}),
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_HEX_SHIELD_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"GROW", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldGrow}),
			Spacer(),
			Slider(U"EDGE DISTANCE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldEdgeDistance}),
			Spacer(),
			Slider(U"EDGE POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldEdgePower}),
			Spacer(),
			Slider(U"EDGE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldEdgeMultiplier}),
			Spacer(),
			Spacer(),
			Slider(U"WAVE MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldWaveMultiplier}),
			Spacer(),
			Slider(U"WAVE DOT MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldWaveDotMultiplier}),
			Spacer(),
			Slider(U"WAVE INTENSITY MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldWaveIntensityMultiplier}),
			Spacer(),
			Slider(U"WAVE INTENSITY POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldWaveIntensityPower}),
			Spacer(),
			Slider(U"WAVE FALLOFF POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldWaveFalloffPower}),
			Spacer(),
			Spacer(),
			Slider(U"DIRECTION FALLOFF POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldDirectionFalloffPower}),
			Spacer(),
			Slider(U"DIRECTION MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gHexShieldDirectionMultiplier}),
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
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"FIRING OFFSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTurretFiringOffset}),
			Spacer(),
			Slider(U"FIRING Z OFFSET", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gTurretFiringZOffset}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
		}),
	});
}
#endif

#if defined(ENABLE_SMOKE_TWEAKS)
Widget TweaksMenu()
{
	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kTweaks && !gpGame->InMainMenu(); }},
	{
		VStack({},
		{
			Spacer(),
			Slider(U"TIME OF DAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSunAngleOverride}),
			Spacer(),
			Slider(U"SMOKE MAX", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeMax}),
			Spacer(),
			Slider(U"SMOKE POWER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokePower}),
			Spacer(),
			Slider(U"SMOKE DECAY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeDecay}),
			Spacer(),
			Slider(U"SMOKE DECAY EXTRA", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeDecayExtra}),
			Spacer(),
			Slider(U"SMOKE DECAY EXTRA THRESHOLD", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeDecayExtraThreshold}),
			Spacer(),
			Slider(U"SMOKE EDGE DECAY DISTANCE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeEdgeDecayDistance}),
			Spacer(),
		}),
		VStack({},
		{
			Spacer(),
			Slider(U"SMOKE COLOR MIN", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeColorMin}),
			Spacer(),
			Slider(U"SMOKE COLOR MULTIPLIER", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeColorMultiplier}),
			Spacer(),
			Slider(U"SMOKE TRAILS FALLOFF", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeTrailsFalloff}),
			Spacer(),
			Slider(U"SMOKE WIND NOISE SCALE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeWindNoiseScale}),
			Spacer(),
			Slider(U"SMOKE WIND NOISE QUANTITY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeWindNoiseQuantity}),
			Spacer(),
			Slider(U"SMOKE NOISE QUANTITY", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeNoiseQuantity}),
			Spacer(),
			Slider(U"SMOKE NOISE SCALE ONE", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeNoiseScaleOne}),
			Spacer(),
			Slider(U"SMOKE NOISE SCALE TWO", {.flags = WidgetFlags::kCaptureHides, .pWrapper = &gSmokeNoiseScaleTwo}),
			Spacer(),
		}),
	});
}
#endif

#endif // ENABLE_DEBUG_INPUT

#if defined(BT_DEBUG)
Widget InGameDebug()
{
	return VStack({.flags = {WidgetFlags::kExcludeFromLayout}, .f2Size = {0.5f, 0.5f}},
	{
		Text({.flags = {WidgetFlags::kCenterHorizontal}, .fTextSize = 0.125f, .fShadowOffset = 0.025f, .uiShadowColor = 0x000000AA,
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
	return VStack({.flags = {WidgetFlags::kExcludeFromLayout}, .Enabled = []() { return false; }}, {});
}

constexpr float kfShieldWidthPerPoint = kfUiScale * 0.002f;
constexpr float kfArmorWidthPerPoint = kfUiScale * 0.004f;

float ShieldWidth()
{
	return 0.001f; // std::max(kfShieldWidthPerPoint * gpGame->CurrentFrame().interpolate.player.fShield, 0.001f);
}

float ArmorWidth()
{
	return 0.001f; // std::max(kfArmorWidthPerPoint * gpGame->CurrentFrame().interpolate.player.fArmor, 0.001f);
}

float ShieldArmorMaxWidth()
{
	return 0.001f; // std::max(std::max(kfShieldWidthPerPoint * Player::MaxShield(gpGame->CurrentFrame()), kfArmorWidthPerPoint * Player::MaxArmor(gpGame->CurrentFrame())), 0.001f);
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

	return VStack({.Enabled = []() { return gpGame->meUiState == UiState::kNone &&
	                                        !(gpGame->CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen); }},
	{
		Spacer(),
		HStack({.f2Size = {0.0f, 2.0f * kfShieldArmorContainerHeight}},
		{
			Spacer(),
			Rotary({.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kBackgroundTexture}, .f2Size = {kfIconSize / gpSwapchainManager->mfAspectRatio, kfIconSize},
			.BackgroundTexture = []()
			{
				return data::kTexturesUiBC7EnergyIconpngCrc;
			},
			.RotaryInfo = []()
			{
				// return RotaryInfo {static_cast<int64_t>(kfEnergyDotCount), static_cast<int64_t>((gpGame->CurrentFrame().interpolate.player.fEnergy / Player::MaxEnergy(gpGame->CurrentFrame())) * kfEnergyDotCount), kfEnergyDotDistance, kfEnergyDotSize, 0x44EEFFFF, 0x00000000};
				return RotaryInfo {static_cast<int64_t>(kfEnergyDotCount), 1, kfEnergyDotDistance, kfEnergyDotSize, 0x44EEFFFF, 0x00000000};
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
					HStack({.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kCenterVertical},
					.Size = []()
					{
						// return XMFLOAT2 {kfShieldWidthPerPoint * Player::MaxShield(gpGame->CurrentFrame()), kfShieldArmorHeight};
						return XMFLOAT2 {kfShieldWidthPerPoint * 1.0f, kfShieldArmorHeight};
					}},
					{
						Spacer({.flags = {WidgetFlags::kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
						Spacer(),
						Spacer({.flags = {WidgetFlags::kBackground}, .uiBackground = 0x0088FFFF,
						.Size = []()
						{
							return XMFLOAT2 {ShieldWidth() - kfShieldArmorMaxWidth, kfShieldArmorHeight};
						}}),
						Spacer(),
						Spacer({.flags = {WidgetFlags::kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
					}),
					Spacer({.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kCenterVertical, WidgetFlags::kBackgroundTexture},
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
					HStack({.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kCenterVertical},
					.Size = []()
					{
						// return XMFLOAT2 {kfArmorWidthPerPoint * Player::MaxArmor(gpGame->CurrentFrame()), kfShieldArmorHeight};
						return XMFLOAT2 {kfArmorWidthPerPoint * 1.0f, kfShieldArmorHeight};
					}},
					{
						Spacer({.flags = {WidgetFlags::kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
						Spacer(),
						Spacer({.flags = {WidgetFlags::kBackground}, .uiBackground = 0xFF2222FF,
						.Size = []()
						{
							return XMFLOAT2 {ArmorWidth() - kfShieldArmorMaxWidth, kfShieldArmorHeight};
						}}),
						Spacer(),
						Spacer({.flags = {WidgetFlags::kBackground}, .f2Size = {kfShieldArmorMaxWidth, 0.0f}, .uiBackground = 0xFFFFFFFF}),
					}),
					Spacer({.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kCenterVertical, WidgetFlags::kBackgroundTexture}, .f2Size = {kfIconSize / gpSwapchainManager->mfAspectRatio, kfIconSize},
					.BackgroundTexture = []()
					{
						return data::kTexturesUiBC7ArmorIconpngCrc;
					}}),
				}),
			}),
			Spacer({.f2Size = {kfMidPadding, 0.0f}}),
			Rotary({.flags = {WidgetFlags::kCenterVertical, WidgetFlags::kBackgroundTexture}, .f2Size = {kfSecondaryIconSize / gpSwapchainManager->mfAspectRatio, kfSecondaryIconSize},
			.BackgroundTexture = []()
			{
				return data::kTexturesUiBC7MissileIconpngCrc;
			},
			.RotaryInfo = []()
			{
				// auto [iCurrent, iCapacity] = Player::SecondaryCapacity(gpGame->CurrentFrame());
				// return RotaryInfo {iCurrent, iCapacity, kfSecondaryDotDistance, kfSecondaryDotSize, 0xFF6F0FFF, 0x999999EE};
				return RotaryInfo {1, 1, kfSecondaryDotDistance, kfSecondaryDotSize, 0xFF6F0FFF, 0x999999EE};
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

	return HStack({.Enabled = []() { return gpGame->meUiState == UiState::kNone && (gpGame->CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen); }},
	{
		VStack({},
		{
			Spacer({.f2Size = {0.0f, 0.1f}}),
			Text({.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kMatchTextWidth}, .f2Size = {0.1f, 0.1f}, .uiTextColor = kuiMainMenuTitleTextColor, .fShadowOffset = kfShadowOffset, .uiShadowColor = 0x000000AA,
			.Text = []()
			{
				return TranslatedString(kStringGameOver);
			}}),
			Spacer({.f2Size = {0.0f, 0.05f}}),
			Button(kStringRestart, {.flags = {WidgetFlags::kCenterHorizontal, WidgetFlags::kMatchTextWidth, WidgetFlags::kFocusBackground}, .f2Size = kf2MainMenuButtonSize, .uiBackground = kuiMainMenuButtonBackgroundColor, .fTextSize = kfMainMenuButtonTextSize, .uiTextColor = kuiDefaultTextColor, .fShadowOffset = kfDefaultShadowOffset, .uiShadowColor = kuiDefaultShadowColor,
			.OnClick = [](XMFLOAT2)
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
