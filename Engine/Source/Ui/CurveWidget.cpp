#include "CurveWidget.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{

constexpr int kiCurveSamples = 256;
constexpr float kfPointRadius = 8.0f;    // control-point dot radius + DragPoint grab half-size in px (was 5, widened so nearby clicks grab)
constexpr float kfGrabTolerance = 24.0f; // pixel radius within which a left-click targets an existing point instead of adding a new one
constexpr ImVec4 kGoldColor(0.95f, 0.75f, 0.2f, 1.0f);

} // namespace

bool CurveWidget(std::string_view label, CurveData& rCurve)
{
	ImGuiIO& rIo = ImGui::GetIO();
	ImVec2 size(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f);

	char pId[128];
	std::snprintf(pId, sizeof(pId), "##Curve_%.*s", static_cast<int>(label.size()), label.data());

	ImGui::TextUnformatted(label.data(), label.data() + label.size());

	float fYMin = rCurve.GetYMin();
	float fYMax = rCurve.GetYMax();

	bool bInteracting = false;

	// CanvasOnly drops title/legend/menus/box-select/mouse-text but keeps the axis gridlines.
	if (ImPlot::BeginPlot(pId, size, ImPlotFlags_CanvasOnly))
	{
		// Lock the view to [0,1] x [YMin,YMax] and hide tick labels — the curve editor is a fixed canvas, not a pannable chart.
		static constexpr ImPlotAxisFlags kAxisFlags = ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock;
		ImPlot::SetupAxes(nullptr, nullptr, kAxisFlags, kAxisFlags);
		ImPlot::SetupAxesLimits(0.0, 1.0, static_cast<double>(fYMin), static_cast<double>(fYMax), ImPlotCond_Always);

		// Faint vertical tick per spread-pass sample position (drawn first so it sits behind the curve).
		float pfPassTicks[shaders::kiMaxSpreadPasses] {};
		for (int i = 0; i < shaders::kiMaxSpreadPasses; ++i)
		{
			float fT = 0.5f;
			if constexpr (shaders::kiMaxSpreadPasses > 1)
			{
				fT = static_cast<float>(i) / static_cast<float>(shaders::kiMaxSpreadPasses - 1);
			}
			pfPassTicks[i] = fT;
		}
		ImPlotSpec passSpec;
		passSpec.LineColor = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
		ImPlot::PlotInfLines("##Passes", pfPassTicks, shaders::kiMaxSpreadPasses, passSpec);

		// Brighter horizontal reference line at y=1 when it falls inside the range.
		if (fYMin <= 1.0f && fYMax >= 1.0f)
		{
			float fOne = 1.0f;
			ImPlotSpec oneSpec;
			oneSpec.LineColor = ImVec4(0.7f, 0.7f, 0.4f, 0.8f);
			oneSpec.LineWeight = 2.0f;
			oneSpec.Flags = ImPlotInfLinesFlags_Horizontal;
			ImPlot::PlotInfLines("##YOne", &fOne, 1, oneSpec);
		}

		// Curve polyline (256 samples through the monotone-cubic evaluator).
		float pfX[kiCurveSamples + 1] {};
		float pfY[kiCurveSamples + 1] {};
		for (int i = 0; i <= kiCurveSamples; ++i)
		{
			float fT = static_cast<float>(i) / static_cast<float>(kiCurveSamples);
			pfX[i] = fT;
			pfY[i] = rCurve.Evaluate(fT);
		}
		ImPlotSpec curveSpec;
		curveSpec.LineColor = kGoldColor;
		curveSpec.LineWeight = 2.0f;
		ImPlot::PlotLine("##Curve", pfX, pfY, kiCurveSamples + 1, curveSpec);

		// Nearest existing control point within the grab tolerance (pixel space) — drives add-suppression and
		// right-click delete so a click aimed at a point never spawns a stray one, mirroring the old ~24px feel.
		ImVec2 mousePixels = ImGui::GetMousePos();
		int iNearestPoint = -1;
		const float fGrabTolerance = kfGrabTolerance * UiScale();
		float fNearestDistSq = fGrabTolerance * fGrabTolerance;
		for (int i = 0; i < rCurve.GetPointCount(); ++i)
		{
			const ImVec2& rPoint = rCurve.GetPoint(i);
			ImVec2 pointPixels = ImPlot::PlotToPixels(static_cast<double>(rPoint.x), static_cast<double>(rPoint.y));
			float fDx = mousePixels.x - pointPixels.x;
			float fDy = mousePixels.y - pointPixels.y;
			float fDistSq = fDx * fDx + fDy * fDy;
			if (fDistSq < fNearestDistSq)
			{
				fNearestDistSq = fDistSq;
				iNearestPoint = i;
			}
		}

		// Left-click empty space adds a point; right-click a non-endpoint deletes it (context menu disabled via CanvasOnly).
		// Handled before the DragPoint loop so a freshly added point's DragPoint below grabs the still-active click
		// this frame (add-then-drag); the pixel tolerance above suppresses a stray add next to an existing point.
		if (ImPlot::IsPlotHovered())
		{
			if (iNearestPoint < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				ImPlotPoint mouse = ImPlot::GetPlotMousePos();
				if (rCurve.AddPoint(ImVec2(static_cast<float>(mouse.x), static_cast<float>(mouse.y))) >= 0)
				{
					bInteracting = true;
				}
			}
			else if (iNearestPoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !rCurve.IsEndpoint(iNearestPoint))
			{
				rCurve.RemovePoint(iNearestPoint);
				bInteracting = true;
			}
		}

		// Draggable control points (drawn last so they sit atop the curve). CurveData owns endpoint X-locking and
		// interior neighbor clamping, so the dragged value is fed straight through MovePoint and re-read next frame.
		for (int i = 0; i < rCurve.GetPointCount(); ++i)
		{
			const ImVec2& rPoint = rCurve.GetPoint(i);
			double fX = rPoint.x;
			double fY = rPoint.y;
			bool bHeld = false;
			if (ImPlot::DragPoint(i, &fX, &fY, kGoldColor, kfPointRadius * UiScale(), ImPlotDragToolFlags_Delayed, nullptr, nullptr, &bHeld))
			{
				rCurve.MovePoint(i, ImVec2(static_cast<float>(fX), static_cast<float>(fY)));
				bInteracting = true;
			}
			if (bHeld)
			{
				bInteracting = true;
			}
		}

		ImPlot::EndPlot();
	}

	return bInteracting;
}

} // namespace engine

#endif // BT_CLIENT
