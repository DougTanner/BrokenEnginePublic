#pragma once

#if defined(BT_CLIENT)

#include "Network/ClientNetwork/ClientSessionBase.h"
#include "Network/ClientReconciler.h"

namespace game
{

struct Frame;

class ClientSession : public engine::ClientSessionBase
{
public:

	ClientSession();
	~ClientSession() override;

	// Connection
	bool IsNetworkMode() const { return mpClientNetwork != nullptr; }
	void ConnectToServer(std::string_view serverAddress);
	void DisconnectFromServer();

	// Main-loop integration
	void PollNetwork();
	void PollAndReconcile();
	void PostTick();
	void PostRender();

	// Reconciliation
	void WaitForReconcile();
	void TryKickReconcile();

	// Subscriptions
	void UpdateSubscriptions();

	// Queries
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick);

private:

	// Connection helpers
	bool PollConnection();
	bool PollConnectionStatus();
	void TryEnterGame();
	void PollDebugFrameResponse();

	// Subscription helpers
	std::vector<engine::GridCoord> ComputeDesiredCoords() const;

	void ApplyReceivedFullStates();
	void ApplyReceivedUpdates();

	struct DesyncDebugState
	{
		int64_t iTick = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
		std::chrono::steady_clock::time_point entryTime {};
	};
	DesyncDebugState mDesyncDebugState;

	static constexpr std::chrono::seconds kDesyncDebugTimeout {5};

	void CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, int64_t iTick, engine::GridCoord coord);

	// Reconciliation
	std::unique_ptr<ClientReconciler> mpReconciler;
};

inline ClientSession* gpClientSession = nullptr;

} // namespace game

#endif // BT_CLIENT
