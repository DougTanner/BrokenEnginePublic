#pragma once

namespace engine
{

// Engine-generic agent commands shared by every game project: ping, quit, get_logs, set_log_level.
// Returns true when cmd was handled. iGameTick is the game's current tick (-1 before game creation), reported by ping.
// Throws on invalid params (external trust boundary); AgentCommandServer::Drain() converts to the failure envelope.
bool ExecuteSharedAgentCommand(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult, int64_t iGameTick);

} // namespace engine
