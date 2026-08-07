#include "Agent/AgentCommands.h"

#if defined(BT_CLIENT)

#include "Graphics/Screenshot.h"
#include "Profile/ProfileManager.h"

#include "Agent/AgentScene.h"
#include "Game.h"
#include "Network/Client/ClientSession.h"

namespace game
{

namespace
{

nlohmann::json BuildFullStateFixtureCoordState(engine::GridCoord coord)
{
	nlohmann::json result;
	result["coord"] = {coord.x, coord.y};

	auto coordIt = gpGame->mCoordFrames.find(coord);
	if (coordIt == gpGame->mCoordFrames.end())
	{
		result["present"] = false;
		return result;
	}

	const engine::CoordFrames& rFrames = coordIt->second;
	result["present"] = true;
	result["confirmedTick"] = rFrames.iConfirmedTick;
	result["confirmedOffset"] = rFrames.iConfirmedOffset;
	result["highWaterValidatedTick"] = rFrames.iHighWaterValidatedTick;
	result["lastFullStateTick"] = rFrames.iLastFullStateTick;
	result["snapshotHead"] = rFrames.iSnapshotHead;
	result["snapshotCount"] = rFrames.iSnapshotCount;
	result["lastRenderedTick"] = rFrames.iLastRenderedTick;
	result["lastRenderedTime"] = rFrames.fLastRenderedTime;
	result["serverUpdateCount"] = rFrames.serverUpdates.size();
	result["lastReplayConfirmedTick"] = rFrames.iLastReplayConfirmedTick;
	result["lastReplayServerUpdateCount"] = rFrames.iLastReplayServerUpdateCount;
	result["stuckFrameCount"] = rFrames.iStuckFrameCount;

	result["pendingFullStateTick"] = nullptr;
	if (rFrames.pendingFullState.has_value())
	{
		result["pendingFullStateTick"] = rFrames.pendingFullState->iTick;
	}

	result["firstServerUpdateTick"] = nullptr;
	result["lastServerUpdateTick"] = nullptr;
	if (!rFrames.serverUpdates.empty())
	{
		result["firstServerUpdateTick"] = rFrames.serverUpdates.begin()->first;
		result["lastServerUpdateTick"] = rFrames.serverUpdates.rbegin()->first;
	}

	bool bRingValid = rFrames.iSnapshotHead >= 0 && rFrames.iSnapshotHead < engine::kiNetworkBufferSize
		&& rFrames.iSnapshotCount >= 0 && rFrames.iSnapshotCount <= engine::kiNetworkBufferSize;
	int64_t iPreviousTick = -1;
	for (int64_t i = 0; bRingValid && i < rFrames.iSnapshotCount; ++i)
	{
		int64_t iPhysical = engine::SnapshotIndex(rFrames.iSnapshotHead, i);
		const std::unique_ptr<Frame>& pSnapshot = rFrames.snapshots[iPhysical];
		bRingValid = pSnapshot != nullptr && (i == 0 || pSnapshot->interpolate.iTick == iPreviousTick + 1);
		if (pSnapshot != nullptr)
		{
			iPreviousTick = pSnapshot->interpolate.iTick;
		}
	}
	if (rFrames.iConfirmedTick >= 0)
	{
		bRingValid = bRingValid && rFrames.iConfirmedOffset >= 0 && rFrames.iConfirmedOffset < rFrames.iSnapshotCount;
		if (bRingValid)
		{
			int64_t iConfirmedPhysical = engine::SnapshotIndex(rFrames.iSnapshotHead, rFrames.iConfirmedOffset);
			bRingValid = rFrames.snapshots[iConfirmedPhysical] != nullptr
				&& rFrames.snapshots[iConfirmedPhysical]->interpolate.iTick == rFrames.iConfirmedTick;
		}
	}
	result["ringValid"] = bRingValid;
	result["tailTick"] = rFrames.iSnapshotCount > 0 ? iPreviousTick : -1;
	return result;
}

nlohmann::json BuildFullStateFixtureState()
{
	ClientDesyncManager& rDesyncManager = *gpClientSession->mpDesyncManager;
	nlohmann::json result;
	result["clientTick"] = gpGame->TickCounter();
	result["stalled"] = gpClientSession->mpDesyncManager->IsStalled();
	result["desyncTick"] = gpClientSession->mpDesyncManager->GetDesyncTick();
	result["syntheticStall"] = rDesyncManager.IsAgentFullStateFixtureArmed();
	result["armedTick"] = rDesyncManager.GetAgentFullStateFixtureTick();
	result["timeMultiply"] = gpGame->mTimeStep.miTimeMultiply;
	result["timeDivide"] = gpGame->mTimeStep.miTimeDivide;
	result["coordState"] = BuildFullStateFixtureCoordState(rDesyncManager.GetAgentFullStateFixtureCoord());
	return result;
}

void CommandClientFullStateFixture(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!engine::PhysicalInputSuppressed())
	{
		throw std::runtime_error("client_full_state_fixture requires an agent-mode client");
	}
	if (!rParams.is_object() || !rParams.contains("action") || !rParams.at("action").is_string() || rParams.size() != 1)
	{
		throw std::runtime_error("client_full_state_fixture requires only string 'action'");
	}

	const std::string action = rParams.at("action").get<std::string>();
	ClientDesyncManager& rDesyncManager = *gpClientSession->mpDesyncManager;
	if (action == "clear")
	{
		rDesyncManager.ClearAgentFullStateFixture();
		rResult = BuildFullStateFixtureState();
		return;
	}

	if (action == "arm_stall")
	{
		if (gpClientSession->mpRuntime->mpClient == nullptr || !(gpClientSession->mpRuntime->mpClient->mStateFlags & engine::Client::ClientStateFlags::kConnectionAccepted) || !(gpClientSession->mpRuntime->mpClient->mStateFlags & engine::Client::ClientStateFlags::kConnected) || gpClientSession->mpRuntime->mpClient->mpServerPeer == nullptr)
		{
			throw std::runtime_error("client_full_state_fixture requires an accepted connection");
		}
		if (gpClientSession->mpDesyncManager->IsStalled())
		{
			throw std::runtime_error("client_full_state_fixture is already stalled");
		}

		const engine::GridCoord coord = gpGame->mClientGridCoord;
		bool bActive = std::ranges::any_of(gpClientSession->mpRuntime->mpClient->mCoordSlots, [coord](const engine::ClientCoordSlot& rSlot)
		{
			return rSlot.eState == engine::CoordSubscriptionState::kActive && rSlot.coord == coord;
		});
		auto coordIt = gpGame->mCoordFrames.find(coord);
		if (!bActive || coordIt == gpGame->mCoordFrames.end() || coordIt->second.iConfirmedTick < 0)
		{
			throw std::runtime_error("client_full_state_fixture requires an active confirmed client coord");
		}
		if (coordIt->second.pendingFullState.has_value())
		{
			throw std::runtime_error("client_full_state_fixture requires no pre-existing pending full state");
		}

		rDesyncManager.ArmAgentFullStateFixture(gpGame->TickCounter(), coord);
		rResult = BuildFullStateFixtureState();
		return;
	}

	if (!rDesyncManager.IsAgentFullStateFixtureArmed())
	{
		throw std::runtime_error("client_full_state_fixture is not armed");
	}
	if (action == "inspect")
	{
		rResult = BuildFullStateFixtureState();
		return;
	}
	if (action != "exercise_gap")
	{
		throw std::runtime_error("client_full_state_fixture 'action' must be arm_stall|inspect|exercise_gap|clear");
	}

	try
	{
		const engine::GridCoord coord = rDesyncManager.GetAgentFullStateFixtureCoord();
		auto coordIt = gpGame->mCoordFrames.find(coord);
		if (coordIt == gpGame->mCoordFrames.end() || !coordIt->second.pendingFullState.has_value())
		{
			throw std::runtime_error("client_full_state_fixture requires a received pending full state");
		}

		engine::CoordFrames& rFrames = coordIt->second;
		const int64_t iPendingTick = rFrames.pendingFullState->iTick;
		const int64_t iDeferTargetTick = gpGame->TickCounter();
		if (iPendingTick <= iDeferTargetTick)
		{
			throw std::runtime_error("client_full_state_fixture requires a pending full state ahead of client tick");
		}

		nlohmann::json beforeDefer = BuildFullStateFixtureCoordState(coord);
		ReconcileDesyncInfo deferDesync = gpClientSession->mpReconciler->Run();
		bool bPendingPreserved = rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == iPendingTick;
		nlohmann::json afterDefer = BuildFullStateFixtureCoordState(coord);
		if (!bPendingPreserved)
		{
			throw std::runtime_error("future pending full state was not deferred");
		}

		const int64_t iConfirmedBeforeGap = rFrames.iConfirmedTick;
		auto eraseBegin = rFrames.serverUpdates.upper_bound(iConfirmedBeforeGap);
		auto eraseEnd = rFrames.serverUpdates.upper_bound(iPendingTick);
		const int64_t iRemovedUpdateCount = std::distance(eraseBegin, eraseEnd);
		rFrames.serverUpdates.erase(eraseBegin, eraseEnd);
		int64_t iUncappedConsecutiveEndpoint = iConfirmedBeforeGap;
		while (rFrames.serverUpdates.contains(iUncappedConsecutiveEndpoint + 1))
		{
			++iUncappedConsecutiveEndpoint;
		}
		const bool bDirectAdoptionRequired = iPendingTick > iUncappedConsecutiveEndpoint;
		if (!bDirectAdoptionRequired)
		{
			throw std::runtime_error("client_full_state_fixture failed to create an update gap before pending full state");
		}

		const float fPendingTime = rFrames.pendingFullState->pFrame->interpolate.fCurrentTime;
		gpGame->SetTickCounter(iPendingTick);
		gpGame->SetCurrentTime(fPendingTime);
		gpGame->mTimeStep.ClearAccumulator();
		ReconcileDesyncInfo adoptionDesync = gpClientSession->mpReconciler->Run();

		nlohmann::json afterAdoption = BuildFullStateFixtureCoordState(coord);
		bool bObsoleteUpdatesAbsent = rFrames.serverUpdates.empty() || rFrames.serverUpdates.begin()->first > iPendingTick;
		bool bRenderBaseNotOlder = rFrames.iLastRenderedTick < 0 || rFrames.iSnapshotCount <= 0
			|| rFrames.snapshots[rFrames.iSnapshotHead]->interpolate.iTick >= rFrames.iLastRenderedTick;
		bool bPendingCleared = !rFrames.pendingFullState.has_value();
		bool bAdoptedTicksMatch = rFrames.iConfirmedTick == iPendingTick
			&& rFrames.iHighWaterValidatedTick == iPendingTick
			&& rFrames.iLastFullStateTick == iPendingTick;
		bool bRingHeadIsAdopted = rFrames.iSnapshotCount > 0
			&& rFrames.snapshots[rFrames.iSnapshotHead] != nullptr
			&& rFrames.snapshots[rFrames.iSnapshotHead]->interpolate.iTick == iPendingTick;
		bool bDirectAdoptionProven = bDirectAdoptionRequired && !adoptionDesync.bDesync && bPendingCleared
			&& bAdoptedTicksMatch && rFrames.iConfirmedOffset == 0 && bObsoleteUpdatesAbsent && bRingHeadIsAdopted;

		rResult["pendingTick"] = iPendingTick;
		rResult["deferTargetTick"] = iDeferTargetTick;
		rResult["beforeDefer"] = std::move(beforeDefer);
		rResult["afterDefer"] = std::move(afterDefer);
		rResult["deferPendingPreserved"] = bPendingPreserved;
		rResult["deferDesync"] = deferDesync.bDesync;
		rResult["removedUpdateCount"] = iRemovedUpdateCount;
		rResult["uncappedConsecutiveEndpoint"] = iUncappedConsecutiveEndpoint;
		rResult["directAdoptionRequired"] = bDirectAdoptionRequired;
		rResult["adoptionDesync"] = adoptionDesync.bDesync;
		rResult["afterAdoption"] = std::move(afterAdoption);
		rResult["pendingCleared"] = bPendingCleared;
		rResult["adoptedTicksMatch"] = bAdoptedTicksMatch;
		rResult["confirmedOffsetZero"] = rFrames.iConfirmedOffset == 0;
		rResult["obsoleteUpdatesAbsent"] = bObsoleteUpdatesAbsent;
		rResult["ringHeadIsAdopted"] = bRingHeadIsAdopted;
		rResult["directAdoptionProven"] = bDirectAdoptionProven;
		rResult["renderBaseNotOlderThanLastRendered"] = bRenderBaseNotOlder;
	}
	catch (...)
	{
		rDesyncManager.ClearAgentFullStateFixture();
		throw;
	}

	rDesyncManager.ClearAgentFullStateFixture();
	rResult["cleared"] = true;
}

std::filesystem::path PathFromParam(const nlohmann::json& rValue)
{
	// Agent-supplied path is UTF-8 (trust boundary); .get<std::string>() throws on a non-string.
	std::string utf8 = rValue.get<std::string>();
	return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

int64_t DesyncProbeCountParameter(const nlohmann::json& rParameters, std::string_view parameterName)
{
	std::string parameterNameString(parameterName);
	if (!rParameters.contains(parameterNameString) || !rParameters.at(parameterNameString).is_number_integer())
	{
		throw std::runtime_error("desync_probe '" + parameterNameString + "' must be an integer in [0,8]");
	}

	if (rParameters.at(parameterNameString).is_number_unsigned())
	{
		uint64_t uiCount = rParameters.at(parameterNameString).get<uint64_t>();
		if (uiCount > 8)
		{
			throw std::runtime_error("desync_probe '" + parameterNameString + "' must be an integer in [0,8]");
		}
		return static_cast<int64_t>(uiCount);
	}

	int64_t iCount = rParameters.at(parameterNameString).get<int64_t>();
	if (iCount < 0 || iCount > 8)
	{
		throw std::runtime_error("desync_probe '" + parameterNameString + "' must be an integer in [0,8]");
	}
	return iCount;
}

int32_t KeyHoldFramesParameter(const nlohmann::json& rParameters)
{
	if (!rParameters.contains("holdFrames"))
	{
		return 1;
	}

	const nlohmann::json& rHoldFrames = rParameters.at("holdFrames");
	if (!rHoldFrames.is_number_integer())
	{
		throw std::runtime_error("key 'holdFrames' must be an integer");
	}

	static constexpr int64_t kiMaxKeyHoldFrames = engine::AgentCommandServer::kiDeferredTimeoutDrains - 2;
	if (rHoldFrames.is_number_unsigned())
	{
		uint64_t uiHoldFrames = rHoldFrames.get<uint64_t>();
		if (uiHoldFrames > static_cast<uint64_t>(kiMaxKeyHoldFrames))
		{
			throw std::runtime_error("key 'holdFrames' must be an integer in [0," + std::to_string(kiMaxKeyHoldFrames) + "]");
		}
		return static_cast<int32_t>(uiHoldFrames);
	}

	int64_t iHoldFrames = rHoldFrames.get<int64_t>();
	if (iHoldFrames < 0)
	{
		return 0;
	}
	if (iHoldFrames > kiMaxKeyHoldFrames)
	{
		throw std::runtime_error("key 'holdFrames' must be an integer in [0," + std::to_string(kiMaxKeyHoldFrames) + "]");
	}
	return static_cast<int32_t>(iHoldFrames);
}

enum class CaptureCommandPhase : uint8_t
{
	kAwaitRestore,
	kAwaitResult,
	kAwaitMinimize,
	kDone,
};

struct CaptureCommandState
{
	~CaptureCommandState()
	{
		// Deferred-response timeout/discard does not poll again. Restore the original minimized state when the
		// state object is released so every failure path cleans up before AgentCommandServer publishes or exits.
		if (bRestoreMinimized && ePhase != CaptureCommandPhase::kDone && IsWindow(hwnd) != FALSE && IsIconic(hwnd) == FALSE)
		{
			ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
		}
	}

	HWND hwnd = nullptr;
	CaptureCommandPhase ePhase = CaptureCommandPhase::kAwaitResult;
	bool bRestoreMinimized = false;
	uint64_t uiCaptureToken = 0;
	std::optional<nlohmann::json> result;
};

enum class RenderDocCapturePhase : uint8_t
{
	kAwaitRestore,
	kAwaitCaptures,
	kAwaitMinimize,
	kDone,
};

struct RenderDocCaptureState
{
	~RenderDocCaptureState()
	{
		// Mirror CaptureCommandState: the deferred-response timeout/discard does not poll again, so restore the
		// original minimized state on release, cleaning up before AgentCommandServer publishes or exits.
		if (bRestoreMinimized && ePhase != RenderDocCapturePhase::kDone && IsWindow(hwnd) != FALSE && IsIconic(hwnd) == FALSE)
		{
			ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
		}
	}

	HWND hwnd = nullptr;
	RenderDocCapturePhase ePhase = RenderDocCapturePhase::kAwaitCaptures;
	bool bRestoreMinimized = false;
	uint32_t uiBaselineCaptures = 0;
	int64_t iFrames = 1;
};

template <typename QueueCaptureT>
void BeginCaptureAndDefer(QueueCaptureT queueCapture)
{
	HWND hwnd = engine::gpGraphics->mHwnd;
	bool bRestoreMinimized = IsIconic(hwnd) != FALSE;
	if (!bRestoreMinimized && engine::gpGraphics->mbSwapchainRecreateDeferred)
	{
		// Preserve the existing fast failure when a non-minimized window has no live capture target.
		throw std::runtime_error("window is minimized or swapchain recreate is deferred");
	}

	std::shared_ptr<CaptureCommandState> pState = std::make_shared<CaptureCommandState>();
	pState->hwnd = hwnd;
	pState->bRestoreMinimized = bRestoreMinimized;
	if (bRestoreMinimized)
	{
		pState->ePhase = CaptureCommandPhase::kAwaitRestore;
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	}
	else
	{
		pState->uiCaptureToken = engine::ResetCaptureResult();
		queueCapture(pState->uiCaptureToken);
	}

	engine::gpAgentCommandServer->DeferResponse([pState, queueCapture = std::move(queueCapture)]() mutable -> std::optional<nlohmann::json>
	{
		if (pState->ePhase == CaptureCommandPhase::kAwaitRestore)
		{
			if (IsIconic(pState->hwnd) == FALSE && engine::gpGraphics->ExtentSettled())
			{
				pState->uiCaptureToken = engine::ResetCaptureResult();
				queueCapture(pState->uiCaptureToken);
				pState->ePhase = CaptureCommandPhase::kAwaitResult;
			}
		}

		if (pState->ePhase == CaptureCommandPhase::kAwaitResult)
		{
			pState->result = engine::TakeCaptureResult(pState->uiCaptureToken);
			if (pState->result.has_value())
			{
				if (!pState->bRestoreMinimized)
				{
					pState->ePhase = CaptureCommandPhase::kDone;
				}
				else
				{
					ShowWindow(pState->hwnd, SW_SHOWMINNOACTIVE);
					pState->ePhase = CaptureCommandPhase::kAwaitMinimize;
				}
			}
		}

		if (pState->ePhase == CaptureCommandPhase::kAwaitMinimize)
		{
			if (IsIconic(pState->hwnd) == FALSE)
			{
				return std::nullopt;
			}
			pState->ePhase = CaptureCommandPhase::kDone;
		}

		if (pState->ePhase != CaptureCommandPhase::kDone)
		{
			return std::nullopt;
		}

		nlohmann::json result = std::move(*pState->result);
		pState->result.reset();
		// A failed async save publishes an {"error":...} result; rethrow it so Drain's poll-exception path emits a
		// proper {"ok":false,"error"} envelope instead of wrapping the error JSON as ok:true.
		if (result.contains("error"))
		{
			throw std::runtime_error(result.at("error").get<std::string>());
		}
		return result;
	});
}

// screenshot: capture the live window to a downscaled JPG/PNG. Completes via the deferred-response mechanism once
// the async save records its result. Schema: {"path"?,"maxWidth"?:1568,"format"?:"jpg|png","quality"?:80}.
void CommandScreenshot(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	// The capture consume site (RenderMainPresentAcquire) is compiled out under !kbScreenshots, so a deferred request
	// would never resolve — fail fast instead of blocking the agent channel until the liveness timeout.
	if constexpr (!kbScreenshots)
	{
		throw std::runtime_error("screenshot capture is compiled out (kbScreenshots is false)");
	}

	// BeginCaptureAndDefer temporarily restores an iconic window without activation, waits for its live swapchain,
	// then returns it to the minimized state after the capture result arrives.
	engine::ScreenshotRequest request;
	request.bPublishResult = true;
	if (rParams.contains("path"))
	{
		request.path = PathFromParam(rParams.at("path"));
	}
	if (rParams.contains("maxWidth"))
	{
		request.iMaxWidth = rParams.at("maxWidth").get<int64_t>();
	}
	if (rParams.contains("format"))
	{
		std::string format = rParams.at("format").get<std::string>();
		if (format == "png")
		{
			request.bPng = true;
		}
		else if (format == "jpg" || format == "jpeg")
		{
			request.bPng = false;
		}
		else
		{
			throw std::runtime_error("screenshot 'format' must be 'jpg' or 'png'");
		}
	}
	if (rParams.contains("quality"))
	{
		request.iQuality = std::clamp<int64_t>(rParams.at("quality").get<int64_t>(), 1, 100);
	}

	BeginCaptureAndDefer([request = std::move(request)](uint64_t uiCaptureToken) mutable
	{
		request.uiCaptureToken = uiCaptureToken;
		engine::gpGraphics->mScreenshotRequest = std::move(request);
	});
}

// renderdoc_capture: trigger RenderDoc capture(s) of the upcoming presented frame(s) and return the .rdc path(s).
// Requires a --renderdoc launch (mpRenderDocApi non-null). Like BeginCaptureAndDefer, an iconic window is temporarily
// restored for the live present, then re-minimized after RenderDoc serializes every capture. Completes via the
// deferred-response mechanism once GetNumCaptures() reaches the pre-trigger baseline plus the requested frame count.
// Schema: {"frames"?:1 (1..8)} -> {"paths":[absolute .rdc paths]}.
void CommandRenderDocCapture(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	RENDERDOC_API_1_6_0* pRenderDocApi = engine::gpInstanceManager->mpRenderDocApi;
	if (pRenderDocApi == nullptr)
	{
		throw std::runtime_error("RenderDoc API not available (launch the client with --renderdoc)");
	}

	int64_t iFrames = 1;
	if (rParams.contains("frames"))
	{
		if (!rParams.at("frames").is_number_integer())
		{
			throw std::runtime_error("renderdoc_capture 'frames' must be an integer in [1,8]");
		}
		iFrames = rParams.at("frames").get<int64_t>();
		if (iFrames < 1 || iFrames > 8)
		{
			throw std::runtime_error("renderdoc_capture 'frames' must be an integer in [1,8]");
		}
	}

	HWND hwnd = engine::gpGraphics->mHwnd;
	bool bRestoreMinimized = IsIconic(hwnd) != FALSE;
	if (!bRestoreMinimized && engine::gpGraphics->mbSwapchainRecreateDeferred)
	{
		// Mirror BeginCaptureAndDefer's fast-fail: a deferred swapchain recreate skips present, so a trigger on a
		// non-minimized window with no live target would only ever time out.
		throw std::runtime_error("window is minimized or swapchain recreate is deferred");
	}

	std::shared_ptr<RenderDocCaptureState> pState = std::make_shared<RenderDocCaptureState>();
	pState->hwnd = hwnd;
	pState->bRestoreMinimized = bRestoreMinimized;
	pState->iFrames = iFrames;
	if (bRestoreMinimized)
	{
		pState->ePhase = RenderDocCapturePhase::kAwaitRestore;
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	}
	else
	{
		pState->uiBaselineCaptures = pRenderDocApi->GetNumCaptures();
		if (iFrames == 1)
		{
			pRenderDocApi->TriggerCapture();
		}
		else
		{
			pRenderDocApi->TriggerMultiFrameCapture(static_cast<uint32_t>(iFrames));
		}
	}

	engine::gpAgentCommandServer->DeferResponse([pState, pRenderDocApi]() -> std::optional<nlohmann::json>
	{
		if (pState->ePhase == RenderDocCapturePhase::kAwaitRestore)
		{
			if (IsIconic(pState->hwnd) == FALSE && engine::gpGraphics->ExtentSettled())
			{
				pState->uiBaselineCaptures = pRenderDocApi->GetNumCaptures();
				if (pState->iFrames == 1)
				{
					pRenderDocApi->TriggerCapture();
				}
				else
				{
					pRenderDocApi->TriggerMultiFrameCapture(static_cast<uint32_t>(pState->iFrames));
				}
				pState->ePhase = RenderDocCapturePhase::kAwaitCaptures;
			}
		}

		if (pState->ePhase == RenderDocCapturePhase::kAwaitCaptures)
		{
			// RenderDoc increments GetNumCaptures() only after a capture is fully serialized; the Drain liveness
			// timeout bounds a capture that never lands (the dtor re-minimizes on that path).
			if (pRenderDocApi->GetNumCaptures() < pState->uiBaselineCaptures + static_cast<uint32_t>(pState->iFrames))
			{
				return std::nullopt;
			}
			if (pState->bRestoreMinimized)
			{
				ShowWindow(pState->hwnd, SW_SHOWMINNOACTIVE);
				pState->ePhase = RenderDocCapturePhase::kAwaitMinimize;
			}
			else
			{
				pState->ePhase = RenderDocCapturePhase::kDone;
			}
		}

		if (pState->ePhase == RenderDocCapturePhase::kAwaitMinimize)
		{
			if (IsIconic(pState->hwnd) == FALSE)
			{
				return std::nullopt;
			}
			pState->ePhase = RenderDocCapturePhase::kDone;
		}

		if (pState->ePhase != RenderDocCapturePhase::kDone)
		{
			return std::nullopt;
		}

		nlohmann::json paths = nlohmann::json::array();
		for (uint32_t uiIndex = pState->uiBaselineCaptures; uiIndex < pState->uiBaselineCaptures + static_cast<uint32_t>(pState->iFrames); ++uiIndex)
		{
			// Two-call GetCapture: first with a null buffer to size the path (length includes the NUL), then read it.
			uint32_t uiPathLength = 0;
			if (pRenderDocApi->GetCapture(uiIndex, nullptr, &uiPathLength, nullptr) == 0)
			{
				throw std::runtime_error("renderdoc_capture: capture index unavailable");
			}
			std::string capturePath(uiPathLength, '\0');
			pRenderDocApi->GetCapture(uiIndex, capturePath.data(), &uiPathLength, nullptr);
			capturePath.resize(uiPathLength > 0 ? uiPathLength - 1 : 0);

			if (!std::filesystem::exists(std::filesystem::path(reinterpret_cast<const char8_t*>(capturePath.c_str()))))
			{
				throw std::runtime_error("renderdoc_capture: capture file missing");
			}
			paths.push_back(std::move(capturePath));
		}

		nlohmann::json result;
		result["paths"] = std::move(paths);
		return result;
	});
}

// resize: change the live client window/framebuffer size mid-session by driving the real HWND resize
// (SetWindowPos -> synchronous WM_SIZE -> gWantedFramebufferExtent2D -> swapchain recreate). Client dims are a
// trust boundary: validated to [320x180, 16384x16384] then rounded up to a multiple of 8 (mirrors SetupWindow).
// The swapchain additionally clamps to surface caps, so the applied extent may differ from the requested one.
// Completes synchronously if already at the requested extent, else via the deferred-response mechanism once the
// swapchain has recreated. Never mutates the persisted gFullscreen setting. Schema: {"width","height"}.
void CommandResize(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("width") || !rParams.at("width").is_number() || !rParams.contains("height") || !rParams.at("height").is_number())
	{
		throw std::runtime_error("resize requires numeric 'width' and 'height'");
	}

	int64_t iWidth = rParams.at("width").get<int64_t>();
	int64_t iHeight = rParams.at("height").get<int64_t>();
	if (iWidth < 320 || iWidth > 16384 || iHeight < 180 || iHeight > 16384)
	{
		throw std::runtime_error("resize 'width'/'height' out of bounds [320x180, 16384x16384]");
	}

	// Multiple-of-8 rounding for reproducible geometry (matches SetupWindow's client-size rounding).
	LONG iClientWidth = common::RoundUp<LONG, 8>(static_cast<LONG>(iWidth));
	LONG iClientHeight = common::RoundUp<LONG, 8>(static_cast<LONG>(iHeight));

	// Reject while minimized: WM_SIZE never fires for a minimized window, so the deferred poll can't converge.
	if (IsIconic(engine::gpGraphics->mHwnd))
	{
		throw std::runtime_error("window is minimized");
	}

	// Style-aware client -> outer conversion: WS_OVERLAPPEDWINDOW grows the client rect by the frame via
	// AdjustWindowRect; WS_POPUP (borderless windowed-fullscreen) has no frame, so client size is the outer size.
	HWND hwnd = engine::gpGraphics->mHwnd;
	LONG_PTR iStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
	RECT outerRect {.left = 0, .top = 0, .right = iClientWidth, .bottom = iClientHeight};
	if ((iStyle & WS_OVERLAPPEDWINDOW) != 0)
	{
		AdjustWindowRect(&outerRect, static_cast<DWORD>(iStyle), FALSE);
	}
	LONG iOuterWidth = outerRect.right - outerRect.left;
	LONG iOuterHeight = outerRect.bottom - outerRect.top;

	// On-screen clamping: a fixed top-left with a growing size can push the bottom/right edges off screen. Query
	// the window's current monitor (mirrors SetupWindow) and keep the outer rect fully within rcMonitor.
	RECT currentRect {};
	GetWindowRect(hwnd, &currentRect);
	LONG iPosX = currentRect.left;
	LONG iPosY = currentRect.top;

	MONITORINFO monitorInfo = {};
	monitorInfo.cbSize = sizeof(monitorInfo);
	GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
	const RECT& rMonitor = monitorInfo.rcMonitor;

	if (iClientWidth == rMonitor.right - rMonitor.left && iClientHeight == rMonitor.bottom - rMonitor.top)
	{
		// Rounded client size exactly the monitor's pixel size: land at the monitor origin (pixel-accurate).
		iPosX = rMonitor.left;
		iPosY = rMonitor.top;
	}
	else
	{
		// Otherwise keep the current position but shift left/up so the outer rect stays fully on the monitor;
		// if the window is larger than the monitor in a dimension, pin that axis to the monitor origin.
		if (iPosX + iOuterWidth > rMonitor.right)
		{
			iPosX = rMonitor.right - iOuterWidth;
		}
		if (iPosX < rMonitor.left)
		{
			iPosX = rMonitor.left;
		}
		if (iPosY + iOuterHeight > rMonitor.bottom)
		{
			iPosY = rMonitor.bottom - iOuterHeight;
		}
		if (iPosY < rMonitor.top)
		{
			iPosY = rMonitor.top;
		}
	}

	// Drain() runs on the WndProc thread, so this SetWindowPos's WM_SIZE fires synchronously and writes
	// gWantedFramebufferExtent2D exactly as a human drag does. No z-order / activation change.
	SetWindowPos(hwnd, nullptr, iPosX, iPosY, iOuterWidth, iOuterHeight, SWP_NOZORDER | SWP_NOACTIVATE);

	// Fast path: if the swapchain already sits at the requested (rounded) extent, answer synchronously.
	if (engine::gpGraphics->mFramebufferExtent2D.width == static_cast<uint32_t>(iClientWidth) && engine::gpGraphics->mFramebufferExtent2D.height == static_cast<uint32_t>(iClientHeight))
	{
		rResult["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
		rResult["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		return;
	}

	engine::gpAgentCommandServer->DeferResponse([]() -> std::optional<nlohmann::json>
	{
		// Graphics::Refresh copies gWantedFramebufferExtent2D into mFramebufferExtent2D when it triggers the recreate
		// (Graphics.cpp), and CreateSwapchain's defined branch may then clamp gWantedFramebufferExtent2D down to the
		// surface-cap currentExtent (SwapchainManager.cpp), so the two converge only once the applied extent settles.
		// ExtentSettled() also requires no recreate be deferred — extent equality alone is satisfied the moment Refresh
		// copies the wanted extent, before the defer gate returns, which would report a false success no live swapchain
		// backs. Poll until settled, then report the applied extent (may differ from the requested one after clamping).
		if (!engine::gpGraphics->ExtentSettled())
		{
			return std::nullopt;
		}
		nlohmann::json result;
		result["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
		result["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		return result;
	});
}

// fullscreen: toggle the live client between borderless windowed-fullscreen (WS_POPUP) and windowed
// (WS_OVERLAPPEDWINDOW) mid-session by driving the engine's WantedFullscreen() -> main-loop reconciliation style-switch path
// via an agent override. 'on' is a trust boundary: validated to a bool. Idempotent — an already-in-state request
// answers synchronously. Never mutates the persisted gFullscreen setting; windowed restore returns to the launch
// extent (a mid-run resize is not preserved). Schema: {"on"}; result: {"fullscreen","width","height"}.
void CommandFullscreen(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("on") || !rParams.at("on").is_boolean())
	{
		throw std::runtime_error("fullscreen requires boolean 'on'");
	}

	bool bOn = rParams.at("on").get<bool>();

	// Reject while minimized: the style toggle drives a swapchain recreate that can't converge off-screen, so the
	// deferred poll would never settle (mirrors resize).
	if (IsIconic(engine::gpGraphics->mHwnd))
	{
		throw std::runtime_error("window is minimized");
	}

	// Live style read: WS_POPUP set == borderless windowed-fullscreen; WS_OVERLAPPEDWINDOW == windowed.
	HWND hwnd = engine::gpGraphics->mHwnd;
	bool bIsFullscreen = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_POPUP) != 0;

	// Fast path: already in the requested mode — report the current extent synchronously (idempotence).
	if (bIsFullscreen == bOn)
	{
		rResult["fullscreen"] = bIsFullscreen;
		rResult["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
		rResult["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		return;
	}

	// Install the override; the per-frame main-loop reconciliation (in MainThread, just after ProcessMessages()) picks it
	// up and performs the style swap + SetupWindow + swapchain recreate. The command completes via the deferred-response mechanism once it lands.
	engine::SetAgentFullscreenOverride(bOn);

	engine::gpAgentCommandServer->DeferResponse([bOn]() -> std::optional<nlohmann::json>
	{
		// Poll until BOTH the live WS_POPUP bit matches the request AND the extent has settled. ExtentSettled() folds
		// extent equality with !mbSwapchainRecreateDeferred, so it can't report a false success while a recreate is
		// still deferred (the style flips a frame before the swapchain finishes). Then report the applied state.
		bool bNowFullscreen = (GetWindowLongPtr(engine::gpGraphics->mHwnd, GWL_STYLE) & WS_POPUP) != 0;
		if (bNowFullscreen != bOn || !engine::gpGraphics->ExtentSettled())
		{
			return std::nullopt;
		}
		nlohmann::json result;
		result["fullscreen"] = bNowFullscreen;
		result["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
		result["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		return result;
	});
}

// window_state: minimize the live client window or restore it mid-session by driving ShowWindow on the real HWND.
// 'minimized' is a trust boundary: validated to a bool. Idempotent — an already-in-state request answers synchronously.
// Minimize uses SW_MINIMIZE; restore uses SW_SHOWNOACTIVATE (a no-activate restore — never steal foreground focus, per
// the agent-mode convention in Main.cpp). Never mutates the persisted gFullscreen setting or any .bin. This command
// produces the minimized/recreate-deferred state that resize/fullscreen reject; captures temporarily restore it. Completes
// synchronously if already in state, else via the deferred-response mechanism once the window state settles. Schema:
// {"minimized"}; result: {"minimized"}, plus {"width","height"} of the settled extent on restore.
void CommandWindowState(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("minimized") || !rParams.at("minimized").is_boolean())
	{
		throw std::runtime_error("window_state requires boolean 'minimized'");
	}

	bool bMinimized = rParams.at("minimized").get<bool>();

	HWND hwnd = engine::gpGraphics->mHwnd;

	// Fast path: already in the requested state — report it synchronously (idempotence), including the current extent
	// on restore (matches resize/fullscreen).
	if ((IsIconic(hwnd) != FALSE) == bMinimized)
	{
		rResult["minimized"] = bMinimized;
		if (!bMinimized)
		{
			rResult["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
			rResult["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		}
		return;
	}

	if (bMinimized)
	{
		// Minimize, then poll until the window reports iconic.
		ShowWindow(hwnd, SW_MINIMIZE);
		engine::gpAgentCommandServer->DeferResponse([]() -> std::optional<nlohmann::json>
		{
			if (IsIconic(engine::gpGraphics->mHwnd) == FALSE)
			{
				return std::nullopt;
			}
			nlohmann::json result;
			result["minimized"] = true;
			return result;
		});
		return;
	}

	// No-activate restore (never steal foreground focus, per Main.cpp's agent-mode convention). Poll until the window
	// is no longer iconic AND the extent has settled — a bare !IsIconic restore would report success before the deferred
	// swapchain recreate lands, the false-success class the sibling polls already guard against.
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	engine::gpAgentCommandServer->DeferResponse([]() -> std::optional<nlohmann::json>
	{
		if (IsIconic(engine::gpGraphics->mHwnd) != FALSE || !engine::gpGraphics->ExtentSettled())
		{
			return std::nullopt;
		}
		nlohmann::json result;
		result["minimized"] = false;
		result["width"] = engine::gpGraphics->mFramebufferExtent2D.width;
		result["height"] = engine::gpGraphics->mFramebufferExtent2D.height;
		return result;
	});
}

// dump_render_target: read back an offscreen render target and encode it (normalized grayscale PNG for
// single-channel, direct PNG for 4x8-bit, optional raw .bin). Schema: {"name","index"?:0,"channel"?:0,"path"?,"raw"?:false}.
void CommandDumpRenderTarget(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	// As CommandScreenshot: the readback consume site is compiled out under !kbScreenshots, so fail fast.
	if constexpr (!kbScreenshots)
	{
		throw std::runtime_error("dump_render_target is compiled out (kbScreenshots is false)");
	}

	// As CommandScreenshot: BeginCaptureAndDefer temporarily restores an iconic window for the live readback.
	if (!rParams.contains("name") || !rParams.at("name").is_string())
	{
		throw std::runtime_error("dump_render_target requires string 'name'");
	}

	engine::DumpRenderTargetRequest request;
	request.bPublishResult = true;
	request.name = rParams.at("name").get<std::string>();
	if (rParams.contains("index"))
	{
		request.iIndex = rParams.at("index").get<int64_t>();
	}
	if (rParams.contains("channel"))
	{
		request.iChannel = rParams.at("channel").get<int64_t>();
	}
	if (rParams.contains("path"))
	{
		request.path = PathFromParam(rParams.at("path"));
	}
	if (rParams.contains("raw"))
	{
		request.bRaw = rParams.at("raw").get<bool>();
	}

	// Trust-boundary validation now (unknown name / bad index / non-encodable format) so errors report synchronously.
	engine::ValidateDumpRenderTargetRequest(request);

	BeginCaptureAndDefer([request = std::move(request)](uint64_t uiCaptureToken) mutable
	{
		request.uiCaptureToken = uiCaptureToken;
		engine::gpGraphics->mDumpRenderTargetRequest = std::move(request);
	});
}

// Full structured dump of the last completed ImGui frame (registry read table) plus game UI state.
nlohmann::json BuildDescribeUi()
{
	nlohmann::json result;
	result["uiState"] = UiStateName(gpGame->meUiState);
	result["gameFlags"] = GameFlagNames(gpGame->mGameFlags);

	result["framebuffer"] = {engine::gpGraphics->mFramebufferExtent2D.width, engine::gpGraphics->mFramebufferExtent2D.height};

	ImVec2 mousePos = (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetIO().MousePos : ImVec2(0.0f, 0.0f);
	result["mouse"] = {mousePos.x, mousePos.y};

	nlohmann::json windows = nlohmann::json::array();
	nlohmann::json items = nlohmann::json::array();
	if (engine::gpAgentUiRegistry != nullptr)
	{
		for (int64_t i = 0; i < engine::gpAgentUiRegistry->WindowCount(); ++i)
		{
			const engine::AgentUiWindow& rWindow = engine::gpAgentUiRegistry->Window(i);
			windows.push_back({{"name", rWindow.pcName}, {"rect", {rWindow.f4Rect.x, rWindow.f4Rect.y, rWindow.f4Rect.z, rWindow.f4Rect.w}}, {"focused", rWindow.bFocused}});
		}
		for (int64_t i = 0; i < engine::gpAgentUiRegistry->ItemCount(); ++i)
		{
			const engine::AgentUiItem& rItem = engine::gpAgentUiRegistry->Item(i);
			if (rItem.pcLabel[0] == '\0')
			{
				continue; // no label recorded (invisible / no ItemInfo) — not addressable, omit
			}
			items.push_back(
			{
				{"label", rItem.pcLabel},
				{"window", rItem.pcWindow},
				{"rect", {rItem.f4Rect.x, rItem.f4Rect.y, rItem.f4Rect.z, rItem.f4Rect.w}},
				{"disabled", rItem.bDisabled},
				{"checked", (rItem.iStatusFlags & ImGuiItemStatusFlags_Checked) != 0},
				{"inputable", (rItem.iStatusFlags & ImGuiItemStatusFlags_Inputable) != 0},
				{"hovered", (rItem.iStatusFlags & ImGuiItemStatusFlags_HoveredRect) != 0},
				{"visible", (rItem.iStatusFlags & ImGuiItemStatusFlags_Visible) != 0},
			});
		}
	}
	result["windows"] = std::move(windows);
	result["items"] = std::move(items);
	return result;
}

// Comma-joined list of candidate labels (optionally filtered to one window) for a not-found / ambiguous error.
std::string CandidateLabels(const char* pcWindow)
{
	std::string candidates;
	if (engine::gpAgentUiRegistry == nullptr)
	{
		return candidates;
	}
	for (int64_t i = 0; i < engine::gpAgentUiRegistry->ItemCount(); ++i)
	{
		const engine::AgentUiItem& rItem = engine::gpAgentUiRegistry->Item(i);
		if (rItem.pcLabel[0] == '\0')
		{
			continue;
		}
		if (pcWindow != nullptr && std::strcmp(rItem.pcWindow, pcWindow) != 0)
		{
			continue;
		}
		if (!candidates.empty())
		{
			candidates += ", ";
		}
		candidates += rItem.pcLabel;
		if (candidates.size() > 1024) // bound the error message
		{
			candidates += ", ...";
			break;
		}
	}
	return candidates;
}

// Win32 VK code for a named key (letters/digits directly; a small symbolic table for the game bindings).
int32_t ParseKeyVk(const std::string& rName)
{
	if (rName.size() == 1)
	{
		char cChar = rName[0];
		if (cChar >= 'a' && cChar <= 'z')
		{
			cChar = static_cast<char>(cChar - 'a' + 'A');
		}
		if ((cChar >= 'A' && cChar <= 'Z') || (cChar >= '0' && cChar <= '9'))
		{
			return static_cast<int32_t>(static_cast<unsigned char>(cChar));
		}
	}
	if (rName == "ESC" || rName == "ESCAPE")
	{
		return VK_ESCAPE;
	}
	if (rName == "SPACE")
	{
		return VK_SPACE;
	}
	if (rName == "TAB")
	{
		return VK_TAB;
	}
	if (rName == "ENTER" || rName == "RETURN")
	{
		return VK_RETURN;
	}
	if (rName == "UP")
	{
		return VK_UP;
	}
	if (rName == "DOWN")
	{
		return VK_DOWN;
	}
	if (rName == "LEFT")
	{
		return VK_LEFT;
	}
	if (rName == "RIGHT")
	{
		return VK_RIGHT;
	}
	if (rName.size() >= 2 && (rName[0] == 'F' || rName[0] == 'f'))
	{
		int32_t iNumber = std::atoi(rName.c_str() + 1);
		if (iNumber >= 1 && iNumber <= 24)
		{
			return VK_F1 + (iNumber - 1);
		}
	}
	throw std::runtime_error("unknown key");
}

// Fill a bounded char buffer from a string param (trust boundary — .get<std::string>() throws on non-string).
void CopyStringParam(char* pcDst, int64_t iDstSize, const std::string& rSource)
{
	int64_t i = 0;
	for (; i < iDstSize - 1 && i < static_cast<int64_t>(rSource.size()); ++i)
	{
		pcDst[i] = rSource[i];
	}
	pcDst[i] = '\0';
}

// Shared label-target parse for click / hover / set_slider.
void FillLabelTarget(const nlohmann::json& rParams, engine::AgentScript& rScript)
{
	if (!rParams.contains("label") || !rParams.at("label").is_string())
	{
		throw std::runtime_error("command requires string 'label'");
	}
	CopyStringParam(rScript.pcLabel, static_cast<int64_t>(sizeof(rScript.pcLabel)), rParams.at("label").get<std::string>());
	if (rParams.contains("window"))
	{
		CopyStringParam(rScript.pcWindow, static_cast<int64_t>(sizeof(rScript.pcWindow)), rParams.at("window").get<std::string>());
		rScript.bHasWindow = true;
	}
}

// Begin a script (throwing "busy" if one is already running) and defer the response until the script completes.
// For label-based scripts pcErrorWindow (may be null) drives the candidate list on a not-found / ambiguous error.
void BeginScriptAndDefer(const engine::AgentScript& rScript, bool bDescribeUiAfter, bool bLabelBased, bool bHasWindow, std::string errorWindow)
{
	if (!engine::gpAgentInput->BeginScript(rScript))
	{
		throw std::runtime_error("busy");
	}

	engine::gpAgentCommandServer->DeferResponse([bDescribeUiAfter, bLabelBased, bHasWindow, errorWindow]() -> std::optional<nlohmann::json>
	{
		engine::AgentScriptStatus eStatus = engine::gpAgentInput->ScriptStatus();
		if (eStatus == engine::AgentScriptStatus::kPending)
		{
			return std::nullopt;
		}
		if (eStatus == engine::AgentScriptStatus::kTimeout)
		{
			throw std::runtime_error("timed out");
		}
		if (eStatus == engine::AgentScriptStatus::kNotFound)
		{
			throw std::runtime_error("no widget matches label; candidates: " + CandidateLabels(bHasWindow ? errorWindow.c_str() : nullptr));
		}
		if (eStatus == engine::AgentScriptStatus::kAmbiguous)
		{
			throw std::runtime_error("ambiguous label; candidates: " + CandidateLabels(bHasWindow ? errorWindow.c_str() : nullptr));
		}
		if (eStatus == engine::AgentScriptStatus::kNotInputable)
		{
			throw std::runtime_error("target is not inputable");
		}
		if (eStatus == engine::AgentScriptStatus::kClipped)
		{
			throw std::runtime_error("target not visible (scrolled out of view)");
		}

		nlohmann::json result;
		if (bLabelBased)
		{
			result["found"] = true;
			result["enabled"] = !engine::gpAgentInput->ResolvedDisabled();
		}
		else
		{
			result["ok"] = true;
		}
		if (bDescribeUiAfter)
		{
			result["ui"] = BuildDescribeUi();
		}
		return result;
	});
}

void CommandDescribeUi([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	rResult = BuildDescribeUi();
}

// click {label, window?, timeoutFrames?=120, describeUiAfter?=true}: stabilize target rect, press/release the left
// mouse button at its center through ImGui IO, then optionally dump the post-click UI.
void CommandClick(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kClick;
	FillLabelTarget(rParams, script);
	if (rParams.contains("timeoutFrames"))
	{
		script.iTimeoutFrames = std::max<int32_t>(0, static_cast<int32_t>(rParams.at("timeoutFrames").get<int64_t>()));
	}
	bool bDescribeUiAfter = !rParams.contains("describeUiAfter") || rParams.at("describeUiAfter").get<bool>();
	BeginScriptAndDefer(script, bDescribeUiAfter, true, script.bHasWindow, script.pcWindow);
}

// hover {label, window?, holdFrames?=2}: move to and stabilize on the target, hold, then dump the UI.
void CommandHover(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kHover;
	FillLabelTarget(rParams, script);
	if (rParams.contains("holdFrames"))
	{
		script.iHoldFrames = std::max<int32_t>(0, static_cast<int32_t>(rParams.at("holdFrames").get<int64_t>()));
	}
	BeginScriptAndDefer(script, true, true, script.bHasWindow, script.pcWindow);
}

// set_slider {label, window?, value}: Ctrl+Click the slider to open ImGui temp-input, type the value, commit (Enter).
void CommandSetSlider(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if (!rParams.contains("value"))
	{
		throw std::runtime_error("set_slider requires 'value'");
	}
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kSetSlider;
	FillLabelTarget(rParams, script);
	double dValue = rParams.at("value").get<double>();
	std::snprintf(script.pcValueText, sizeof(script.pcValueText), "%g", dValue);
	BeginScriptAndDefer(script, false, true, script.bHasWindow, script.pcWindow);
}

// key {key, holdFrames?=1}: hold then release a named VK through the RawInput overlay, driving KeyboardPressed edges.
void CommandKey(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if (!rParams.contains("key") || !rParams.at("key").is_string())
	{
		throw std::runtime_error("key requires string 'key'");
	}
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kKey;
	script.iKeyVk = ParseKeyVk(rParams.at("key").get<std::string>());
	script.iHoldFrames = KeyHoldFramesParameter(rParams);
	BeginScriptAndDefer(script, false, false, false, std::string());
}

// mouse {x, y, action:"move|down|up|click|wheel", button?="left", notches?}: raw pixel coords feeding both the ImGui
// IO sink and the RawInput overlay (normalized) for world clicks / unlabeled targets and camera-zoom wheel.
// (x/y optional for wheel — both or neither)
void CommandMouse(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kMouse;

	std::string action = rParams.contains("action") ? rParams.at("action").get<std::string>() : "move";
	if (action == "move")
	{
		script.eMouseAction = engine::AgentMouseAction::kMove;
	}
	else if (action == "down")
	{
		script.eMouseAction = engine::AgentMouseAction::kDown;
	}
	else if (action == "up")
	{
		script.eMouseAction = engine::AgentMouseAction::kUp;
	}
	else if (action == "click")
	{
		script.eMouseAction = engine::AgentMouseAction::kClick;
	}
	else if (action == "wheel")
	{
		script.eMouseAction = engine::AgentMouseAction::kWheel;
	}
	else
	{
		throw std::runtime_error("mouse 'action' must be move|down|up|click|wheel");
	}

	if (script.eMouseAction == engine::AgentMouseAction::kWheel)
	{
		script.iWheelNotches = rParams.contains("notches") ? static_cast<int32_t>(rParams.at("notches").get<int64_t>()) : 1;

		// Optional target coords: both present routes the ImGui wheel to the window under (x,y); neither leaves the
		// previous pin in place. Either way the camera also zooms unless that hovered window can actually scroll.
		bool bHasX = rParams.contains("x");
		bool bHasY = rParams.contains("y");
		if (bHasX != bHasY)
		{
			throw std::runtime_error("mouse wheel requires both 'x' and 'y' or neither");
		}
		if (bHasX)
		{
			script.f2CoordPixels[0] = static_cast<float>(rParams.at("x").get<double>());
			script.f2CoordPixels[1] = static_cast<float>(rParams.at("y").get<double>());
			script.bHasCoord = true;
		}
	}
	else
	{
		if (!rParams.contains("x") || !rParams.contains("y"))
		{
			throw std::runtime_error("mouse requires 'x' and 'y'");
		}
		script.f2CoordPixels[0] = static_cast<float>(rParams.at("x").get<double>());
		script.f2CoordPixels[1] = static_cast<float>(rParams.at("y").get<double>());
		script.bHasCoord = true;

		std::string button = rParams.contains("button") ? rParams.at("button").get<std::string>() : "left";
		if (button == "left")
		{
			script.iImGuiMouseButton = 0;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonLeft;
		}
		else if (button == "right")
		{
			script.iImGuiMouseButton = 1;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonRight;
		}
		else if (button == "middle")
		{
			script.iImGuiMouseButton = 2;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonMiddle;
		}
		else
		{
			throw std::runtime_error("mouse 'button' must be left|right|middle");
		}
	}

	BeginScriptAndDefer(script, false, false, false, std::string());
}

// desync_probe: exercise the real desync packet and recovery paths without manufacturing a simulation mismatch.
// Schema: {"desyncReports"?:int[0,8],"debugFrameRequests"?:int[0,8]} OR {"triggerRecovery":true}.
void CommandDesyncProbe(const nlohmann::json& rParameters, nlohmann::json& rResult)
{
	// Heap: validation errors, JSON result, and triggerRecovery's serialized Frame snapshot
	ScopedSuppressAllocationTracking suppress;

	if (!rParameters.is_object())
	{
		throw std::runtime_error("desync_probe params must be an object");
	}

	size_t iKnownParameterCount = static_cast<size_t>(rParameters.contains("desyncReports")) + static_cast<size_t>(rParameters.contains("debugFrameRequests")) + static_cast<size_t>(rParameters.contains("triggerRecovery"));
	if (rParameters.size() != iKnownParameterCount)
	{
		throw std::runtime_error("desync_probe accepts only 'desyncReports', 'debugFrameRequests', and 'triggerRecovery'");
	}

	int64_t iDesyncReportCount = rParameters.contains("desyncReports") ? DesyncProbeCountParameter(rParameters, "desyncReports") : 0;
	int64_t iDebugFrameRequestCount = rParameters.contains("debugFrameRequests") ? DesyncProbeCountParameter(rParameters, "debugFrameRequests") : 0;
	bool bTriggerRecovery = false;
	if (rParameters.contains("triggerRecovery"))
	{
		if (!rParameters.at("triggerRecovery").is_boolean())
		{
			throw std::runtime_error("desync_probe 'triggerRecovery' must be a boolean");
		}
		bTriggerRecovery = rParameters.at("triggerRecovery").get<bool>();
		if (!bTriggerRecovery)
		{
			throw std::runtime_error("desync_probe 'triggerRecovery' must be true when present");
		}
	}
	if (bTriggerRecovery && (rParameters.contains("desyncReports") || rParameters.contains("debugFrameRequests")))
	{
		throw std::runtime_error("desync_probe 'triggerRecovery' is mutually exclusive with packet counts");
	}

	if (gpGame == nullptr || gpClientSession == nullptr || gpClientSession->mpRuntime->mpClient == nullptr || !(gpClientSession->mpRuntime->mpClient->mStateFlags & engine::Client::ClientStateFlags::kConnected) || gpClientSession->mpRuntime->mpClient->mpServerPeer == nullptr || gpGame->InMainMenu())
	{
		throw std::runtime_error("desync_probe requires a connected live client/server session");
	}

	auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
	if (coordIt == gpGame->mCoordFrames.end() || coordIt->second.iSnapshotCount <= 0)
	{
		throw std::runtime_error("desync_probe requires a current client frame");
	}
	int64_t iSnapshotIndex = engine::SnapshotIndex(coordIt->second.iSnapshotHead, coordIt->second.iSnapshotCount - 1);
	const std::unique_ptr<Frame>& pCurrentFrame = coordIt->second.snapshots[iSnapshotIndex];
	if (pCurrentFrame == nullptr)
	{
		throw std::runtime_error("desync_probe requires a current client frame");
	}

	const int64_t iTick = pCurrentFrame->interpolate.iTick;
	const engine::GridCoord coord = gpGame->mClientGridCoord;
	const common::crc_t uiExpectedCrc = pCurrentFrame->postRender.sharedCrc;
	const common::crc_t uiActualCrc = uiExpectedCrc ^ static_cast<common::crc_t>(1);

	for (int64_t i = 0; i < iDesyncReportCount; ++i)
	{
		gpClientSession->mpRuntime->mpClient->SendDesyncReport(iTick, coord, uiExpectedCrc, uiActualCrc);
	}
	for (int64_t i = 0; i < iDebugFrameRequestCount; ++i)
	{
		gpClientSession->mpRuntime->mpClient->SendDebugFrameRequest(iTick, coord);
	}

	if (bTriggerRecovery)
	{
		if (gpClientSession->mpDesyncManager->IsStalled())
		{
			throw std::runtime_error("desync_probe cannot trigger recovery while desync debug mode is already stalled");
		}

		std::ostringstream outputStream(std::ios::binary);
		outputStream << *pCurrentFrame;
		std::istringstream inputStream(outputStream.str(), std::ios::binary);
		std::unique_ptr<Frame> pSnapshot = std::make_unique<Frame>();
		inputStream >> *pSnapshot;

		ReconcileDesyncInfo desyncInfo
		{
			.bDesync = true,
			.iDesyncTick = iTick,
			.desyncCoord = coord,
			.desyncExpectedCrc = uiExpectedCrc,
			.desyncActualCrc = uiActualCrc,
			.pDesyncClientFrame = std::move(pSnapshot),
		};
		gpClientSession->mpDesyncManager->OnDesyncDetected(std::move(desyncInfo));
	}

	rResult["tick"] = iTick;
	rResult["coord"] = {coord.x, coord.y};
	rResult["desyncDebugFrames"] = kbDesyncDebugFrames;
	rResult["stalled"] = gpClientSession->mpDesyncManager->IsStalled();
	rResult["desyncReports"] = iDesyncReportCount;
	rResult["debugFrameRequests"] = iDebugFrameRequestCount;
	rResult["triggerRecovery"] = bTriggerRecovery;
}

void CommandQueryProfile(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.is_object() || !rParams.empty())
	{
		throw std::runtime_error("query_profile requires empty params");
	}

	engine::GpuTimer* pGpuTimers = gpProfileManager->GetGpuTimers();
	nlohmann::json gpuTimers = nlohmann::json::array();
	for (int64_t i = 0; i < engine::kGpuTimerCount; ++i)
	{
		engine::GpuTimer& rGpuTimer = pGpuTimers[i];
		nlohmann::json gpuTimer;
		gpuTimer["index"] = i;
		gpuTimer["name"] = std::string(engine::kGpuTimerNames[i]);
		gpuTimer["currentUs"] = rGpuTimer.smoothedMicroseconds.Current();
		gpuTimer["averageUs"] = rGpuTimer.smoothedMicroseconds.Average();
		gpuTimer["maxUs"] = rGpuTimer.smoothedMicroseconds.Max();
		gpuTimers.push_back(std::move(gpuTimer));
	}
	rResult["gpuTimers"] = std::move(gpuTimers);
	const engine::GpuShadowSample& rShadowSample = gpProfileManager->mGpuShadowSample;
	rResult["shadowSample"] =
	{
		{"sequence", rShadowSample.uiSequence},
		{"currentUs", rShadowSample.iCurrentMicroseconds},
	};
}

} // namespace

bool ExecuteAgentCommandClient(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (cmd == "client_full_state_fixture")
	{
		CommandClientFullStateFixture(rParams, rResult);
		return true;
	}
	if (cmd == "screenshot")
	{
		CommandScreenshot(rParams, rResult);
		return true;
	}
	if (cmd == "renderdoc_capture")
	{
		CommandRenderDocCapture(rParams, rResult);
		return true;
	}
	if (cmd == "resize")
	{
		CommandResize(rParams, rResult);
		return true;
	}
	if (cmd == "fullscreen")
	{
		CommandFullscreen(rParams, rResult);
		return true;
	}
	if (cmd == "window_state")
	{
		CommandWindowState(rParams, rResult);
		return true;
	}
	if (cmd == "dump_render_target")
	{
		CommandDumpRenderTarget(rParams, rResult);
		return true;
	}
	if (cmd == "describe_ui")
	{
		CommandDescribeUi(rParams, rResult);
		return true;
	}
	if (cmd == "click")
	{
		CommandClick(rParams, rResult);
		return true;
	}
	if (cmd == "hover")
	{
		CommandHover(rParams, rResult);
		return true;
	}
	if (cmd == "set_slider")
	{
		CommandSetSlider(rParams, rResult);
		return true;
	}
	if (cmd == "key")
	{
		CommandKey(rParams, rResult);
		return true;
	}
	if (cmd == "mouse")
	{
		CommandMouse(rParams, rResult);
		return true;
	}
	if (cmd == "describe_scene")
	{
		CommandDescribeScene(rParams, rResult);
		return true;
	}
	if (cmd == "desync_probe")
	{
		CommandDesyncProbe(rParams, rResult);
		return true;
	}
	if (cmd == "query_profile")
	{
		CommandQueryProfile(rParams, rResult);
		return true;
	}
	return false;
}

} // namespace game

#endif // defined(BT_CLIENT)
