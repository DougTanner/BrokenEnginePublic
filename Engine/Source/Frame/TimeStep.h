#pragma once

namespace engine
{

// Manages fixed timestep accumulator and time scaling for Frame updates
class TimeStep
{
public:

	TimeStep();

	// Add real delta time and return number of steps needed
	// Returns 0 if not enough time accumulated for a step
	int64_t UpdateRealtime();

	// Consume one step from the accumulator
	void ConsumeStep();

	// Get interpolation alpha for smooth rendering between steps
	// Returns value in [0, 1] representing how far between last and next step
	float GetInterpolationAlpha() const;

	// Clear accumulator (used when falling too far behind)
	void ClearAccumulator();

	// Get current time multiplier
	int64_t GetTimeMultiplier() const { return miTimeMultiply; }

	// Set time scale (multiply/divide)
	void SetTimeScale(int64_t iMultiply, int64_t iDivide);

	// Get smoothed average delta for performance monitoring
	float GetAverageDelta() const { return mAverageDelta.Average(); }

	// Death spiral prevention constants
	static constexpr int64_t kiMaxUpdatesPerFrame = 12;  // Threshold for auto-reduction
	static constexpr int64_t kiMaxAccumulatorSteps = 4;  // Max backlog (in update steps)

	// Decrease time scale (halve multiplier if > 1, else double divider if bAllowSlowMo)
	// Returns true if time scale was changed, updates debug text
	bool DecreaseTimeScale(bool bAllowSlowMo = true);

	// Increase time scale (halve divider if > 1, else double multiplier), updates debug text
	void IncreaseTimeScale();

	common::Timer mRealTime;
	int64_t miTimeMultiply = 1;
	int64_t miTimeDivide = 1;
	std::chrono::nanoseconds mUpdateRemainderNs = 0ns;
	common::Smoothed<float, 256> mAverageDelta;
};

} // namespace engine
