#if defined(BT_CLIENT)

#include "GraphicsQualityWrappers.h"

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

namespace game
{

// int64_t 0/1/2 == GraphicsQualityLevel kLow/kMedium/kHigh
engine::Wrapper gTerrainShadowsLevel(int64_t {0}, std::vector<int64_t> {0, 1, 2});
engine::Wrapper gObjectShadowsLevel(int64_t {0}, std::vector<int64_t> {0, 1, 2});
engine::Wrapper gLightingLevel(int64_t {1}, std::vector<int64_t> {0, 1, 2});
engine::Wrapper gSmokeDetailLevel(int64_t {1}, std::vector<int64_t> {0, 1, 2});
engine::Wrapper gWaterLevel(int64_t {0}, std::vector<int64_t> {0, 1, 2});

namespace
{

constexpr size_t kuiLevelCount = static_cast<size_t>(GraphicsQualityLevel::kCount);

constexpr float kfTerrainShadowsRenderMultipliers[kuiLevelCount] {0.1f, 0.2f, 0.4f};

constexpr float kfObjectShadowsRenderMultipliers[kuiLevelCount] {0.5f, 1.0f, 2.0f};

struct LightingLevelValues
{
	float fDepositTextureMultiplier;
	float fSpreadTextureMultiplierStart;
	float fSpreadTextureMultiplierEnd;
	float fSpreadPassCount;
	float fBlurSampleCount;
};

constexpr LightingLevelValues kLightingLevels[kuiLevelCount]
{
	{.fDepositTextureMultiplier = 0.2f, .fSpreadTextureMultiplierStart = 0.01f, .fSpreadTextureMultiplierEnd = 0.005f, .fSpreadPassCount = 12.0f, .fBlurSampleCount = 64.0f, },
	{.fDepositTextureMultiplier = 0.4f, .fSpreadTextureMultiplierStart = 0.05f, .fSpreadTextureMultiplierEnd = 0.01f, .fSpreadPassCount = 40.0f, .fBlurSampleCount = 200.0f, },
	{.fDepositTextureMultiplier = 0.8f, .fSpreadTextureMultiplierStart = 0.1f, .fSpreadTextureMultiplierEnd = 0.02f, .fSpreadPassCount = 40.0f, .fBlurSampleCount = 400.0f, },
};

constexpr float kfSmokeSimulationPixels[kuiLevelCount] {0.75f, 1.0f, 1.5f};

struct WaterLevelValues
{
	float fShapeDetail;
	// Both counts must stay members of the gWaterLowCount/gWaterMediumCount allowed set {15, 31, 63, 127, 255}
	int64_t iLowCount;
	int64_t iMediumCount;
};

constexpr WaterLevelValues kWaterLevels[kuiLevelCount]
{
	{.fShapeDetail = 1.0f / 4.0f, .iLowCount = 15, .iMediumCount = 63, },
	{.fShapeDetail = 1.0f / 2.0f, .iLowCount = 31, .iMediumCount = 127, },
	{.fShapeDetail = 1.0f, .iLowCount = 255, .iMediumCount = 255, },
};

// A level wrapper's value is float-backed and reachable from persisted settings, so clamp before it indexes a table.
size_t LevelIndex(const engine::Wrapper& rWrapper)
{
	return static_cast<size_t>(std::clamp(rWrapper.Get<int64_t>(), int64_t {0}, static_cast<int64_t>(kuiLevelCount) - 1));
}

} // namespace

void ApplyTerrainShadowsLevel()
{
	engine::gShadowRenderMultiplier.Set(kfTerrainShadowsRenderMultipliers[LevelIndex(gTerrainShadowsLevel)]);
}

void ApplyObjectShadowsLevel()
{
	engine::gObjectShadowsRenderMultiplier.Set(kfObjectShadowsRenderMultipliers[LevelIndex(gObjectShadowsLevel)]);
}

void ApplyLightingLevel()
{
	const LightingLevelValues& rValues = kLightingLevels[LevelIndex(gLightingLevel)];
	engine::gLightingDepositTextureMultiplier.Set(rValues.fDepositTextureMultiplier);
	engine::gSpreadTextureMultiplierStart.Set(rValues.fSpreadTextureMultiplierStart);
	engine::gSpreadTextureMultiplierEnd.Set(rValues.fSpreadTextureMultiplierEnd);
	engine::gSpreadPassCount.Set(rValues.fSpreadPassCount);
	engine::gLightingBlurSampleCount.Set(rValues.fBlurSampleCount);
}

void ApplySmokeDetailLevel()
{
	engine::gSmokeSimulationPixels.Set(kfSmokeSimulationPixels[LevelIndex(gSmokeDetailLevel)]);
}

void ApplyWaterLevel()
{
	const WaterLevelValues& rValues = kWaterLevels[LevelIndex(gWaterLevel)];
	engine::gWaterShapeDetail.Set(rValues.fShapeDetail);
	engine::gWaterLowCount.Set<int64_t>(rValues.iLowCount);
	engine::gWaterMediumCount.Set<int64_t>(rValues.iMediumCount);
}

void ApplyAllGraphicsQualityLevels()
{
	ApplyTerrainShadowsLevel();
	ApplyObjectShadowsLevel();
	ApplyLightingLevel();
	ApplySmokeDetailLevel();
	ApplyWaterLevel();
}

} // namespace game

#endif // defined(BT_CLIENT)
