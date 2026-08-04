#include "ProfileManager.h"

#include "Game.h"
#if defined(BT_CLIENT)
#include "Network/Client/ClientSession.h"
#endif

namespace game
{

namespace
{

#if defined(BT_CLIENT)
void AppendBytes(common::Workbuffer& rWorkbuffer, int64_t iBytes)
{
	if (iBytes >= 1024 * 1024)
	{
		rWorkbuffer.AppendFloat(static_cast<float>(iBytes) / (1024.0f * 1024.0f), 1);
		rWorkbuffer.Append(" MB/s");
	}
	else
	{
		rWorkbuffer.AppendFloat(static_cast<float>(iBytes) / 1024.0f, 1);
		rWorkbuffer.Append(" KB/s");
	}
}
#endif // BT_CLIENT

} // namespace

ProfileManager* gpProfileManager = nullptr;

ProfileManager::ProfileManager()
: ProfileManagerBase(mGameCpuCounters, mGameCpuTimers, kGameCpuCounterNames, kGameCpuTimerNames, kGameCpuCounterCount, kGameCpuTimerCount)
{
	ASSERT(gpProfileManager == nullptr);

	gpProfileManager = this;

	if constexpr (kbProfiling)
	{
#if defined(BT_SERVER)
		RegisterRawCpuTimer(kCpuTimerPostRenderUpdateNavQuery);
		RegisterRawCpuTimerEvent(kCpuTimerPostRenderUpdateNavQuery);
#endif // BT_SERVER

		BootStart(engine::kBootTimerTotal);
	}
}

#if defined(BT_SERVER)

void ProfileManager::OnRawCpuTimersLatched(int64_t iSampleTick)
{
	if constexpr (kbProfiling)
	{
		const engine::RawCpuTimerRecord rawRecord = GetRawCpuTimer(kCpuTimerPostRenderUpdateNavQuery);
		if (rawRecord.iInvocationCount == 8)
		{
			PublishRawCpuTimerEvent(kCpuTimerPostRenderUpdateNavQuery, iSampleTick);
		}
	}
}

#endif // BT_SERVER

ProfileManager::~ProfileManager()
{
	if (gpProfileManager == this)
	{
		gpProfileManager = nullptr;
	}
}

#if defined(BT_CLIENT)

void ProfileManager::FormatGameScreens(common::Workbuffer& rWorkbuffer)
{
	if (meProfileScreen == engine::ProfileScreen::kFrames)
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		rWorkbuffer.Append("Frames: ");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mActiveCoords.size()));
		rWorkbuffer.Append(" [");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mClientGridCoord.x));
		rWorkbuffer.Append(",");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mClientGridCoord.y));
		rWorkbuffer.Append("]\n");

		int32_t iMinX = gpGame->mClientGridCoord.x;
		int32_t iMaxX = gpGame->mClientGridCoord.x;
		int32_t iMinY = gpGame->mClientGridCoord.y;
		int32_t iMaxY = gpGame->mClientGridCoord.y;
		for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
		{
			iMinX = std::min(iMinX, rCoord.x);
			iMaxX = std::max(iMaxX, rCoord.x);
			iMinY = std::min(iMinY, rCoord.y);
			iMaxY = std::max(iMaxY, rCoord.y);
		}
		for (int32_t y = iMaxY; y >= iMinY; --y)
		{
			for (int32_t x = iMinX; x <= iMaxX; ++x)
			{
				bool bClient = (x == gpGame->mClientGridCoord.x && y == gpGame->mClientGridCoord.y);
				if (bClient)
				{
					rWorkbuffer.Append("P");
				}
				else
				{
					bool bActive = false;
					for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
					{
						if (rCoord.x == x && rCoord.y == y)
						{
							bActive = true;
							break;
						}
					}
					rWorkbuffer.Append(bActive ? "#" : "O");
				}
			}
			rWorkbuffer.Append("\n");
		}

		auto profileCoordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		if (profileCoordIt != gpGame->mCoordFrames.end() && profileCoordIt->second.iSnapshotCount > 0)
		{
			const game::Frame& rRenderFrame = gpGame->RenderFrame(gpGame->mClientGridCoord);
			rWorkbuffer.Append("Tick: ");
			rWorkbuffer.Append(rRenderFrame.interpolate.iTick);
			rWorkbuffer.Append("  Time: ");
			rWorkbuffer.AppendFloat(rRenderFrame.interpolate.fCurrentTime, 1);
			rWorkbuffer.Append("s");
		}
		engine::gpImGuiManager->UpdateTextArea(engine::kTextProfileFrameStats, rWorkbuffer.View());
	}

	if (meProfileScreen == engine::ProfileScreen::kNetwork)
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		// Header with simulation level info
		if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
		{
			constexpr engine::NetworkSimulationConfig kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation);
			rWorkbuffer.Append("Network (Sim: ");
			if (gpGame->mTimeStep.miTimeMultiply > 1)
			{
				rWorkbuffer.Append("BYPASS ");
				rWorkbuffer.Append(engine::GetNetworkSimulationName(keNetworkSimulation));
				rWorkbuffer.Append(")");
			}
			else
			{
				rWorkbuffer.Append(engine::GetNetworkSimulationName(keNetworkSimulation));
				rWorkbuffer.Append(" ");
				rWorkbuffer.Append(kSimConfig.iPingMinMs);
				rWorkbuffer.Append("-");
				rWorkbuffer.Append(kSimConfig.iPingMaxMs);
				rWorkbuffer.Append("ms)");
			}
		}
		else
		{
			rWorkbuffer.Append("Network (Sim: Off)");
		}

		if (engine::gpClient == nullptr)
		{
			rWorkbuffer.Append("\n(Offline)");
		}
		else
		{
			// -- Transport --
			rWorkbuffer.Append("\n-- Transport --\n");
			ENetPeer* pPeer = engine::gpClient->mpServerPeer;
			if (pPeer != nullptr)
			{
				int64_t iRtt = static_cast<int64_t>(pPeer->roundTripTime);
				rWorkbuffer.Append("RTT: ");
				rWorkbuffer.Append(iRtt);
				rWorkbuffer.Append(" ms");
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					constexpr engine::NetworkSimulationConfig kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation);
					if (iRtt > kSimConfig.iPingMaxMs * 3 / 2)
					{
						rWorkbuffer.Append("!");
					}
				}

				rWorkbuffer.Append("  Pipe: ");
				rWorkbuffer.AppendFloat(engine::gpClient->mSmoothedPipelineRttUs.Get() / 1000.0f, 1);
				rWorkbuffer.Append(" ms\n");

				float fLoss = pPeer->packetLoss * 100.0f / 65536.0f;
				rWorkbuffer.Append("Loss: ");
				rWorkbuffer.AppendFloat(fLoss, 1);
				rWorkbuffer.Append("%");
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					constexpr engine::NetworkSimulationConfig kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation);
					if (gpGame->mTimeStep.miTimeMultiply > 1)
					{
						rWorkbuffer.Append(" (sim: BYPASS)");
					}
					else
					{
						if (fLoss > kSimConfig.fPacketLossPercent * 2.0f)
						{
							rWorkbuffer.Append("!");
						}
						rWorkbuffer.Append(" (sim: ");
						rWorkbuffer.AppendFloat(kSimConfig.fPacketLossPercent, 1);
						rWorkbuffer.Append("%)");
					}
				}
				rWorkbuffer.Append("\n");

				int64_t iActiveSlotCount = 0;
				for (const engine::ClientCoordSlot& rSlot : engine::gpClient->mCoordSlots)
				{
					if (rSlot.eState == engine::CoordSubscriptionState::kActive)
					{
						++iActiveSlotCount;
					}
				}
				int64_t iExpectedFrames = engine::gpClient->mTimeState.iExpectedUpdatesPerSecond * iActiveSlotCount;
				int64_t iLostFrames = iExpectedFrames - engine::gpClient->mFramesReceived.Get();
				float fPacketLossPercent = iExpectedFrames > 0 && iLostFrames > 0 ? static_cast<float>(iLostFrames) * 100.0f / static_cast<float>(iExpectedFrames) : 0.0f;
				rWorkbuffer.Append("Pkt Loss: ");
				rWorkbuffer.AppendFloat(fPacketLossPercent, 1);
				rWorkbuffer.Append("%  Jitter: ");
				rWorkbuffer.AppendFloat(engine::gpClient->mSmoothedJitterUs.Get() / 1000.0f, 1);
				rWorkbuffer.Append(" ms\n");

				mSmoothedRtt = iRtt;
				mSmoothedRtt.Update();
				mSmoothedJitter = engine::gpClient->mSmoothedJitterUs.Get() / 1000;
				mSmoothedJitter.Update();
			}

			rWorkbuffer.Append("In: ");
			AppendBytes(rWorkbuffer, engine::gpClient->mBytesInPerSecond.Get());
			rWorkbuffer.Append("  Out: ");
			AppendBytes(rWorkbuffer, engine::gpClient->mBytesOutPerSecond.Get());

			// -- Sync --
			rWorkbuffer.Append("\n-- Sync --\n");
			{
				int64_t iMinAckFloor = -1;
				int64_t iTotalRecv = 0;
				for (const engine::ClientCoordSlot& rSlot : engine::gpClient->mCoordSlots)
				{
					if (rSlot.eState == engine::CoordSubscriptionState::kActive)
					{
						if (iMinAckFloor < 0 || rSlot.ackState.iAckFloor < iMinAckFloor)
						{
							iMinAckFloor = rSlot.ackState.iAckFloor;
						}
						iTotalRecv += std::popcount(rSlot.ackState.uiReceivedBitfieldLow) + std::popcount(rSlot.ackState.uiReceivedBitfieldHigh);
					}
				}
				rWorkbuffer.Append("Ack: ");
				rWorkbuffer.Append(iMinAckFloor);
				rWorkbuffer.Append("  Conf: ");
				rWorkbuffer.Append(gpClientSession->GetConfirmedTick());
				mSmoothedRecv = iTotalRecv;
			}
			mSmoothedRecv.Update();
			rWorkbuffer.Append("\nRecv: ");
			rWorkbuffer.Append(mSmoothedRecv.Get());
			rWorkbuffer.Append("/128");

			// -- Prediction --
			rWorkbuffer.Append("\n-- Prediction --\n");
			auto predCoordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
			if (predCoordIt != gpGame->mCoordFrames.end() && predCoordIt->second.iSnapshotCount > 0)
			{
				mSmoothedRollback = gpGame->RenderFrame(gpGame->mClientGridCoord).interpolate.iTick - gpClientSession->GetClientConfirmedTick();
			}
			mSmoothedRollback.Update();
			mSmoothedBuffer = gpClientSession->GetServerUpdateBufferSize();
			mSmoothedBuffer.Update();
			int64_t iRollbackValue = mSmoothedRollback.Get();
			rWorkbuffer.Append("Rollback: ");
			rWorkbuffer.Append(iRollbackValue);
			if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
			{
				if (iRollbackValue > 8)
				{
					rWorkbuffer.Append("!");
				}
			}
			rWorkbuffer.Append("  Buffer: ");
			rWorkbuffer.Append(mSmoothedBuffer.Get());
			rWorkbuffer.Append("\nDesync: ");
			bool bDesync = gpClientSession->mpDesyncManager->GetDesyncTick() >= 0;
			if (bDesync)
			{
				rWorkbuffer.Append("Yes (");
				rWorkbuffer.Append(gpClientSession->mpDesyncManager->GetDesyncTick());
				rWorkbuffer.Append(")");
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					rWorkbuffer.Append("!");
				}
			}
			else
			{
				rWorkbuffer.Append("No");
			}

			// -- Clock --
			rWorkbuffer.Append("\n-- Clock --\n");
			rWorkbuffer.Append("Offset: ");
			rWorkbuffer.Append(mSmoothedClockOffset.Get());
			rWorkbuffer.Append("  Target: -");
			rWorkbuffer.Append(mSmoothedClockTarget.Get());
			rWorkbuffer.Append("  Err: ");
			rWorkbuffer.Append(mSmoothedClockError.Get());

			// -- Reconciliation --
			rWorkbuffer.Append("\n-- Reconciliation --\n");
			int64_t iCrc = mCrcValidatedTicksPerSecond.Get();
			int64_t iAssumed = mAssumedTicksPerSecond.Get();
			int64_t iFast = mCrcFastPathEventsPerSecond.Get();
			int64_t iStatus = mStatusChangeReplayTicksPerSecond.Get();
			int64_t iKnockOn = mKnockOnReplayTicksPerSecond.Get();

			bool bCrcFlag = false;
			bool bAssumedFlag = false;
			bool bFastFlag = false;
			bool bStatusFlag = false;
			bool bKnockOnFlag = false;
			if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
			{
				constexpr engine::NetworkSimulationBounds kBounds = engine::GetNetworkSimulationBounds(keNetworkSimulation);
				bCrcFlag = iCrc < kBounds.iCrcMin;
				bAssumedFlag = iAssumed > kBounds.iAssumedMax;
				bFastFlag = iFast > kBounds.iFastReplayMax;
				bStatusFlag = iStatus > kBounds.iStatusReplayMax;
				bKnockOnFlag = iKnockOn > kBounds.iKnockOnReplayMax;
			}

			rWorkbuffer.Append("CRC: ");
			rWorkbuffer.Append(iCrc);
			rWorkbuffer.Append(bCrcFlag ? "/s!" : "/s");
			rWorkbuffer.Append("  Assumed: ");
			rWorkbuffer.Append(iAssumed);
			rWorkbuffer.Append(bAssumedFlag ? "/s!" : "/s");
			rWorkbuffer.Append("\nReplay: ");
			rWorkbuffer.Append(iFast);
			rWorkbuffer.Append(bFastFlag ? " fast!" : " fast");
			rWorkbuffer.Append("  ");
			rWorkbuffer.Append(iStatus);
			rWorkbuffer.Append(bStatusFlag ? " status!" : " status");
			rWorkbuffer.Append("  ");
			rWorkbuffer.Append(iKnockOn);
			rWorkbuffer.Append(bKnockOnFlag ? " knock-on!" : " knock-on");
		}

		engine::gpImGuiManager->UpdateTextArea(engine::kTextProfileFps, rWorkbuffer.View());
	}
}

void ProfileManager::SetClockCorrection(int64_t iOffset, int64_t iTargetBehind, int64_t iError)
{
	mSmoothedClockOffset = iOffset;
	mSmoothedClockTarget = iTargetBehind;
	mSmoothedClockError = iError;
	mSmoothedClockOffset.Update();
	mSmoothedClockTarget.Update();
	mSmoothedClockError.Update();
}

void ProfileManager::SetReconcileCounters(int64_t iCrcValidated, int64_t iAssumed, int64_t iCrcFastPath, int64_t iStatusChangeReplay, int64_t iKnockOnReplay)
{
	mCrcValidatedTicksPerSecond.Set(iCrcValidated);
	mAssumedTicksPerSecond.Set(iAssumed);
	mCrcFastPathEventsPerSecond.Set(iCrcFastPath);
	mStatusChangeReplayTicksPerSecond.Set(iStatusChangeReplay);
	mKnockOnReplayTicksPerSecond.Set(iKnockOnReplay);
}

#endif // BT_CLIENT

} // namespace game
