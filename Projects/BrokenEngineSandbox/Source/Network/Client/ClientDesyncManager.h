#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct ReconcileDesyncInfo;

class ClientDesyncManager
{
public:

	bool IsStalled() const { return mDesyncDebugState.iTick >= 0; }
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }

	void OnDesyncDetected(ReconcileDesyncInfo&& rDesyncInfo);
	void PollDebugFrameResponse();
	bool PollDesyncTimeout();
	void RecoverFromDesync();
	void ResetCoordStatesForResync();
	void Reset();

private:

	struct DesyncDebugState
	{
		int64_t iTick = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
		std::chrono::steady_clock::time_point entryTime {};
	};
	DesyncDebugState mDesyncDebugState;

	static constexpr std::chrono::seconds kDesyncDebugTimeout {5};

	// Desync frequency tracking for escalation
	int64_t miDesyncCount = 0;
	std::chrono::steady_clock::time_point mFirstDesyncTime {};
	static constexpr int64_t kiMaxDesyncsBeforeDisconnect = 3;
	static constexpr std::chrono::seconds kDesyncWindowDuration {10};
};

} // namespace game

#endif // BT_CLIENT
