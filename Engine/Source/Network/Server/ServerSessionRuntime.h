#pragma once

#if defined(BT_SERVER)

#include "Network/Server/Server.h"

namespace game
{

class ServerSession;
struct Frame;

} // namespace game

namespace engine
{

class NetworkDiscoveryResponder;

class ServerSessionRuntime
{
public:

	explicit ServerSessionRuntime(game::ServerSession& rSession, uint16_t uiPort);
	~ServerSessionRuntime();

	void Poll(const NetworkTimeState& rTimeState);
	void PollTickBoundary(const NetworkTimeState& rTimeState);
	void WaitForTick(TimeStep& rTimeStep);
	void CompleteTick(int64_t iTick);
	void CompleteUpdate(int64_t iFullTicks, int64_t iTick);
	void ResetTransportForLoad();
	void PublishTick(int64_t iTick, const std::pair<GridCoord, GridUpdateData>* pGridUpdates, int64_t iGridUpdateCount, const std::pair<GridCoord, const game::Frame*>* pFullFrames, int64_t iFullFrameCount);

	game::ServerSession& mrSession;
	std::unique_ptr<Server> mpServer;
	std::unique_ptr<NetworkDiscoveryResponder> mpDiscoveryResponder;
	HANDLE mTimerHandle = nullptr;
};

} // namespace engine

#endif // BT_SERVER
