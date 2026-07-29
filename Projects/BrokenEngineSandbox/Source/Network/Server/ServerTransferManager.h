#pragma once

#if defined(BT_SERVER)

namespace game
{

struct SubscriptionUpdate;

struct ClientTransferInfo
{
	engine::global_id_t globalPlayerId {};
	engine::GridCoord destination {};
	engine::ClientGuid clientGuid {};
};

class ServerTransferManager
{
public:

	void HarvestTransfers();
	void ResetState();
	bool QueueReplayTransferFixture(engine::GridCoord destination, StatusChange transfer);

	bool HasPendingSubscriptionUpdate(int64_t iClientId) const;

	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mTransfers;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mReplayTransferFixtures;
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;

private:

	void CollectTransfers(common::ScopedWorkbufferArena& rTransfersArena);
	void SortTransfersByType();
	void SpawnTransfers();
	void TrackClientTransfers(std::span<const ClientTransferInfo> clientTransfers);
};

} // namespace game

#endif // BT_SERVER
