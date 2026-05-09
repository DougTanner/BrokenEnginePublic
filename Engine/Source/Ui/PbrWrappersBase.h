#pragma once

#include "WrapperBase.h"

namespace engine
{

// Pbr - Engine Variables
extern Wrapper gPbrSun;
extern Wrapper gPbrSunPower;
extern Wrapper gPbrDayBrightness;
// Pbr - BRDF
extern Wrapper gPbrBrdfDiffuse;
extern Wrapper gPbrBrdfDiffusePower;
extern Wrapper gPbrBrdfSpecular;
extern Wrapper gPbrBrdfSpecularPower;
// Pbr - Tone Mapping
extern Wrapper gPbrExposure;
extern Wrapper gPbrGamma;
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
