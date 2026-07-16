#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"

namespace game
{

void SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets);

void WriteFleetData(std::fstream& rFileStream, const std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, const common::RandomEngine& rRandom);

void ReadFleetData(std::fstream& rFileStream, std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, std::unordered_map<engine::ClientGuid, int64_t, engine::ClientGuidHash>& rGuidToClientId, common::RandomEngine& rRandom);

} // namespace game

#endif // BT_SERVER
