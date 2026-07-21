Remote Entity Snapshot Interpolation
================================================================================
NOTE: Line numbers indicative only — anchor on symbols.

PROBLEM
-------
Remote entities (non-client coords) render from the latest extrapolated state.
When server updates are delayed or lost, entities freeze then teleport to the
corrected position. This is visually jarring.

SOLUTION
--------
Render remote entities at (current_time - interpolation_delay), interpolating
between two known server-confirmed states. As long as the buffer holds 2+
states, motion is smooth regardless of packet loss.

At 32 Hz (kiTickRate=32), each tick = 31.25ms.
- 2-frame buffer (~62ms delay): survives 1 lost packet
- 3-frame buffer (~94ms delay): survives 2 lost packets

The client coord (the cell the local client follows) is EXCLUDED -- it uses
prediction + visual smoothing via Camera (Camera.cpp:78-87, velocity extrapolation
from position delta).

SCOPE
-----
- Per-coord ring buffer of confirmed entity states (position + direction)
- Interpolation at render time between two bracketing states
- Buffer underrun: extrapolate from last known state, or freeze if no data


================================================================================
1. DATA STRUCTURES
================================================================================

FILE: Engine/Source/GameBase.h

Add a new struct and per-coord buffer inside the #if defined(BT_CLIENT) block
of CoordFrames (struct at line 39):

  struct InterpolationSnapshot
  {
      int64_t iTick = 0;
      float fTime = 0.0f;  // mfCurrentTime at this tick

      // Per-player: parallel arrays matching PlayersInterpolate layout
      int64_t iPlayerCount = 0;
      XMVECTOR pVecPlayerPositions[kiMaxPlayersPerFrame] {};
      XMVECTOR pVecPlayerDirections[kiMaxPlayersPerFrame] {};

      // Per-spaceship: parallel arrays matching SpaceshipsInterpolate layout
      int64_t iSpaceshipCount = 0;
      XMVECTOR pVecSpaceshipPositions[kiMaxSpaceshipsPerFrame] {};
      XMVECTOR pVecSpaceshipDirections[kiMaxSpaceshipsPerFrame] {};
  };

  static constexpr int64_t kiInterpolationBufferSize = 4;

  InterpolationSnapshot interpolationBuffer[kiInterpolationBufferSize] {};
  int64_t iInterpolationHead = 0;
  int64_t iInterpolationCount = 0;

Reset these in ResetClientState() (line 104).

NOTE ON kiMaxPlayersPerFrame / kiMaxSpaceshipsPerFrame: These will be small
fixed constants (e.g., 16/32) chosen to match the maximum entity counts per
frame. Check existing spawn limits in HealthDamage.h or Frame constants.
If no suitable constant exists, define them alongside InterpolationSnapshot.
Using fixed arrays avoids heap allocation in the hot path.


================================================================================
2. RECORDING CONFIRMED STATES
================================================================================

FILE: Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp
(ApplyCoordWriteback moved here from ClientReconciler.cpp; ~lines 44-53)

After reconciliation writes back confirmed state to CoordFrames (in
ApplyCoordWriteback, called from ApplyResult), record an
interpolation snapshot for each non-client coord.

Specifically, after the writeback updates iConfirmedTick (GameBase.h:54),
add a call to a new helper:

  RecordInterpolationSnapshot(rSub, confirmedFrame);

Where confirmedFrame is the frame at the new confirmed tick.

NEW FUNCTION (as a static helper in ReconcileReplay.cpp):

  static void RecordInterpolationSnapshot(CoordFrames& rSub, const game::Frame& rFrame)
  {
      int64_t iSlot = (rSub.iInterpolationHead + rSub.iInterpolationCount) % CoordFrames::kiInterpolationBufferSize;
      if (rSub.iInterpolationCount >= CoordFrames::kiInterpolationBufferSize)
      {
          // Buffer full: advance head, overwrite oldest
          rSub.iInterpolationHead = (rSub.iInterpolationHead + 1) % CoordFrames::kiInterpolationBufferSize;
      }
      else
      {
          ++rSub.iInterpolationCount;
      }

      CoordFrames::InterpolationSnapshot& rSnapshot = rSub.interpolationBuffer[iSlot];
      rSnapshot.iTick = rFrame.interpolate.iTick;
      rSnapshot.fTime = rFrame.interpolate.fCurrentTime;

      // Players
      const auto& rPlayers = *rFrame.interpolate.pPlayers;
      rSnapshot.iPlayerCount = std::min(rPlayers.iCount, static_cast<int64_t>(kiMaxPlayersPerFrame));
      for (int64_t i = 0; i < rSnapshot.iPlayerCount; ++i)
      {
          rSnapshot.pVecPlayerPositions[i] = rPlayers.pVecPositions[i];
          rSnapshot.pVecPlayerDirections[i] = rPlayers.pVecDirections[i];
      }

      // Spaceships
      const auto& rSpaceships = *rFrame.interpolate.pSpaceships;
      rSnapshot.iSpaceshipCount = std::min(rSpaceships.iCount, static_cast<int64_t>(kiMaxSpaceshipsPerFrame));
      for (int64_t i = 0; i < rSnapshot.iSpaceshipCount; ++i)
      {
          rSnapshot.pVecSpaceshipPositions[i] = rSpaceships.pVecPositions[i];
          rSnapshot.pVecSpaceshipDirections[i] = rSpaceships.pVecDirections[i];
      }
  }


================================================================================
3. INTERPOLATION AT RENDER TIME
================================================================================

FILE: Engine/Source/GameBase.cpp

In GameBase::Render() (line 266), after computing fCurrentTime and
before the per-frame interpolate loop (line 276 interpolateFrame lambda):

Add a new step that, for each non-client active coord, computes interpolated
positions and overwrites the FrameInterpolate data in mRenderInterpolates.

  float fRenderTime = fCurrentTime;  // already computed at line 249
  static constexpr float kfInterpolationDelay = 2.0f * game::kfDeltaTime; // 2 ticks

After interpolateFrame() runs for each coord (after line 289), apply
interpolation overrides for non-client coords:

  for (const GridCoord& rCoord : rActiveCoords)
  {
      if (rCoord == cameraCoord) continue;  // client coord uses prediction

      auto subIt = mCoordFrames.find(rCoord);
      if (subIt == mCoordFrames.end()) continue;

      const CoordFrames& rSub = subIt->second;
      if (rSub.iInterpolationCount < 2) continue;  // need 2 snapshots minimum

      float fTargetTime = fRenderTime - kfInterpolationDelay;

      ApplySnapshotInterpolation(
          gpGraphics->mRenderInterpolates.at(rCoord),
          rSub, fTargetTime);
  }


NEW FUNCTION (in GameBase.cpp, #if defined(BT_CLIENT)):

  void GameBase::ApplySnapshotInterpolation(
      game::FrameInterpolate& rInterpolate,
      const CoordFrames& rSub,
      float fTargetTime)
  {
      // Find two snapshots bracketing fTargetTime
      // Ring buffer: index 0 = oldest, iInterpolationCount-1 = newest
      const CoordFrames::InterpolationSnapshot* pBefore = nullptr;
      const CoordFrames::InterpolationSnapshot* pAfter = nullptr;

      for (int64_t i = 0; i < rSub.iInterpolationCount - 1; ++i)
      {
          int64_t iIdx = (rSub.iInterpolationHead + i) % CoordFrames::kiInterpolationBufferSize;
          int64_t iNextIdx = (rSub.iInterpolationHead + i + 1) % CoordFrames::kiInterpolationBufferSize;

          if (rSub.interpolationBuffer[iIdx].fTime <= fTargetTime &&
              rSub.interpolationBuffer[iNextIdx].fTime >= fTargetTime)
          {
              pBefore = &rSub.interpolationBuffer[iIdx];
              pAfter = &rSub.interpolationBuffer[iNextIdx];
              break;
          }
      }

      if (pBefore == nullptr)
      {
          // Target time is outside buffer range.
          // Use newest two snapshots and extrapolate.
          int64_t iNewest = (rSub.iInterpolationHead + rSub.iInterpolationCount - 1)
                            % CoordFrames::kiInterpolationBufferSize;
          int64_t iPrev = (rSub.iInterpolationHead + rSub.iInterpolationCount - 2)
                          % CoordFrames::kiInterpolationBufferSize;
          pBefore = &rSub.interpolationBuffer[iPrev];
          pAfter = &rSub.interpolationBuffer[iNewest];
      }

      float fDuration = pAfter->fTime - pBefore->fTime;
      float fAlpha = (fDuration > 0.0f)
          ? (fTargetTime - pBefore->fTime) / fDuration
          : 0.0f;
      // Clamp to [0, 2] to allow limited extrapolation beyond the buffer
      fAlpha = std::clamp(fAlpha, 0.0f, 2.0f);

      // Interpolate players
      auto& rPlayers = *rInterpolate.pPlayers;
      int64_t iPlayerCount = std::min(rPlayers.iCount,
          std::min(pBefore->iPlayerCount, pAfter->iPlayerCount));
      for (int64_t i = 0; i < iPlayerCount; ++i)
      {
          rPlayers.pVecPositions[i] = XMVectorLerp(
              pBefore->pVecPlayerPositions[i],
              pAfter->pVecPlayerPositions[i], fAlpha);
          rPlayers.pVecDirections[i] = XMVector3Normalize(XMVectorLerp(
              pBefore->pVecPlayerDirections[i],
              pAfter->pVecPlayerDirections[i], fAlpha));
      }

      // Interpolate spaceships
      auto& rSpaceships = *rInterpolate.pSpaceships;
      int64_t iSpaceshipCount = std::min(rSpaceships.iCount,
          std::min(pBefore->iSpaceshipCount, pAfter->iSpaceshipCount));
      for (int64_t i = 0; i < iSpaceshipCount; ++i)
      {
          rSpaceships.pVecPositions[i] = XMVectorLerp(
              pBefore->pVecSpaceshipPositions[i],
              pAfter->pVecSpaceshipPositions[i], fAlpha);
          rSpaceships.pVecDirections[i] = XMVector3Normalize(XMVectorLerp(
              pBefore->pVecSpaceshipDirections[i],
              pAfter->pVecSpaceshipDirections[i], fAlpha));
      }
  }


FILE: Engine/Source/GameBase.h

Add declaration (line ~111, inside #if defined(BT_CLIENT)):

  static void ApplySnapshotInterpolation(game::FrameInterpolate& rInterpolate,
      const CoordFrames& rSub, float fTargetTime);


================================================================================
4. BUFFER UNDERRUN / EDGE CASES
================================================================================

Handled naturally by the interpolation function:

- FEWER THAN 2 SNAPSHOTS: Skip interpolation entirely (the coord renders from
  its normal extrapolated/predicted state as it does today). This covers initial
  connection, new subscriptions, and grid transitions.

- TARGET TIME BEFORE BUFFER: Use oldest two snapshots, alpha will be negative,
  clamped to 0 -- effectively freezes at oldest known position. This should not
  happen in practice since the buffer advances faster than render time.

- TARGET TIME AFTER BUFFER (packet loss): Use newest two snapshots and
  extrapolate. Alpha > 1.0 (clamped at 2.0) provides brief linear extrapolation
  covering ~1 additional tick before capping. Beyond that, position freezes at
  the extrapolation cap. When new data arrives, the buffer catches up and smooth
  interpolation resumes.

- ENTITY COUNT MISMATCH (spawn/despawn between snapshots): Interpolate only the
  min(before, after) count. Newly spawned entities (index >= min count) render
  at their current extrapolated position from the normal pipeline. Despawned
  entities are not rendered since the current FrameInterpolate count is
  authoritative.


================================================================================
5. SINGLE-PLAYER / OFFLINE MODE
================================================================================

No changes needed. When the game session has no accepted connection
(game::gpClientSession->mpRuntime->mpClient == nullptr, or its public mStateFlags
lacks engine::Client::ClientStateFlags::kConnectionAccepted),
iInterpolationCount stays at 0, so the "< 2" check skips interpolation
and everything renders as today.


================================================================================
FILES MODIFIED
================================================================================

1. Engine/Source/GameBase.h (CoordFrames struct at line 39)
   - Add InterpolationSnapshot struct inside CoordFrames (#if BT_CLIENT)
   - Add kiInterpolationBufferSize, interpolationBuffer[], iInterpolationHead,
     iInterpolationCount fields
   - Reset new fields in ResetClientState()
   - Add kiMaxPlayersPerFrame, kiMaxSpaceshipsPerFrame constants
   - Add ApplySnapshotInterpolation declaration to GameBase class

2. Engine/Source/GameBase.cpp (Render function at line 266)
   - After the interpolateFrame loop, add snapshot interpolation override
     for non-client coords
   - Add ApplySnapshotInterpolation implementation

3. Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp
   - In ApplyCoordWriteback (~lines 44-53) or ApplyResult, call RecordInterpolationSnapshot
     for each non-client coord after confirmed state updates
   - Add RecordInterpolationSnapshot helper function

4. Engine/Source/GameBase.h (CoordFrames::ResetClientState)
   - Clear interpolation buffer fields (iInterpolationHead = 0,
     iInterpolationCount = 0)
