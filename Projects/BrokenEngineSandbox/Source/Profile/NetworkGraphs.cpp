#include "Pch.h"

#if defined(BT_CLIENT)

#include "ProfileManager.h"

#include "Network/Client/Client.h"

namespace game
{

static ImPlotPoint SmoothedGetter(int iIndex, void* pData)
{
	common::Smoothed<int64_t>* pSmoothed = static_cast<common::Smoothed<int64_t>*>(pData);
	int64_t iStart = pSmoothed->miNext - pSmoothed->miCount;
	if (iStart < 0)
	{
		iStart += common::Smoothed<int64_t>::kiCapacity;
	}
	int64_t iActual = (iStart + iIndex) % common::Smoothed<int64_t>::kiCapacity;
	return ImPlotPoint(static_cast<double>(iIndex), static_cast<double>(pSmoothed->mpValues[iActual]));
}

static void PlotSmoothed(const char* pLabel, common::Smoothed<int64_t>& rSmoothed)
{
	if (ImPlot::BeginPlot(pLabel, ImVec2(-1.0f, 120.0f * engine::UiScale())))
	{
		ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
		ImPlot::PlotLineG(pLabel, SmoothedGetter, &rSmoothed, static_cast<int>(rSmoothed.miCount));
		ImPlot::EndPlot();
	}
}

void ProfileManager::RenderImPlotGraphs()
{
	if (meProfileScreen != engine::ProfileScreen::kNetwork)
	{
		return;
	}

	if (engine::gpClient == nullptr)
	{
		return;
	}

	const float fUiScale = engine::UiScale();
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 10.0f * fUiScale), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.48f, ImGui::GetIO().DisplaySize.y - 20.0f * fUiScale), ImGuiCond_Always);

	if (ImGui::Begin("Network Graphs", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
	{
		PlotSmoothed("RTT (ms)", mSmoothedRtt);
		PlotSmoothed("Jitter (ms)", mSmoothedJitter);
		PlotSmoothed("Rollback (ticks)", mSmoothedRollback);
		PlotSmoothed("Server Buffer", mSmoothedBuffer);
		PlotSmoothed("Clock Error", mSmoothedClockError);
	}
	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
