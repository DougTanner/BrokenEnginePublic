#include "ProfileManager.h"

#include "Game.h"
#if defined(BT_CLIENT)
#include "Network/ClientSession.h"
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
: ProfileManagerBase()
{
	gpProfileManager = this;

	if constexpr (kbEnableProfiling)
	{
		BootStart(engine::kBootTimerTotal);
	}
}

ProfileManager::~ProfileManager()
{
	gpProfileManager = nullptr;
}

engine::CpuCounter& ProfileManager::GetCpuCounter(int64_t iIndex)
{
	return iIndex < engine::kEngineCpuCounterCount ? mEngineCpuCounters[iIndex] : mGameCpuCounters[iIndex - engine::kEngineCpuCounterCount];
}

engine::CpuTimer& ProfileManager::GetCpuTimer(int64_t iIndex)
{
	return iIndex < engine::kEngineCpuTimerCount ? mEngineCpuTimers[iIndex] : mGameCpuTimers[iIndex - engine::kEngineCpuTimerCount];
}

int64_t ProfileManager::GetCpuCounterCount() const
{
	return kGameCpuCounterCount;
}

int64_t ProfileManager::GetCpuTimerCount() const
{
	return kGameCpuTimerCount;
}

#if defined(BT_CLIENT)

void ProfileManager::FormatGameScreens(common::Workbuffer& rWorkbuffer)
{
	if (meProfileScreen == engine::ProfileScreen::kFrames)
	{
		rWorkbuffer.Push();
		rWorkbuffer.Append("Frames: ");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mActiveCoords.size()));
		rWorkbuffer.Append(" [");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mHumanGridCoord.x));
		rWorkbuffer.Append(",");
		rWorkbuffer.Append(static_cast<int64_t>(gpGame->mHumanGridCoord.y));
		rWorkbuffer.Append("]\n");

		int32_t iMinX = gpGame->mHumanGridCoord.x;
		int32_t iMaxX = gpGame->mHumanGridCoord.x;
		int32_t iMinY = gpGame->mHumanGridCoord.y;
		int32_t iMaxY = gpGame->mHumanGridCoord.y;
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
				bool bHuman = (x == gpGame->mHumanGridCoord.x && y == gpGame->mHumanGridCoord.y);
				if (bHuman)
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

		if (gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord))
		{
			rWorkbuffer.Append("Tick: ");
			rWorkbuffer.Append(gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.iTick);
			rWorkbuffer.Append("  Time: ");
			rWorkbuffer.AppendFloat(gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.fCurrentTime, 1);
			rWorkbuffer.Append("s");
		}
		engine::gpTextManager->UpdateTextArea(engine::kTextProfileFrameStats, rWorkbuffer.View());
		rWorkbuffer.Pop();
	}

	if (meProfileScreen == engine::ProfileScreen::kNetwork)
	{
		rWorkbuffer.Push();

		// Header with simulation level info
		if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
		{
			constexpr engine::NetworkSimulationConfig kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation);
			rWorkbuffer.Append("Network (Sim: ");
			rWorkbuffer.Append(engine::GetNetworkSimulationName(keNetworkSimulation));
			rWorkbuffer.Append(" ");
			rWorkbuffer.Append(kSimConfig.iPingMinMs);
			rWorkbuffer.Append("-");
			rWorkbuffer.Append(kSimConfig.iPingMaxMs);
			rWorkbuffer.Append("ms)");
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
			ENetPeer* pPeer = engine::gpClient->GetServerPeer();
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
				rWorkbuffer.AppendFloat(engine::gpClient->GetPipelineRttUs() / 1000.0f, 1);
				rWorkbuffer.Append(" ms\n");

				float fLoss = pPeer->packetLoss * 100.0f / 65536.0f;
				rWorkbuffer.Append("Loss: ");
				rWorkbuffer.AppendFloat(fLoss, 1);
				rWorkbuffer.Append("%");
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					constexpr engine::NetworkSimulationConfig kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation);
					if (fLoss > kSimConfig.fPacketLossPercent * 2.0f)
					{
						rWorkbuffer.Append("!");
					}
					rWorkbuffer.Append(" (sim: ");
					rWorkbuffer.AppendFloat(kSimConfig.fPacketLossPercent, 1);
					rWorkbuffer.Append("%)");
				}
				rWorkbuffer.Append("\n");

				rWorkbuffer.Append("Pkt Loss: ");
				rWorkbuffer.AppendFloat(engine::gpClient->GetPacketLossPercent(), 1);
				rWorkbuffer.Append("%  Jitter: ");
				rWorkbuffer.AppendFloat(engine::gpClient->GetJitterUs() / 1000.0f, 1);
				rWorkbuffer.Append(" ms\n");
			}

			rWorkbuffer.Append("In: ");
			AppendBytes(rWorkbuffer, engine::gpClient->GetBytesInPerSecond());
			rWorkbuffer.Append("  Out: ");
			AppendBytes(rWorkbuffer, engine::gpClient->GetBytesOutPerSecond());

			// -- Sync --
			rWorkbuffer.Append("\n-- Sync --\n");
			{
				int64_t iMinAckFloor = -1;
				int64_t iTotalRecv = 0;
				for (const auto& rSlot : engine::gpClient->GetCoordSlots())
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
			if (gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord))
			{
				mSmoothedRollback = gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.iTick - gpClientSession->GetHumanConfirmedTick();
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
			bool bDesync = gpClientSession->GetDesyncTick() >= 0;
			if (bDesync)
			{
				rWorkbuffer.Append("Yes (");
				rWorkbuffer.Append(gpClientSession->GetDesyncTick());
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

		engine::gpTextManager->UpdateTextArea(engine::kTextProfileFps, rWorkbuffer.View());
		rWorkbuffer.Pop();
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
	if (iCrcValidated > 0) mCrcValidatedTicksPerSecond.Set(iCrcValidated);
	if (iAssumed > 0) mAssumedTicksPerSecond.Set(iAssumed);
	if (iCrcFastPath > 0) mCrcFastPathEventsPerSecond.Set(iCrcFastPath);
	if (iStatusChangeReplay > 0) mStatusChangeReplayTicksPerSecond.Set(iStatusChangeReplay);
	if (iKnockOnReplay > 0) mKnockOnReplayTicksPerSecond.Set(iKnockOnReplay);
}

#endif // BT_CLIENT

} // namespace game
