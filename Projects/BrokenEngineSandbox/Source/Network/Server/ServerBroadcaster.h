#pragma once

#if defined(BT_SERVER)

namespace engine
{

class ServerSessionRuntime;

} // namespace engine

namespace game
{

struct PendingUpdatePlayerRequest
{
	int64_t iClientId = 0;
	engine::global_id_t globalId {};
	bool bUseMissiles = false;
	float fNavigationDelay = 60.0f;
};

class ServerBroadcaster
{
public:

	void BuildFrameInputs();
	void BuildTickPublication(int64_t iTick, engine::ServerSessionRuntime& rRuntime, common::ScopedWorkbufferArena& rPublicationArena);
	void ProcessUpdatePlayerRequests();

	void QueueUpdatePlayerRequest(const PendingUpdatePlayerRequest& rRequest);
	void QueueAgentStatusChange(engine::GridCoord coord, const StatusChange& rChange);
	void ClearPendingRequests();
	void ClearBroadcastStatusChanges();
	void ResetState();

	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastStatusChanges;

	// Agent-injected StatusChanges accumulated at the command drain point; consumed in BuildFrameInputs before the
	// mBroadcastStatusChanges snapshot, per-coord, only when ticking (not paused/zero-tick), not during replay playback, no clients
	// awaiting spawn, and the coord is active + frame-ready — else held for a later tick. Cross-update accumulator:
	// cleared only in ResetState(), never in ClearPendingRequests() (which runs before the agent Drain). .cpp carries detail.
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mPendingAgentStatusChanges;

private:

	std::vector<PendingUpdatePlayerRequest> mPendingUpdatePlayerRequests;
};

} // namespace game

#endif // BT_SERVER
