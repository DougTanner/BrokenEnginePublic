#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct ReconcileDesyncInfo;

class ClientDesyncManager
{
public:

	bool IsStalled() const
	{
		return mDesyncDebugState.iTick >= 0 || mAgentFullStateFixtureState.bStalled;
	}
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }
	bool IsAgentFullStateFixtureArmed() const
	{
		return mAgentFullStateFixtureState.bStalled;
	}
	int64_t GetAgentFullStateFixtureTick() const
	{
		return mAgentFullStateFixtureState.iTick;
	}
	engine::GridCoord GetAgentFullStateFixtureCoord() const
	{
		return mAgentFullStateFixtureState.coord;
	}

	void OnDesyncDetected(ReconcileDesyncInfo&& rDesyncInfo);
	void PollDebugFrameResponse();
	bool PollDesyncTimeout();
	void RecoverFromDesync();
	void ResetCoordStatesForResync();
	void Reset();
	void ArmAgentFullStateFixture(int64_t iTick, engine::GridCoord coord);
	void ClearAgentFullStateFixture();

private:

	struct DesyncDebugState
	{
		int64_t iTick = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
		std::chrono::steady_clock::time_point entryTime {};
	};
	DesyncDebugState mDesyncDebugState;

	struct AgentFullStateFixtureState
	{
		bool bStalled = false;
		int64_t iTick = -1;
		engine::GridCoord coord {};
	};
	AgentFullStateFixtureState mAgentFullStateFixtureState {};

	static constexpr std::chrono::seconds kDesyncDebugTimeout {5};

	// Desync frequency tracking for escalation
	int64_t miDesyncCount = 0;
	std::chrono::steady_clock::time_point mFirstDesyncTime {};
	static constexpr int64_t kiMaxDesyncsBeforeDisconnect = 3;
	static constexpr std::chrono::seconds kDesyncWindowDuration {10};
};

} // namespace game

#endif // BT_CLIENT
