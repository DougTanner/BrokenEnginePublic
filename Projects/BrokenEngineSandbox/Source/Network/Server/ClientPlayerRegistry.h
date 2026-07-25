#pragma once

#if defined(BT_SERVER)

namespace game
{

struct OwnedPlayer
{
	engine::global_id_t globalId {};
	engine::GridCoord coord {};
};

class ClientPlayerRegistry
{
public:

	enum class RelinkContext
	{
		kConnect,
		kLoad,
	};

	void Add(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord);
	void RemoveAt(int64_t iClientId, int64_t iIndex);
	void UpdateCoord(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord newCoord);
	std::span<const OwnedPlayer> Owned(int64_t iClientId) const;
	void Remove(int64_t iClientId);
	void Clear(int64_t iClientId);
	int64_t RelinkFromFrames(int64_t iClientId, const engine::ClientGuid& rGuid, RelinkContext eContext);

private:

	std::unordered_map<int64_t, std::vector<OwnedPlayer>> mOwned;
};

} // namespace game

#endif // BT_SERVER
