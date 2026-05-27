#include "ClientSettings.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
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

struct GraphicsSettings
{
	static constexpr int64_t kiVersion = 6;

	bool bFullscreen = false;
	VkPresentModeKHR ePresentMode = VK_PRESENT_MODE_FIFO_KHR;
	bool bMultisampling = false;
	VkSampleCountFlagBits eSampleCount = VK_SAMPLE_COUNT_4_BIT;
	bool bAnisotropy = false;
	float fMaxAnisotropy = 0.0f;
	bool bSampleShading = false;
	float fMinSampleShading = 0.0f;
	float fMipLodBias = 0.0f;
	float fWaterShapeDetail = 0.0f;
	bool bSmoke = false;
	float fSmokeSimulationPixels = 0.0f;
	float fSmokeSimulationArea = 0.0f;
	float fMinimumAmbient = 0.0f;
	bool bWind = false;
	bool bOpaqueUi = false;
	float fUiOpacity = 0.9f;
	float fUiFontScale = 1.0f;
};
static constexpr char kpcGraphicsSettingsPath[] = "GraphicsSettings.bin";

void SaveGraphicsSettings()
{
	// Heap: file I/O allocates
	ScopedSuppressAllocationTracking suppress;

	GraphicsSettings graphicsSettings
	{
		.bFullscreen = engine::gFullscreen.Get<bool>(),
		.ePresentMode = engine::gPresentMode.Get<VkPresentModeKHR>(),
		.bMultisampling = engine::gMultisampling.Get<bool>(),
		.eSampleCount = engine::gSampleCount.Get<VkSampleCountFlagBits>(),
		.bAnisotropy = engine::gAnisotropy.Get<bool>(),
		.fMaxAnisotropy = engine::gMaxAnisotropy.Get(),
		.bSampleShading = engine::gSampleShading.Get<bool>(),
		.fMinSampleShading = engine::gMinSampleShading.Get(),
		.fMipLodBias = engine::gMipLodBias.Get(),
		.fWaterShapeDetail = engine::gWaterShapeDetail.Get(),
		.bSmoke = engine::gSmokeEnabled.Get<bool>(),
		.fSmokeSimulationPixels = engine::gSmokeSimulationPixels.Get(),
		.fSmokeSimulationArea = engine::gSmokeSimulationArea.Get(),
		.fMinimumAmbient = engine::gSunMoonMinimumAmbient.Get(),
		.bWind = engine::gWindEnabled.Get<bool>(),
		.bOpaqueUi = engine::gOpaqueUi.Get<bool>(),
		.fUiOpacity = engine::gUiOpacity.Get(),
		.fUiFontScale = engine::gUiFontScale.Get(),
	};

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcGraphicsSettingsPath, graphicsSettings);
}

bool LoadGraphicsSettings()
{
	GraphicsSettings graphicsSettings {};

	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcGraphicsSettingsPath, graphicsSettings))
	{
		engine::gFullscreen.Set(graphicsSettings.bFullscreen);
		engine::gPresentMode.Set<VkPresentModeKHR>(graphicsSettings.ePresentMode);
		engine::gMultisampling.Set(graphicsSettings.bMultisampling);
		engine::gSampleCount.Set<VkSampleCountFlagBits>(graphicsSettings.eSampleCount);
		engine::gAnisotropy.Set(graphicsSettings.bAnisotropy);
		engine::gMaxAnisotropy.Set(graphicsSettings.fMaxAnisotropy);
		engine::gSampleShading.Set(graphicsSettings.bSampleShading);
		engine::gMinSampleShading.Set(graphicsSettings.fMinSampleShading);
		engine::gMipLodBias.Set(graphicsSettings.fMipLodBias);
		engine::gWaterShapeDetail.Set(graphicsSettings.fWaterShapeDetail);
		engine::gSmokeEnabled.Set(graphicsSettings.bSmoke);
		engine::gSmokeSimulationPixels.Set(graphicsSettings.fSmokeSimulationPixels);
		engine::gSmokeSimulationArea.Set(graphicsSettings.fSmokeSimulationArea);
		engine::gSunMoonMinimumAmbient.Set(graphicsSettings.fMinimumAmbient);
		engine::gWindEnabled.Set(graphicsSettings.bWind);
		engine::gOpaqueUi.Set(graphicsSettings.bOpaqueUi);
		engine::gUiOpacity.Set(graphicsSettings.fUiOpacity);
		engine::gUiFontScale.Set(graphicsSettings.fUiFontScale);
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
	engine::gWindEnabled.ResetToDefault();
	engine::gOpaqueUi.ResetToDefault();
	engine::gUiOpacity.ResetToDefault();
	engine::gUiFontScale.ResetToDefault();

	SaveGraphicsSettings();
}

struct TweaksSettings
{
	static constexpr int64_t kiVersion = 12;

	bool bShowImGui = false;
	bool bSectionVisible[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionX[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionY[static_cast<size_t>(engine::TweakSection::kCount)] {};
	int8_t iActiveSubtab[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fSunAngle = 1.15f;
	bool bSectionCollapsed[static_cast<size_t>(engine::TweakSection::kCount)] {};
};
static constexpr char kpcTweaksSettingsPath[] = "TweaksSettings.bin";

void SaveTweaksSettings()
{
	if constexpr (!kbDebugInput)
	{
		return;
	}

	bool bSectionVisible[static_cast<size_t>(engine::TweakSection::kCount)] {};
	ImVec2 f2WindowPositions[static_cast<size_t>(engine::TweakSection::kCount)] {};
	int8_t iActiveSubtab[static_cast<size_t>(engine::TweakSection::kCount)] {};
	bool bSectionCollapsed[static_cast<size_t>(engine::TweakSection::kCount)] {};
	engine::gpImGuiManager->mpTweaksScreen->SaveState(bSectionVisible, f2WindowPositions, iActiveSubtab, bSectionCollapsed);

	TweaksSettings settings {};
	settings.bShowImGui = gpGame->mbShowImGui;
	for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
	{
		settings.bSectionVisible[i] = bSectionVisible[i];
		settings.fWindowPositionX[i] = f2WindowPositions[i].x;
		settings.fWindowPositionY[i] = f2WindowPositions[i].y;
		settings.iActiveSubtab[i] = iActiveSubtab[i];
		settings.bSectionCollapsed[i] = bSectionCollapsed[i];
	}
	settings.fSunAngle = engine::gSunAngleOverride.Get();

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

		ImVec2 f2WindowPositions[static_cast<size_t>(engine::TweakSection::kCount)] {};
		for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
		{
			f2WindowPositions[i] = {settings.fWindowPositionX[i], settings.fWindowPositionY[i]};
		}

		engine::gpImGuiManager->mpTweaksScreen->LoadState(settings.bSectionVisible, f2WindowPositions, settings.iActiveSubtab, settings.bSectionCollapsed);

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
	float fCameraEyeHeightTarget = 198.0f; // matches Camera::kfCameraEyeHeightInitial
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
