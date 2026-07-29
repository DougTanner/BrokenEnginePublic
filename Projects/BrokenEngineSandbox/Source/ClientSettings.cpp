#include "ClientSettings.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/SoundSettingsWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

namespace game
{

struct SoundSettings
{
	static constexpr int64_t kiVersion = 2;

	float fMasterVolume = 0.0f;
	float fMusicVolume = 0.0f;
	float fSoundVolume = 0.0f;
};
static constexpr char kpcSoundSettingsPath[] = "SoundSettings.bin";

void SaveSoundSettings()
{
	SoundSettings soundSettings
	{
		.fMasterVolume = engine::gMasterVolume.Get(),
		.fMusicVolume = engine::gMusicVolume.Get(),
		.fSoundVolume = engine::gSoundVolume.Get(),
	};

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcSoundSettingsPath, soundSettings);
}

void LoadSoundSettings()
{
	SoundSettings soundSettings {};

	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcSoundSettingsPath, soundSettings))
	{
		engine::gMasterVolume.Set(soundSettings.fMasterVolume);
		engine::gMusicVolume.Set(soundSettings.fMusicVolume);
		engine::gSoundVolume.Set(soundSettings.fSoundVolume);
	}

	if constexpr (kbRecording)
	{
		engine::gMusicVolume.Set(0.0f);
	}
}

void ResetSoundSettings()
{
	engine::gMasterVolume.ResetToDefault();
	engine::gMusicVolume.ResetToDefault();
	engine::gSoundVolume.ResetToDefault();

	SaveSoundSettings();
}

enum class GraphicsSettingsFlags : uint8_t
{
	kFullscreen    = 1 << 0,
	kMultisampling = 1 << 1,
	kAnisotropy    = 1 << 2,
	kSampleShading = 1 << 3,
	kSmoke         = 1 << 4,
	kWind          = 1 << 5,
	kOpaqueUi      = 1 << 6,
};

struct GraphicsSettings
{
	static constexpr int64_t kiVersion = 9;

	common::Flags<GraphicsSettingsFlags> flags {};
	uint8_t uiPad[3] {};
	VkPresentModeKHR ePresentMode = VK_PRESENT_MODE_FIFO_KHR;
	VkSampleCountFlagBits eSampleCount = VK_SAMPLE_COUNT_4_BIT;
	float fMaxAnisotropy = 0.0f;
	float fMinSampleShading = 0.0f;
	float fMipLodBias = 0.0f;
	float fWaterShapeDetail = 0.0f;
	float fSmokeSimulationPixels = 0.0f;
	float fSmokeSimulationArea = 0.0f;
	float fMinimumAmbient = 0.0f;
	float fLightingUpdateCadence = 1.0f;
	float fUiOpacity = 0.9f;
	float fUiFontScale = 1.0f;
	engine::UiTheme eUiTheme = engine::UiTheme::kNavalSteel;
	uint8_t uiTrailingPad[3] {};
};
static constexpr char kpcGraphicsSettingsPath[] = "GraphicsSettings.bin";

void SaveGraphicsSettings()
{
	// Heap: file I/O allocates
	ScopedSuppressAllocationTracking suppress;

	GraphicsSettings graphicsSettings
	{
		.ePresentMode = engine::gPresentMode.Get<VkPresentModeKHR>(),
		.eSampleCount = engine::gSampleCount.Get<VkSampleCountFlagBits>(),
		.fMaxAnisotropy = engine::gMaxAnisotropy.Get(),
		.fMinSampleShading = engine::gMinSampleShading.Get(),
		.fMipLodBias = engine::gMipLodBias.Get(),
		.fWaterShapeDetail = engine::gWaterShapeDetail.Get(),
		.fSmokeSimulationPixels = engine::gSmokeSimulationPixels.Get(),
		.fSmokeSimulationArea = engine::gSmokeSimulationArea.Get(),
		.fMinimumAmbient = engine::gSunMoonMinimumAmbient.Get(),
		.fLightingUpdateCadence = engine::gLightingUpdateCadence.Get(),
		.fUiOpacity = engine::gUiOpacity.Get(),
		.fUiFontScale = engine::gUiFontScale.Get(),
		.eUiTheme = engine::GetUiTheme(),
	};

	graphicsSettings.flags.Set(GraphicsSettingsFlags::kFullscreen, engine::gFullscreen.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kMultisampling, engine::gMultisampling.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kAnisotropy, engine::gAnisotropy.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kSampleShading, engine::gSampleShading.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kSmoke, engine::gSmokeEnabled.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kWind, engine::gWindEnabled.Get<bool>());
	graphicsSettings.flags.Set(GraphicsSettingsFlags::kOpaqueUi, engine::gOpaqueUi.Get<bool>());

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcGraphicsSettingsPath, graphicsSettings);
}

bool LoadGraphicsSettings()
{
	GraphicsSettings graphicsSettings {};

	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcGraphicsSettingsPath, graphicsSettings))
	{
		engine::gFullscreen.Set(graphicsSettings.flags & GraphicsSettingsFlags::kFullscreen);
		engine::gPresentMode.Set<VkPresentModeKHR>(graphicsSettings.ePresentMode);
		engine::gMultisampling.Set(graphicsSettings.flags & GraphicsSettingsFlags::kMultisampling);
		engine::gSampleCount.Set<VkSampleCountFlagBits>(graphicsSettings.eSampleCount);
		engine::gAnisotropy.Set(graphicsSettings.flags & GraphicsSettingsFlags::kAnisotropy);
		engine::gMaxAnisotropy.Set(graphicsSettings.fMaxAnisotropy);
		engine::gSampleShading.Set(graphicsSettings.flags & GraphicsSettingsFlags::kSampleShading);
		engine::gMinSampleShading.Set(graphicsSettings.fMinSampleShading);
		engine::gMipLodBias.Set(graphicsSettings.fMipLodBias);
		engine::gWaterShapeDetail.Set(graphicsSettings.fWaterShapeDetail);
		engine::gSmokeEnabled.Set(graphicsSettings.flags & GraphicsSettingsFlags::kSmoke);
		engine::gSmokeSimulationPixels.Set(graphicsSettings.fSmokeSimulationPixels);
		engine::gSmokeSimulationArea.Set(graphicsSettings.fSmokeSimulationArea);
		engine::gSunMoonMinimumAmbient.Set(graphicsSettings.fMinimumAmbient);
		if (std::isfinite(graphicsSettings.fLightingUpdateCadence))
		{
			engine::gLightingUpdateCadence.Set(graphicsSettings.fLightingUpdateCadence);
		}
		else
		{
			engine::gLightingUpdateCadence.ResetToDefault();
		}
		engine::gWindEnabled.Set(graphicsSettings.flags & GraphicsSettingsFlags::kWind);
		engine::gOpaqueUi.Set(graphicsSettings.flags & GraphicsSettingsFlags::kOpaqueUi);
		engine::gUiOpacity.Set(graphicsSettings.fUiOpacity);
		engine::gUiFontScale.Set(graphicsSettings.fUiFontScale);
		engine::gUiTheme.Set<engine::UiTheme>(graphicsSettings.eUiTheme);
		return true;
	}

	return false;
}

void ResetGraphicsSettings()
{
	engine::gFullscreen.ResetToDefault();
	engine::gPresentMode.ResetToDefault();
	engine::gMultisampling.ResetToDefault();
	engine::gSampleCount.ResetToDefault();
	engine::gAnisotropy.ResetToDefault();
	engine::gMaxAnisotropy.ResetToDefault();
	engine::gSampleShading.ResetToDefault();
	engine::gMinSampleShading.ResetToDefault();
	engine::gMipLodBias.ResetToDefault();
	engine::gWaterShapeDetail.ResetToDefault();
	engine::gSmokeEnabled.ResetToDefault();
	engine::gSmokeSimulationPixels.ResetToDefault();
	engine::gSmokeSimulationArea.ResetToDefault();
	engine::gSunMoonMinimumAmbient.ResetToDefault();
	engine::gLightingUpdateCadence.ResetToDefault();
	engine::gWindEnabled.ResetToDefault();
	engine::gOpaqueUi.ResetToDefault();
	engine::gUiOpacity.ResetToDefault();
	engine::gUiFontScale.ResetToDefault();
	engine::gUiTheme.ResetToDefault();

	SaveGraphicsSettings();
}

struct TweaksSettings
{
	static constexpr int64_t kiVersion = 14;

	bool bShowImGui = false;
	uint8_t uiPad[3] {};
	float fSunAngle = 1.15f;
	// Engine-owned layout POD, embedded by value: one array bound and one sizeof for the whole program.
	engine::TweakSectionState sectionState {};
};
static_assert(std::is_trivially_copyable_v<TweaksSettings>);
static_assert(sizeof(TweaksSettings) == 304, "kiVersion must be bumped with this layout");
static constexpr char kpcTweaksSettingsPath[] = "TweaksSettings.bin";

void SaveTweaksSettings()
{
	if constexpr (!kbDebugInput)
	{
		return;
	}

	TweaksSettings settings {};
	settings.bShowImGui = gpGame->mbShowImGui;
	settings.fSunAngle = engine::gSunAngleOverride.Get();
	engine::gpImGuiManager->mpTweaksScreen->SaveState(settings.sectionState);

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcTweaksSettingsPath, settings);
}

void LoadTweaksSettings()
{
	if constexpr (!kbDebugInput)
	{
		return;
	}

	TweaksSettings settings {};
	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcTweaksSettingsPath, settings))
	{
		gpGame->mbShowImGui = settings.bShowImGui;
		engine::gpImGuiManager->mpTweaksScreen->LoadState(settings.sectionState);
		engine::gSunAngleOverride.Set(settings.fSunAngle);
	}
	else
	{
		LOG(kDefault, kWarning, "LoadTweaks FAILED to read file");
	}
}

struct ClientStateSettings
{
	static constexpr int64_t kiVersion = 3;

	game::FleetGuid fleetGuid {};
	int64_t iFocusedShipId = 0;
	float fCameraEyeHeightTarget = game::Camera::kfCameraEyeHeightInitial;
	uint8_t uiPad[4] {};
};
static constexpr char kpcClientStatePath[] = "ClientState.bin";

void SaveClientState()
{
	// Heap: engine::WriteVersionedFile file I/O
	ScopedSuppressAllocationTracking suppress;

	ClientStateSettings settings
	{
		.fleetGuid              = gpGame->mRememberedFleetGuid,
		.iFocusedShipId         = gpGame->mRememberedFocusedShipId.iValue,
		.fCameraEyeHeightTarget = gpGame->mfRememberedCameraEyeHeightTarget,
	};
	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcClientStatePath, settings);
}

void LoadClientState()
{
	// Heap: engine::ReadVersionedFile file I/O
	ScopedSuppressAllocationTracking suppress;

	ClientStateSettings settings {};
	if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcClientStatePath, settings))
	{
		return;
	}

	gpGame->mRememberedFleetGuid = settings.fleetGuid;
	gpGame->mRememberedFocusedShipId = engine::global_id_t {settings.iFocusedShipId};
	gpGame->mfRememberedCameraEyeHeightTarget = settings.fCameraEyeHeightTarget;

	// Apply zoom directly so the camera starts AT the saved zoom rather than easing from the default.
	gpCamera->mfCameraEyeHeight = settings.fCameraEyeHeightTarget;
	gpCamera->mfCameraEyeHeightTarget = settings.fCameraEyeHeightTarget;
}

} // namespace game

#endif // BT_CLIENT
