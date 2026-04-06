#pragma once

namespace game
{

struct FleetMember
{
	engine::global_player_t globalPlayerId {};
	bool bAlive = true;
};

struct Fleet
{
	std::vector<FleetMember> members;
};

} // namespace game
