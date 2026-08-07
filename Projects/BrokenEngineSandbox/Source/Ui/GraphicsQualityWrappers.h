#pragma once

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace game
{

enum class GraphicsQualityLevel : uint8_t
{
	kLow,
	kMedium,
	kHigh,

	kCount,
};

// Player-facing quality levels. Each holds a GraphicsQualityLevel as its int64_t value and owns no rendering
// state itself; the Apply functions below are what write the underlying engine wrappers.
extern engine::Wrapper gTerrainShadowsLevel;
extern engine::Wrapper gObjectShadowsLevel;
extern engine::Wrapper gLightingLevel;
extern engine::Wrapper gSmokeDetailLevel;
extern engine::Wrapper gWaterLevel;

// Write the underlying engine wrappers for one feature from its current level. Call right after the level
// changes: Set() leaves the underlying wrappers' previous value intact, so Graphics::Refresh still sees the
// change on its next poll, and these never call Changed<T>() (that single poll belongs to Refresh).
void ApplyTerrainShadowsLevel();
void ApplyObjectShadowsLevel();
void ApplyLightingLevel();
void ApplySmokeDetailLevel();
void ApplyWaterLevel();

void ApplyAllGraphicsQualityLevels();

} // namespace game

#endif // defined(BT_CLIENT)
