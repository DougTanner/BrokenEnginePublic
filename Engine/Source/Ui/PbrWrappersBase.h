#pragma once

#include "WrapperBase.h"

namespace engine
{

// Pbr - Engine Variables
extern Wrapper gPbrSun;
extern Wrapper gPbrDayBrightness;
extern Wrapper gPbrModelDataMipLodBias;
// Pbr - BRDF
extern Wrapper gPbrBrdfDiffuse;
extern Wrapper gPbrBrdfDiffusePower;
extern Wrapper gPbrBrdfSpecular;
extern Wrapper gPbrBrdfSpecularPower;
// Pbr - Tone Mapping
extern Wrapper gPbrExposure;
extern Wrapper gPbrGamma;
// Pbr - Color Grading
extern Wrapper gColorGradingSaturation;
extern Wrapper gColorGradingContrast;
extern Wrapper gColorGradingTemperature;
// Pbr - Post Lighting
extern Wrapper gPbrLightingSpecular;
extern Wrapper gPbrLightingSpecularPower;
extern Wrapper gPbrLighting;
extern Wrapper gPbrLightingPower;
// Pbr - IBL
extern Wrapper gPbrIblAmbient;
extern Wrapper gPbrIblDiffuse;
extern Wrapper gPbrIblDiffusePower;
extern Wrapper gPbrIblSpecular;
extern Wrapper gPbrIblSpecularPower;
extern Wrapper gPbrIblShadowBlend;
extern Wrapper gPbrIblAmbientColorBlend;
extern Wrapper gPbrCubemapLodPower;
extern Wrapper gPbrCubemapLodOffset;
extern Wrapper gPbrShadowFloor;
// Pbr - Smoke
extern Wrapper gPbrSmoke;
// Pbr - Emissive
extern Wrapper gPbrEmissive;

} // namespace engine
