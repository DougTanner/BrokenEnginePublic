#pragma once

#if defined(BT_CLIENT)

#include "Game.h"

namespace game
{

// describe_scene: structured JSON view of the rendered scene — camera, UI/game state, visible player/spaceship
// units (world + screen positions via CameraBase::WorldToScreen), fleets, per-collection counts, and island
// placements. Client-only. Params {"includeUnits"?:true,"maxUnits"?:200}. Throws std::runtime_error on bad
// params (trust boundary), caught by AgentCommandServer::Drain(). nlohmann::json arrives via the game Pch.
void CommandDescribeScene(const nlohmann::json& rParams, nlohmann::json& rResult);

// Shared enum-name helpers for the agent JSON surface — k-prefixed vocabulary matching the in-code enum names.
// Used by both describe_scene (AgentScene.cpp) and describe_ui (AgentCommandsClient.cpp). nlohmann::json arrives
// via the game Pch; UiState needs the explicit Game.h include above.
const char* UiStateName(UiState eState);
nlohmann::json GameFlagNames(engine::GameFlags_t flags);

} // namespace game

#endif // defined(BT_CLIENT)
