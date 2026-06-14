#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class CurveData
{
public:

	static constexpr int kiMaxControlPoints = 16;

	CurveData() = delete;

	CurveData(std::initializer_list<ImVec2> initial, float fYMin, float fYMax)
	: mfYMin(fYMin)
	, mfYMax(fYMax)
	{
		ASSERT(mfYMin < mfYMax);
		ASSERT(initial.size() >= 2);
		mPoints.reserve(kiMaxControlPoints);
		for (const ImVec2& rPoint : initial)
		{
			mPoints.push_back(rPoint);
		}
	}

	~CurveData() = default;

	float GetYMin() const { return mfYMin; }
	float GetYMax() const { return mfYMax; }
	int GetPointCount() const { return static_cast<int>(mPoints.size()); }
	const ImVec2& GetPoint(int iIndex) const { return mPoints[iIndex]; }
	bool IsEndpoint(int iIndex) const { return iIndex == 0 || iIndex == static_cast<int>(mPoints.size()) - 1; }

	// Add a new control point. Clamps X to (0, 1) (endpoints stay reserved at exactly 0 and 1).
	// Returns index of the new point, or -1 if at cap.
	int AddPoint(ImVec2 point)
	{
		if (static_cast<int>(mPoints.size()) >= kiMaxControlPoints)
		{
			return -1;
		}
		point.x = std::clamp(point.x, std::nextafter(0.0f, 1.0f), std::nextafter(1.0f, 0.0f));
		point.y = std::clamp(point.y, mfYMin, mfYMax);
		int iInsert = 1;
		while (iInsert < static_cast<int>(mPoints.size()) && mPoints[iInsert].x < point.x)
		{
			++iInsert;
		}
		mPoints.insert(mPoints.begin() + iInsert, point);
		return iInsert;
	}

	// Remove a non-endpoint point. Silently ignored for endpoints or when at the floor of 2 points.
	void RemovePoint(int iIndex)
	{
		if (iIndex <= 0 || iIndex >= static_cast<int>(mPoints.size()) - 1)
		{
			return;
		}
		if (mPoints.size() <= 2)
		{
			return;
		}
		mPoints.erase(mPoints.begin() + iIndex);
	}

	// Move a point. Endpoints lock X to 0 or 1; interior points clamp X between neighbors
	// (with a small epsilon) so the sort order is preserved.
	void MovePoint(int iIndex, ImVec2 point)
	{
		if (iIndex < 0 || iIndex >= static_cast<int>(mPoints.size()))
		{
			return;
		}
		point.y = std::clamp(point.y, mfYMin, mfYMax);
		if (iIndex == 0)
		{
			mPoints[0] = ImVec2(0.0f, point.y);
			return;
		}
		if (iIndex == static_cast<int>(mPoints.size()) - 1)
		{
			mPoints[iIndex] = ImVec2(1.0f, point.y);
			return;
		}
		constexpr float kfSeparation = 1e-4f;
		float fMinX = mPoints[iIndex - 1].x + kfSeparation;
		float fMaxX = mPoints[iIndex + 1].x - kfSeparation;
		point.x = std::clamp(point.x, fMinX, fMaxX);
		mPoints[iIndex] = point;
	}

	// Sample the curve at fT in [0, 1] using monotone cubic Hermite (Fritsch-Carlson).
	float Evaluate(float fT) const
	{
		fT = std::clamp(fT, 0.0f, 1.0f);
		int iCount = static_cast<int>(mPoints.size());
		if (fT <= mPoints[0].x)
		{
			return mPoints[0].y;
		}
		if (fT >= mPoints[iCount - 1].x)
		{
			return mPoints[iCount - 1].y;
		}
		int i = 0;
		while (i < iCount - 1 && mPoints[i + 1].x < fT)
		{
			++i;
		}
		float fMOut;
		float fMIn;
		ComputeTangents(i, fMOut, fMIn);
		float fX0 = mPoints[i].x;
		float fX1 = mPoints[i + 1].x;
		float fY0 = mPoints[i].y;
		float fY1 = mPoints[i + 1].y;
		float fH = fX1 - fX0;
		float fU = (fT - fX0) / fH;
		float fU2 = fU * fU;
		float fU3 = fU2 * fU;
		float fH00 = 2.0f * fU3 - 3.0f * fU2 + 1.0f;
		float fH10 = fU3 - 2.0f * fU2 + fU;
		float fH01 = -2.0f * fU3 + 3.0f * fU2;
		float fH11 = fU3 - fU2;
		float fY = fH00 * fY0 + fH10 * fH * fMOut + fH01 * fY1 + fH11 * fH * fMIn;
		return std::clamp(fY, mfYMin, mfYMax);
	}

private:

	// Fritsch-Carlson tangents at segment endpoints i (left) and i+1 (right).
	void ComputeTangents(int iSegment, float& rfMLeft, float& rfMRight) const
	{
		rfMLeft = SecantTangent(iSegment);
		rfMRight = SecantTangent(iSegment + 1);
		// Preserve monotonicity on the current segment.
		float fDelta = Secant(iSegment);
		if (fDelta == 0.0f)
		{
			rfMLeft = 0.0f;
			rfMRight = 0.0f;
			return;
		}
		float fAlpha = rfMLeft / fDelta;
		float fBeta = rfMRight / fDelta;
		float fMag = fAlpha * fAlpha + fBeta * fBeta;
		if (fMag > 9.0f)
		{
			float fScale = 3.0f / std::sqrt(fMag);
			rfMLeft = fScale * fAlpha * fDelta;
			rfMRight = fScale * fBeta * fDelta;
		}
	}

	// Average of adjacent secant slopes (or the single adjacent secant at boundaries).
	float SecantTangent(int iIndex) const
	{
		int iCount = static_cast<int>(mPoints.size());
		if (iIndex == 0)
		{
			return Secant(0);
		}
		if (iIndex == iCount - 1)
		{
			return Secant(iCount - 2);
		}
		float fLeft = Secant(iIndex - 1);
		float fRight = Secant(iIndex);
		if (fLeft * fRight <= 0.0f)
		{
			return 0.0f;
		}
		return 0.5f * (fLeft + fRight);
	}

	float Secant(int iSegment) const
	{
		float fDx = mPoints[iSegment + 1].x - mPoints[iSegment].x;
		return (mPoints[iSegment + 1].y - mPoints[iSegment].y) / fDx;
	}

	std::vector<ImVec2> mPoints;
	float mfYMin;
	float mfYMax;
};

} // namespace engine

#endif // BT_CLIENT
