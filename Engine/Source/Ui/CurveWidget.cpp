#include "CurveWidget.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{

constexpr float kfPointRadius = 6.0f;
constexpr float kfPointRadiusHover = 9.0f;
constexpr float kfHitRadius = 24.0f;
constexpr int kiCurveSamples = 256;

ImU32 Rgba(float fR, float fG, float fB, float fA)
{
	return ImGui::GetColorU32(ImVec4(fR, fG, fB, fA));
}

int FindPointAt(const CurveData& rCurve, ImVec2 mouse, ImVec2 min, ImVec2 size, float fYRange)
{
	int iBest = -1;
	float fBestDist = kfHitRadius * kfHitRadius;
	for (int i = 0; i < rCurve.GetPointCount(); ++i)
	{
		const ImVec2& rPoint = rCurve.GetPoint(i);
		float fPx = min.x + rPoint.x * size.x;
		float fPy = min.y + size.y - ((rPoint.y - rCurve.GetYMin()) / fYRange) * size.y;
		float fDx = mouse.x - fPx;
		float fDy = mouse.y - fPy;
		float fDist = fDx * fDx + fDy * fDy;
		if (fDist < fBestDist)
		{
			fBestDist = fDist;
			iBest = i;
		}
	}
	return iBest;
}

ImVec2 PixelToCurve(ImVec2 pixel, ImVec2 min, ImVec2 size, float fYMin, float fYRange)
{
	float fT = (pixel.x - min.x) / size.x;
	float fY = fYMin + (1.0f - (pixel.y - min.y) / size.y) * fYRange;
	return ImVec2(fT, fY);
}

} // namespace

bool CurveWidget(std::string_view label, CurveData& rCurve)
{
	ImGuiIO& rIo = ImGui::GetIO();
	ImVec2 size(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f);

	char pId[128];
	std::snprintf(pId, sizeof(pId), "##Curve_%.*s", static_cast<int>(label.size()), label.data());

	ImGui::TextUnformatted(label.data(), label.data() + label.size());

	ImVec2 min = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(pId, size);
	ImVec2 max(min.x + size.x, min.y + size.y);
	bool bHovered = ImGui::IsItemHovered();
	bool bActive = ImGui::IsItemActive();

	float fYMin = rCurve.GetYMin();
	float fYMax = rCurve.GetYMax();
	float fYRange = fYMax - fYMin;

	ImDrawList* pDrawList = ImGui::GetWindowDrawList();
	pDrawList->AddRect(min, max, Rgba(0.6f, 0.6f, 0.7f, 1.0f));

	// Integer gridlines on Y, brighter at y=1
	int iYStart = static_cast<int>(std::ceil(fYMin));
	int iYEnd = static_cast<int>(std::floor(fYMax));
	for (int iY = iYStart; iY <= iYEnd; ++iY)
	{
		float fRatio = (static_cast<float>(iY) - fYMin) / fYRange;
		float fPy = min.y + size.y - fRatio * size.y;
		bool bOne = iY == 1;
		ImU32 color = bOne ? Rgba(0.7f, 0.7f, 0.4f, 0.8f) : Rgba(0.3f, 0.3f, 0.35f, 0.6f);
		float fThickness = bOne ? 2.0f : 1.0f;
		pDrawList->AddLine(ImVec2(min.x, fPy), ImVec2(max.x, fPy), color, fThickness);
	}

	// Pass tick overlay — one faint vertical line per spread pass sample position
	for (int i = 0; i < shaders::kiMaxSpreadPasses; ++i)
	{
		float fT = (shaders::kiMaxSpreadPasses > 1)
			? static_cast<float>(i) / static_cast<float>(shaders::kiMaxSpreadPasses - 1)
			: 0.5f;
		float fPx = min.x + fT * size.x;
		pDrawList->AddLine(ImVec2(fPx, min.y), ImVec2(fPx, max.y), Rgba(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
	}

	// Curve polyline
	ImVec2 prev;
	for (int i = 0; i <= kiCurveSamples; ++i)
	{
		float fT = static_cast<float>(i) / static_cast<float>(kiCurveSamples);
		float fY = rCurve.Evaluate(fT);
		float fPx = min.x + fT * size.x;
		float fPy = min.y + size.y - ((fY - fYMin) / fYRange) * size.y;
		ImVec2 cur(fPx, fPy);
		if (i > 0)
		{
			pDrawList->AddLine(prev, cur, Rgba(0.95f, 0.75f, 0.2f, 1.0f), 2.0f);
		}
		prev = cur;
	}

	// Input handling
	static int siDragIndex = -1;
	bool bInteracting = false;

	ImVec2 mouse = rIo.MousePos;
	int iHoverIndex = bHovered ? FindPointAt(rCurve, mouse, min, size, fYRange) : -1;

	if (bActive && siDragIndex >= 0)
	{
		ImVec2 curvePos = PixelToCurve(mouse, min, size, fYMin, fYRange);
		rCurve.MovePoint(siDragIndex, curvePos);
		bInteracting = true;
	}
	else if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (iHoverIndex >= 0)
		{
			siDragIndex = iHoverIndex;
			bInteracting = true;
		}
		else
		{
			ImVec2 curvePos = PixelToCurve(mouse, min, size, fYMin, fYRange);
			int iNew = rCurve.AddPoint(curvePos);
			if (iNew >= 0)
			{
				siDragIndex = iNew;
				bInteracting = true;
			}
		}
	}
	else if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		if (iHoverIndex >= 0 && !rCurve.IsEndpoint(iHoverIndex))
		{
			rCurve.RemovePoint(iHoverIndex);
			bInteracting = true;
		}
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		siDragIndex = -1;
	}

	// Control points (drawn last so they sit on top of the curve)
	for (int i = 0; i < rCurve.GetPointCount(); ++i)
	{
		const ImVec2& rPoint = rCurve.GetPoint(i);
		float fPx = min.x + rPoint.x * size.x;
		float fPy = min.y + size.y - ((rPoint.y - fYMin) / fYRange) * size.y;
		bool bEmphasized = i == iHoverIndex || i == siDragIndex;
		float fRadius = bEmphasized ? kfPointRadiusHover : kfPointRadius;
		ImU32 color = bEmphasized ? Rgba(1.0f, 1.0f, 1.0f, 1.0f) : Rgba(0.95f, 0.75f, 0.2f, 1.0f);
		pDrawList->AddCircleFilled(ImVec2(fPx, fPy), fRadius, color);
		pDrawList->AddCircle(ImVec2(fPx, fPy), fRadius, Rgba(0.1f, 0.1f, 0.1f, 1.0f), 0, 1.5f);
	}

	return bInteracting;
}

} // namespace engine

#endif // BT_CLIENT
