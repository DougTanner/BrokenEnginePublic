#pragma once

namespace engine
{

// DT: TODO Move these, delete this file
using area_t = uint8_t;
inline constexpr area_t kuiMaxAreas = 254ui8;

inline constexpr uint16_t kuiMaxAreaLights = 2046ui16;

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

using puller_t = uint8_t;
inline constexpr puller_t kuiMaxPullers = 254ui8;

} // namespace engine
