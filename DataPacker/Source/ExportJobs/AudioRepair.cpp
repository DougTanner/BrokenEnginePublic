#include "AudioRepair.h"

namespace audiorepair
{

namespace
{

// Fix thresholds
constexpr float kfDcOffsetThreshold = 0.002f;
constexpr float kfPeakNormalizeThreshold = 1.001f; // a legit -32768 16-bit sample lands at 1.00003 after /32767 — don't rescale a whole file for one LSB
constexpr float kfEdgeAmplitudeThreshold = 0.01f;
constexpr int64_t kiEdgeProbeFrames = 8; // sub-0.2ms attack is a click even when frame 0 is small
constexpr float kfEdgeFadeSeconds = 0.003f; // raised cosine; 132 samples @ 44.1kHz
constexpr int64_t kiEdgeFadeMinSamples = 16;

// Declip detection — shared by the fix path and every warn-only branch so reported numbers mean
// the same thing either way. Detection is relative to the channel's own peak so sources attenuated
// after clipping are still caught; the absolute floor keeps quiet material from false-positiving.
constexpr float kfClipRunLevelFraction = 0.999f;
constexpr float kfClipDetectMinPeak = 0.95f;
constexpr int64_t kiClipRunMinSamples = 3;
constexpr int64_t kiClipRunMaxFixSamples = 32; // longer runs: warn-only, unfixable by short-interval interpolation
constexpr int64_t kiClipSupportSamplesPerSide = 8;
constexpr int64_t kiClipSupportMinSamplesPerSide = 4;
constexpr float kfDeclipMaxReconstruction = 2.0f; // spline blowup cap
constexpr int64_t kiDeclipWarnOnlyRunCount = 256; // more runs per channel => mastering-style limiting, warn-only

// Warn-only thresholds
constexpr float kfLoopSeamWarnThreshold = 0.005f;

struct ClipRun
{
	int64_t iStart = 0; // first clipped frame, inclusive
	int64_t iEnd = 0;   // last clipped frame, inclusive
	float fRailValue = 0.0f; // signed mean of the run's samples — the flat-top level
};

int64_t ScrubNonFinite(std::vector<float>& rfSamples)
{
	int64_t iScrubbed = 0;
	for (float& rfSample : rfSamples)
	{
		if (!std::isfinite(rfSample))
		{
			rfSample = 0.0f;
			++iScrubbed;
		}
	}
	return iScrubbed;
}

// Returns the per-channel mean actually subtracted (0.0 when below threshold)
float RemoveDcOffset(std::vector<float>& rfSamples, int64_t iChannels, int64_t iChannel)
{
	double dSum = 0.0;
	int64_t iFrames = static_cast<int64_t>(rfSamples.size()) / iChannels;
	for (int64_t i = 0; i < iFrames; ++i)
	{
		dSum += rfSamples[i * iChannels + iChannel];
	}
	float fMean = static_cast<float>(dSum / static_cast<double>(iFrames));
	if (std::abs(fMean) <= kfDcOffsetThreshold)
	{
		return 0.0f;
	}

	for (int64_t i = 0; i < iFrames; ++i)
	{
		rfSamples[i * iChannels + iChannel] -= fMean;
	}
	return fMean;
}

// Natural cubic spline second derivatives via the Thomas algorithm (M_first = M_last = 0),
// non-uniform x spacing. Doubles internally — trivial cost offline.
void SolveNaturalCubicSpline(const double* pXs, const double* pYs, double* pSecondDerivatives, int64_t iCount)
{
	pSecondDerivatives[0] = 0.0;
	pSecondDerivatives[iCount - 1] = 0.0;
	if (iCount < 3)
	{
		return;
	}

	// Forward elimination over the interior tridiagonal system
	std::vector<double> dDiagonal(iCount, 0.0);
	std::vector<double> dRhs(iCount, 0.0);
	for (int64_t i = 1; i < iCount - 1; ++i)
	{
		double dHPrevious = pXs[i] - pXs[i - 1];
		double dHNext = pXs[i + 1] - pXs[i];
		dDiagonal[i] = 2.0 * (dHPrevious + dHNext);
		dRhs[i] = 6.0 * ((pYs[i + 1] - pYs[i]) / dHNext - (pYs[i] - pYs[i - 1]) / dHPrevious);

		if (i > 1)
		{
			double dFactor = dHPrevious / dDiagonal[i - 1];
			dDiagonal[i] -= dFactor * dHPrevious;
			dRhs[i] -= dFactor * dRhs[i - 1];
		}
	}

	// Back substitution
	for (int64_t i = iCount - 2; i >= 1; --i)
	{
		double dHNext = pXs[i + 1] - pXs[i];
		pSecondDerivatives[i] = (dRhs[i] - dHNext * pSecondDerivatives[i + 1]) / dDiagonal[i];
	}
}

double EvaluateCubicSpline(const double* pXs, const double* pYs, const double* pSecondDerivatives, int64_t iInterval, double dX)
{
	double dH = pXs[iInterval + 1] - pXs[iInterval];
	double dA = pXs[iInterval + 1] - dX;
	double dB = dX - pXs[iInterval];
	return pSecondDerivatives[iInterval] * dA * dA * dA / (6.0 * dH)
		+ pSecondDerivatives[iInterval + 1] * dB * dB * dB / (6.0 * dH)
		+ (pYs[iInterval] / dH - pSecondDerivatives[iInterval] * dH / 6.0) * dA
		+ (pYs[iInterval + 1] / dH - pSecondDerivatives[iInterval + 1] * dH / 6.0) * dB;
}

// Detects flat-top runs and (when bAllowDeclip) reconstructs short ones with a natural cubic
// spline through the nearest clean samples each side. The clipped mask is computed once and never
// updated, so every run reconstructs from original clean samples — order-independent.
void DeclipChannel(std::vector<float>& rfSamples, int64_t iChannels, int64_t iChannel, bool bAllowDeclip, std::string_view relativeFile)
{
	int64_t iFrames = static_cast<int64_t>(rfSamples.size()) / iChannels;
	auto Sample = [&](int64_t iFrame) -> float&
	{
		return rfSamples[iFrame * iChannels + iChannel];
	};

	// Rails detect independently: an asymmetric source (e.g. clipped then DC-shifted) can have one
	// rail well below the other's peak, so a single per-channel level would miss it
	float fPositivePeak = 0.0f;
	float fNegativePeak = 0.0f;
	for (int64_t i = 0; i < iFrames; ++i)
	{
		float fSample = Sample(i);
		fPositivePeak = std::max(fPositivePeak, fSample);
		fNegativePeak = std::max(fNegativePeak, -fSample);
	}
	bool bDetectPositive = fPositivePeak >= kfClipDetectMinPeak;
	bool bDetectNegative = fNegativePeak >= kfClipDetectMinPeak;
	if (!bDetectPositive && !bDetectNegative)
	{
		return;
	}

	// Clipped mask + maximal same-sign runs
	float fPositiveClipLevel = kfClipRunLevelFraction * fPositivePeak;
	float fNegativeClipLevel = kfClipRunLevelFraction * fNegativePeak;
	std::vector<uint8_t> clippedMask(iFrames, 0);
	for (int64_t i = 0; i < iFrames; ++i)
	{
		float fSample = Sample(i);
		clippedMask[i] = ((bDetectPositive && fSample >= fPositiveClipLevel) || (bDetectNegative && fSample <= -fNegativeClipLevel)) ? 1 : 0;
	}

	std::vector<ClipRun> runs;
	for (int64_t i = 0; i < iFrames;)
	{
		if (clippedMask[i] == 0)
		{
			++i;
			continue;
		}
		bool bPositive = Sample(i) >= 0.0f;
		int64_t iStart = i;
		double dRailSum = 0.0;
		while (i < iFrames && clippedMask[i] != 0 && (Sample(i) >= 0.0f) == bPositive)
		{
			dRailSum += Sample(i);
			++i;
		}
		int64_t iLength = i - iStart;
		if (iLength >= kiClipRunMinSamples)
		{
			runs.push_back({iStart, i - 1, static_cast<float>(dRailSum / static_cast<double>(iLength))});
		}
	}

	if (runs.empty())
	{
		return;
	}

	int64_t iLongestRun = 0;
	for (const ClipRun& rRun : runs)
	{
		iLongestRun = std::max(iLongestRun, rRun.iEnd - rRun.iStart + 1);
	}

	if (static_cast<int64_t>(runs.size()) > kiDeclipWarnOnlyRunCount)
	{
		LOG(kDefault, kWarning, "{}: pervasive clipping, {} runs (longest {}) on channel {} - left as-is (mastering-style limiting)", relativeFile, runs.size(), iLongestRun, iChannel);
		return;
	}
	if (!bAllowDeclip)
	{
		LOG(kDefault, kWarning, "{}: clipping detected, {} runs (longest {}) on channel {} - declip disabled for this asset", relativeFile, runs.size(), iLongestRun, iChannel);
		return;
	}

	int64_t iRunsFixed = 0;
	int64_t iLongestFixed = 0;
	int64_t iRunsSkipped = 0;
	float fMaxReconstruction = 0.0f;
	std::vector<double> dXs;
	std::vector<double> dYs;
	std::vector<double> dSecondDerivatives;
	for (const ClipRun& rRun : runs)
	{
		int64_t iLength = rRun.iEnd - rRun.iStart + 1;
		if (iLength > kiClipRunMaxFixSamples || rRun.iStart == 0 || rRun.iEnd == iFrames - 1)
		{
			++iRunsSkipped;
			continue;
		}

		// Nearest clean frames each side, skipping neighboring runs' rail samples (they bias the fit low)
		dXs.clear();
		dYs.clear();
		int64_t iLeftCount = 0;
		for (int64_t i = rRun.iStart - 1; i >= 0 && iLeftCount < kiClipSupportSamplesPerSide; --i)
		{
			if (clippedMask[i] == 0)
			{
				dXs.push_back(static_cast<double>(i));
				dYs.push_back(Sample(i));
				++iLeftCount;
			}
		}
		if (iLeftCount < kiClipSupportMinSamplesPerSide)
		{
			++iRunsSkipped;
			continue;
		}
		std::reverse(dXs.begin(), dXs.end());
		std::reverse(dYs.begin(), dYs.end());

		int64_t iRightCount = 0;
		for (int64_t i = rRun.iEnd + 1; i < iFrames && iRightCount < kiClipSupportSamplesPerSide; ++i)
		{
			if (clippedMask[i] == 0)
			{
				dXs.push_back(static_cast<double>(i));
				dYs.push_back(Sample(i));
				++iRightCount;
			}
		}
		if (iRightCount < kiClipSupportMinSamplesPerSide)
		{
			++iRunsSkipped;
			continue;
		}

		dSecondDerivatives.assign(dXs.size(), 0.0);
		SolveNaturalCubicSpline(dXs.data(), dYs.data(), dSecondDerivatives.data(), static_cast<int64_t>(dXs.size()));

		// The gap lies in the interval between the innermost support points
		int64_t iGapInterval = iLeftCount - 1;
		bool bPositive = rRun.fRailValue >= 0.0f;
		for (int64_t i = rRun.iStart; i <= rRun.iEnd; ++i)
		{
			float fReconstructed = static_cast<float>(EvaluateCubicSpline(dXs.data(), dYs.data(), dSecondDerivatives.data(), iGapInterval, static_cast<double>(i)));
			// Declip only restores magnitude the clip removed: force the run's sign and rail floor,
			// and cap against spline blowup on pathological support. min/max ordering (not std::clamp):
			// an over-full-scale float source can rail beyond the cap, and clamp with lo > hi is UB — the cap wins
			if (bPositive)
			{
				fReconstructed = std::min(std::max(fReconstructed, rRun.fRailValue), kfDeclipMaxReconstruction);
			}
			else
			{
				fReconstructed = std::max(std::min(fReconstructed, rRun.fRailValue), -kfDeclipMaxReconstruction);
			}
			Sample(i) = fReconstructed;
			fMaxReconstruction = std::max(fMaxReconstruction, std::abs(fReconstructed));
		}
		++iRunsFixed;
		iLongestFixed = std::max(iLongestFixed, iLength);
	}

	if (iRunsFixed > 0)
	{
		LOG(kDefault, kWarning, "{}: declipped {} runs (longest {}, max reconstruction {:.3f}) on channel {}", relativeFile, iRunsFixed, iLongestFixed, fMaxReconstruction, iChannel);
	}
	if (iRunsSkipped > 0)
	{
		LOG(kDefault, kWarning, "{}: {} clip runs unfixable (too long, at file edge, or insufficient clean support) on channel {}", relativeFile, iRunsSkipped, iChannel);
	}
}

// Whole-file rescale by one factor across all channels (preserves the stereo image). Absorbs
// declip reconstruction overshoot and over-full-scale float sources.
void NormalizePeak(std::vector<float>& rfSamples, std::string_view relativeFile)
{
	float fPeak = 0.0f;
	for (float fSample : rfSamples)
	{
		fPeak = std::max(fPeak, std::abs(fSample));
	}
	if (fPeak <= kfPeakNormalizeThreshold)
	{
		return;
	}

	float fScale = 1.0f / fPeak;
	for (float& rfSample : rfSamples)
	{
		rfSample *= fScale;
	}
	LOG(kDefault, kWarning, "{}: peak {:.4f} above full scale, rescaled by {:.4f}", relativeFile, fPeak, fScale);
}

void ValidateLoopSeam(const std::vector<float>& rfSamples, int64_t iChannels, std::string_view relativeFile, bool bDcCorrected)
{
	int64_t iFrames = static_cast<int64_t>(rfSamples.size()) / iChannels;
	for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
	{
		float fSeamJump = std::abs(rfSamples[iChannel] - rfSamples[(iFrames - 1) * iChannels + iChannel]);
		if (fSeamJump > kfLoopSeamWarnThreshold)
		{
			LOG(kDefault, kWarning, "{}: loop seam mismatch {:.4f} > {:.4f} on channel {} - will click every loop iteration (warn only){}", relativeFile, fSeamJump, kfLoopSeamWarnThreshold, iChannel, bDcCorrected ? "; DC offset was also corrected - check the source edit" : "");
		}
	}
}

// Raised-cosine fade-in/out when an edge is audibly non-zero. All channels fade together (fading
// one channel of a stereo pair would skew the image). Runs last: every earlier pass changes edge
// amplitudes, and measuring the final value avoids fading files an earlier pass already cured.
void ApplyEdgeFades(std::vector<float>& rfSamples, int64_t iChannels, int64_t iSamplesPerSec, std::string_view relativeFile, bool bAllowEdgeFades)
{
	int64_t iFrames = static_cast<int64_t>(rfSamples.size()) / iChannels;
	int64_t iFadeFrames = std::max(kiEdgeFadeMinSamples, static_cast<int64_t>(kfEdgeFadeSeconds * static_cast<float>(iSamplesPerSec)));
	iFadeFrames = std::min(iFadeFrames, iFrames / 2);
	if (iFadeFrames < 2)
	{
		return;
	}

	// Probe a few frames in, not just the edge frame: a ramp to high amplitude within ~0.2ms is
	// still a click even when frame 0 itself is small
	int64_t iProbeFrames = std::min(kiEdgeProbeFrames, iFrames);
	float fOnsetPeak = 0.0f;
	float fTailPeak = 0.0f;
	for (int64_t i = 0; i < iProbeFrames; ++i)
	{
		for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
		{
			fOnsetPeak = std::max(fOnsetPeak, std::abs(rfSamples[i * iChannels + iChannel]));
			fTailPeak = std::max(fTailPeak, std::abs(rfSamples[(iFrames - 1 - i) * iChannels + iChannel]));
		}
	}

	if (fOnsetPeak > kfEdgeAmplitudeThreshold)
	{
		if (bAllowEdgeFades)
		{
			for (int64_t i = 0; i < iFadeFrames; ++i)
			{
				float fGain = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(iFadeFrames)));
				for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
				{
					rfSamples[i * iChannels + iChannel] *= fGain;
				}
			}
			LOG(kDefault, kWarning, "{}: faded {}-frame onset (edge amplitude {:.3f})", relativeFile, iFadeFrames, fOnsetPeak);
		}
		else
		{
			LOG(kDefault, kWarning, "{}: onset amplitude {:.3f} above {:.3f} (warn only)", relativeFile, fOnsetPeak, kfEdgeAmplitudeThreshold);
		}
	}

	if (fTailPeak > kfEdgeAmplitudeThreshold)
	{
		if (bAllowEdgeFades)
		{
			for (int64_t i = 0; i < iFadeFrames; ++i)
			{
				float fGain = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(iFadeFrames)));
				for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
				{
					rfSamples[(iFrames - 1 - i) * iChannels + iChannel] *= fGain;
				}
			}
			LOG(kDefault, kWarning, "{}: faded {}-frame tail (edge amplitude {:.3f})", relativeFile, iFadeFrames, fTailPeak);
		}
		else
		{
			LOG(kDefault, kWarning, "{}: tail amplitude {:.3f} above {:.3f} (warn only)", relativeFile, fTailPeak, kfEdgeAmplitudeThreshold);
		}
	}
}

// Modified Bessel function of the first kind, order 0 — series sum_{k>=0} ((x/2)^k / k!)^2.
// Doubles throughout; the series converges fast for the Kaiser beta range used here.
double BesselI0(double dX)
{
	double dSum = 1.0;
	double dTerm = 1.0;
	double dHalfX = 0.5 * dX;
	for (int64_t k = 1; k < 64; ++k)
	{
		dTerm *= dHalfX / static_cast<double>(k);
		double dTermSquared = dTerm * dTerm;
		dSum += dTermSquared;
		if (dTermSquared < 1e-12 * dSum)
		{
			break;
		}
	}
	return dSum;
}

// Normalized sinc: sin(pi x) / (pi x), with the removable singularity at 0 handled.
double NormalizedSinc(double dX)
{
	if (dX == 0.0)
	{
		return 1.0;
	}
	double dPiX = std::numbers::pi_v<double> * dX;
	return std::sin(dPiX) / dPiX;
}

} // namespace

void Resample(std::vector<float>& rfSamples, int64_t iChannels, int64_t iSourceRate, int64_t iTargetRate, std::string_view relativeFile)
{
	if (iSourceRate == iTargetRate || rfSamples.empty())
	{
		return;
	}

	int64_t iSourceFrames = static_cast<int64_t>(rfSamples.size()) / iChannels;
	if (iSourceFrames < 2)
	{
		return;
	}

	// Input frames advanced per output frame; output count preserves duration.
	double dStep = static_cast<double>(iSourceRate) / static_cast<double>(iTargetRate);
	int64_t iTargetFrames = std::llround(static_cast<double>(iSourceFrames) / dStep);
	if (iTargetFrames < 1)
	{
		return;
	}

	// Kaiser-windowed sinc. Cutoff sits at the lower of the two Nyquists (input-relative), so
	// downsampling widens the kernel to band-limit before decimation; upsampling leaves it at the
	// input Nyquist. ~32 taps per output sample at unity rate (16 sinc zero-crossings each side).
	// Per-output weight-sum normalization pins DC gain to 1 and absorbs edge-clamp asymmetry.
	constexpr int64_t kiZeroCrossings = 16;
	constexpr double kdKaiserBeta = 9.0; // ~ -90 dB stopband
	double dCutoff = std::min(1.0, 1.0 / dStep); // == min(1, target/source)
	double dHalfWidth = static_cast<double>(kiZeroCrossings) / dCutoff; // support half-width, in input frames
	double dInverseI0Beta = 1.0 / BesselI0(kdKaiserBeta);

	std::vector<float> fResampled(static_cast<size_t>(iTargetFrames * iChannels));
	for (int64_t iOut = 0; iOut < iTargetFrames; ++iOut)
	{
		double dCenter = static_cast<double>(iOut) * dStep; // position in input frames
		int64_t iFirstTap = static_cast<int64_t>(std::ceil(dCenter - dHalfWidth));
		int64_t iLastTap = static_cast<int64_t>(std::floor(dCenter + dHalfWidth));

		double dAccumulator[2] = {0.0, 0.0}; // caller asserts iChannels <= 2
		double dWeightSum = 0.0;
		for (int64_t iTap = iFirstTap; iTap <= iLastTap; ++iTap)
		{
			double dDelta = dCenter - static_cast<double>(iTap);
			double dWindowArg = dDelta / dHalfWidth;
			if (dWindowArg <= -1.0 || dWindowArg >= 1.0)
			{
				continue;
			}
			double dWindow = BesselI0(kdKaiserBeta * std::sqrt(1.0 - dWindowArg * dWindowArg)) * dInverseI0Beta;
			double dWeight = NormalizedSinc(dCutoff * dDelta) * dWindow;
			int64_t iClampedTap = std::clamp(iTap, static_cast<int64_t>(0), iSourceFrames - 1); // repeat edges
			for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
			{
				dAccumulator[iChannel] += static_cast<double>(rfSamples[iClampedTap * iChannels + iChannel]) * dWeight;
			}
			dWeightSum += dWeight;
		}

		double dInverseWeightSum = (dWeightSum != 0.0) ? 1.0 / dWeightSum : 0.0;
		for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
		{
			fResampled[iOut * iChannels + iChannel] = static_cast<float>(dAccumulator[iChannel] * dInverseWeightSum);
		}
	}

	rfSamples.swap(fResampled);
	LOG(kDefault, kInfo, "{}: resampled {} -> {} Hz ({} -> {} frames)", relativeFile, iSourceRate, iTargetRate, iSourceFrames, iTargetFrames);
}

void RepairAudio(std::vector<float>& rfSamples, int64_t iChannels, int64_t iSamplesPerSec, std::string_view relativeFile, bool bLoopAsset, bool bAllowDeclip, bool bAllowEdgeFades)
{
	if (rfSamples.empty())
	{
		return;
	}

	// Pass order is load-bearing: scrub before any mean/peak, DC before
	// declip/normalize/fades, declip before normalize (reconstruction overshoot is absorbed there),
	// seam/fades last on final sample values.
	int64_t iScrubbed = ScrubNonFinite(rfSamples);
	if (iScrubbed > 0)
	{
		LOG(kDefault, kWarning, "{}: scrubbed {} non-finite samples to 0", relativeFile, iScrubbed);
	}

	bool bDcCorrected = false;
	for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
	{
		float fMean = RemoveDcOffset(rfSamples, iChannels, iChannel);
		if (fMean != 0.0f)
		{
			bDcCorrected = true;
			LOG(kDefault, kWarning, "{}: DC offset {:+.4f} removed on channel {}", relativeFile, fMean, iChannel);
		}
	}

	for (int64_t iChannel = 0; iChannel < iChannels; ++iChannel)
	{
		DeclipChannel(rfSamples, iChannels, iChannel, bAllowDeclip, relativeFile);
	}

	NormalizePeak(rfSamples, relativeFile);

	if (bLoopAsset)
	{
		ValidateLoopSeam(rfSamples, iChannels, relativeFile, bDcCorrected);
	}
	else
	{
		ApplyEdgeFades(rfSamples, iChannels, iSamplesPerSec, relativeFile, bAllowEdgeFades);
	}
}

} // namespace audiorepair
