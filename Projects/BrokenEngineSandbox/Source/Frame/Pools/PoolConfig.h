#pragma once

namespace engine
{

using area_t = uint8_t;
inline constexpr area_t kuiMaxAreas = 254ui8;

using area_light_t = uint16_t;
inline constexpr area_light_t kuiMaxAreaLights = 2046ui16;

#if defined(ENABLE_NAVMESH_DISPLAY)
using billboard_t = uint32_t;
inline constexpr billboard_t kuiMaxBillboards = 8ui32 * 2046ui32;
#else
using billboard_t = uint16_t;
inline constexpr billboard_t kuiMaxBillboards = 2046ui16;
#endif

using explosion_t = uint8_t;
inline constexpr explosion_t kuiMaxExplosions = 254ui8;

using hex_shield_t = uint8_t;
inline constexpr hex_shield_t kuiMaxHexShields = 254ui8;

using hud_t = uint8_t;
inline constexpr hud_t kuiMaxHuds = 254ui8;

using point_light_t = uint16_t;
inline constexpr point_light_t kuiMaxPointLights = 2046ui16;
using point_light_controller_t = uint16_t;
inline constexpr point_light_controller_t kuiMaxPointLightControllers2 = 254ui16;
inline constexpr point_light_controller_t kuiMaxPointLightControllers3 = 2046ui16;

using puff_t = uint16_t;
inline constexpr puff_t kuiMaxPuffs = 1022ui16;
using puff_controller_t = uint16_t;
inline constexpr puff_controller_t kuiMaxPuffControllers2 = 1022ui16;
inline constexpr puff_controller_t kuiMaxPuffControllers3 = 510ui16;

using puller_t = uint8_t;
inline constexpr puller_t kuiMaxPullers = 254ui8;

using pusher_t = uint16_t;
inline constexpr pusher_t kuiMaxPushers = 8 * 1022ui16;

using sound_t = uint16_t;
inline constexpr sound_t kuiMaxSounds = 2046ui16;

using splash_t = uint8_t;
inline constexpr splash_t kuiMaxSplashes = 254ui8;

using target_t = uint16_t;
inline constexpr target_t kuiMaxTargets = 16382ui16;

using trail_t = uint16_t;
inline constexpr trail_t kuiMaxTrails = 510ui16;

} // namespace engine
