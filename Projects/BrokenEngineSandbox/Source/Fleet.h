#pragma once

namespace game
{

struct FleetMember
{
	engine::global_id_t globalPlayerId {};
	bool bAlive = true;
};

struct Fleet
{
	std::vector<FleetMember> members;
	int64_t iFlagshipIndex = 0;
};

} // namespace game
