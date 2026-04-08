#pragma once

#if defined(BT_SERVER)

namespace game
{

struct PendingUpdatePlayerRequest
{
	int64_t iClientId = 0;
	engine::global_id_t globalId {};
	bool bUseMissiles = false;
	float fNavigationDelay = 2.0f;
};

class ServerBroadcaster
{
public:

	void BuildFrameInputs();
	void BroadcastStatusChanges(int64_t iTick);
	void ProcessUpdatePlayerRequests();

	void QueueUpdatePlayerRequest(const PendingUpdatePlayerRequest& rRequest);
	void ClearPendingRequests();
	void ClearSpawns();
	void ResetState();

	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mSpawns;

private:

	std::vector<PendingUpdatePlayerRequest> mPendingUpdatePlayerRequests;
};

} // namespace game

#endif // BT_SERVER
