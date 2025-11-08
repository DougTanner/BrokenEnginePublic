#pragma once

namespace engine
{

// Manages fixed timestep accumulator and time scaling for physics updates
class TimeStep
{
public:

	TimeStep();

	// Reset timers on focus loss or initialization
	void Reset();

	// Add real delta time and return number of physics steps needed
	// Returns 0 if not enough time accumulated for a step
	int64_t AddDelta(std::chrono::nanoseconds realDeltaNs, bool bSingleStep, bool bLostFocus);

	// Consume one physics step from the accumulator
	void ConsumeStep();

	// Get interpolation alpha for smooth rendering between physics steps
	// Returns value in [0, 1] representing how far between last and next physics step
	float GetInterpolationAlpha() const;

	// Adjust time scale to slow down simulation if behind VSync
	// Returns true if time scale was reduced
	bool AdjustTimeScale(std::chrono::nanoseconds monitorRefreshTimeNs);

	// Clear accumulator (used when falling too far behind)
	void ClearAccumulator();

	// Get current time multiplier
	int64_t GetTimeMultiplier() const { return miTimeMultiply; }

	// Set time scale (multiply/divide)
	void SetTimeScale(int64_t iMultiply, int64_t iDivide);

	// Get smoothed average delta for performance monitoring
	float GetAverageDelta() const { return mAverageDelta.Average(); }

	common::Timer mRealTime;
	int64_t miTimeMultiply = 1;
	int64_t miTimeDivide = 1;
	std::chrono::nanoseconds mUpdateRemainderNs = 0ns;
	common::Smoothed<float, 256> mAverageDelta;

private:

	static constexpr std::chrono::nanoseconds kUpdateStepNs = 4'000'000ns;
};

} // namespace engine
