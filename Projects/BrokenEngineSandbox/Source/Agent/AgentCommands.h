#pragma once

namespace game
{

// Dispatches a single agent command by name. rParams is the request "params" object, rResult the response
// "result" object to populate. Tries the engine shared commands first (engine::ExecuteSharedAgentCommand —
// ping/quit/get_logs/set_log_level), then falls through to the side-specific BT_CLIENT/BT_SERVER handlers.
// Throws (std::runtime_error / nlohmann type/parse errors) on any failure — unknown command, missing/mistyped
// params, unknown category or level, invalid regex — which the engine AgentCommandServer::Drain() catches and
// formats into the failure envelope. Engine calling game:: is the sanctioned direction. nlohmann::json arrives
// via the game Pch (ExternalHeaders BT_ENGINE gate).
void ExecuteAgentCommand(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult);

#if defined(BT_CLIENT)
// Client-only command dispatch (capture, input, scene, GPU profile, and diagnostic probes). ExecuteAgentCommand falls
// through to this under BT_CLIENT before the unknown-command throw; returns true if handled. Defined in the
// client-vcxproj-only AgentCommandsClient.cpp. Throws on bad params (trust boundary), caught by AgentCommandServer::Drain().
bool ExecuteAgentCommandClient(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult);
#endif

#if defined(BT_SERVER)
// Server-only command dispatch (status / pause / timescale / save / load / reset / replay_record / replay_play /
// replay_drop_retained_end_frame / CPU query_profile).
// ExecuteAgentCommand falls through to this under BT_SERVER before the unknown-command throw; returns true if
// handled. Defined in the server-vcxproj-only AgentCommandsServer.cpp. Throws on bad params (trust boundary), caught
// by AgentCommandServer::Drain().
bool ExecuteAgentCommandServer(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult);
#endif

} // namespace game
