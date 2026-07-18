#include "PbrWrappersBase.h"

namespace engine
{

// Pbr - Engine Variables
Wrapper gPbrSun(0.8f, 0.5f, 1.0f);
Wrapper gPbrDayBrightness(3.0f, 1.0f, 4.0f);
Wrapper gPbrModelDataMipLodBias(0.0f, -2.0f, 2.0f);
// Pbr - BRDF
Wrapper gPbrBrdfDiffuse(10.0f, 0.0f, 20.0f);
Wrapper gPbrBrdfDiffusePower(0.8f, 0.5f, 1.0f);
Wrapper gPbrBrdfSpecular(20.0f, 0.0f, 30.0f);
Wrapper gPbrBrdfSpecularPower(0.30f, 0.1f, 1.0f);
// Pbr - Tone Mapping
Wrapper gPbrExposure(0.9f, 0.0f, 2.0f);
Wrapper gPbrGamma(0.9f, 0.0f, 2.0f);
// Pbr - Color Grading
Wrapper gColorGradingSaturation(1.05f, 0.0f, 2.0f);
Wrapper gColorGradingContrast(1.1f, 0.5f, 2.0f);
Wrapper gColorGradingTemperature(-0.2f, -1.0f, 1.0f);
// Pbr - Post Lighting
Wrapper gPbrLightingSpecular(0.5f, 0.0f, 1.0f);
Wrapper gPbrLightingSpecularPower(0.5f, 0.1f, 1.0f);
Wrapper gPbrLighting(0.2f, 0.0f, 0.3f);
Wrapper gPbrLightingPower(1.5f, 0.5f, 2.0f);
// Pbr - IBL
Wrapper gPbrIblAmbient(0.25f, 0.0f, 0.4f);
Wrapper gPbrIblDiffuse(1.0f, 0.0f, 3.0f);
Wrapper gPbrIblDiffusePower(0.95f, 0.5f, 2.0f);
Wrapper gPbrIblSpecular(4.0f, 0.0f, 6.0f);
Wrapper gPbrIblSpecularPower(1.0f, 0.5f, 2.0f);
Wrapper gPbrIblShadowBlend(0.3f, 0.0f, 1.0f);
Wrapper gPbrIblAmbientColorBlend(0.5f, 0.0f, 1.0f);
Wrapper gPbrCubemapLodPower(1.0f, 0.1f, 1.0f);
Wrapper gPbrCubemapLodOffset(0.0f, 0.0f, 100.0f);
Wrapper gPbrShadowFloor(0.3f, 0.0f, 1.0f);
// Pbr - Smoke
Wrapper gPbrSmoke(0.5f, 0.0f, 1.0f);
// Pbr - Emissive
Wrapper gPbrEmissive(1.0f, 0.0f, 5.0f);

} // namespace engine
