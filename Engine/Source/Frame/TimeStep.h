#pragma once

namespace engine
{

inline constexpr int64_t kiTickRate = 32;

// Manages fixed timestep accumulator and time scaling for Frame updates
class TimeStep
{
public:

	TimeStep() = default;

	// Add real delta time and return number of ticks needed
	// Returns 0 if not enough time accumulated for a tick
	int64_t TickRealtime();

	// Clear accumulator (used when falling too far behind)
	void ClearAccumulator();

	// Push ticks worth of time back into the accumulator (used by the client sim ceiling clamp when
	// wall-clock wanted more ticks than the session ceiling allows). TickRealtime's existing
	// kiMaxAccumulatorTicks cap bounds total growth during long stalls.
	void AbsorbUnusedTicks(int64_t iTicks);

	// Convert a wall-clock duration into sim-clock duration using the current time ratio.
	// Same formula as TickRealtime's accumulator step — keep the two in sync.
	std::chrono::nanoseconds WallToSim(std::chrono::nanoseconds wallNs) const
	{
		return (wallNs * miTimeMultiply) / miTimeDivide;
	}

	// Inverse of WallToSim: convert a sim-clock duration into the wall-clock duration it occupies
	// at the current time ratio. Use for waiters/sleepers (e.g. ServerSessionRuntime::WaitForTick) that
	// need real-time pacing for a fixed amount of sim time.
	std::chrono::nanoseconds SimToWall(std::chrono::nanoseconds simNs) const
	{
		return (simNs * miTimeDivide) / miTimeMultiply;
	}

	// Set time scale (multiply/divide)
	void SetTimeScale(int64_t iMultiply, int64_t iDivide);

	// Death spiral prevention constants
	static constexpr int64_t kiMaxTicksPerFrame = 12;  // Threshold for auto-reduction
	static constexpr int64_t kiMaxAccumulatorTicks = 4;  // Max backlog (in ticks)

	// Decrease time scale (halve multiplier if > 1, else double divider if bAllowSlowMo)
	// Returns true if time scale was changed, updates debug text
	bool DecreaseTimeScale(bool bAllowSlowMo = true);

	// Increase time scale (halve divider if > 1, else double multiplier), updates debug text
	void IncreaseTimeScale();

	common::Timer mRealTime;
	int64_t miTimeMultiply = 1;
	int64_t miTimeDivide = 1;
	std::chrono::nanoseconds mTickRemainderNs = 0ns;
	common::Smoothed<float, 256> mAverageDelta;
	bool mbTimeScaleChanged = false;
};

} // namespace engine
