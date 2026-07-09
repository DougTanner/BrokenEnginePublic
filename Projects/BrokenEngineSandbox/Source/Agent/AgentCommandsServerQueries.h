#pragma once

#if defined(BT_SERVER)

namespace game
{

// Frame-read query commands (query_frame / query_players / query_collection); the remaining server agent commands
// (sim-control, injection, dispatcher) live in AgentCommandsServer.cpp. rParams is the request "params" object,
// rResult the response "result" to populate. Throw on bad params (trust boundary), caught by
// AgentCommandServer::Drain(). nlohmann::json / engine::GridCoord arrive via the game Pch. CoordFromParam is shared
// with the injection group in AgentCommandsServer.cpp.
engine::GridCoord CoordFromParam(const nlohmann::json& rParams, const char* pcKey = "coord");
void CommandQueryFrame(const nlohmann::json& rParams, nlohmann::json& rResult);
void CommandQueryPlayers(const nlohmann::json& rParams, nlohmann::json& rResult);
void CommandQueryCollection(const nlohmann::json& rParams, nlohmann::json& rResult);

} // namespace game

#endif // defined(BT_SERVER)
