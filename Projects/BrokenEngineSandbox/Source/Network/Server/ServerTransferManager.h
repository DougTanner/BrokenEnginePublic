#pragma once

#if defined(BT_SERVER)

namespace game
{

struct SubscriptionUpdate;

class ServerTransferManager
{
public:

	void HarvestTransfers();
	void ResetState();

	bool HasPendingSubscriptionUpdate(int64_t iClientId) const;

	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mTransfers;
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;

private:

	void CollectTransfers(std::vector<struct ClientTransferInfo>& rClientTransfers);
	void SortTransfersByType();
	void SpawnTransfers();
	void TrackClientTransfers(const std::vector<struct ClientTransferInfo>& rClientTransfers);
};

} // namespace game

#endif // BT_SERVER
