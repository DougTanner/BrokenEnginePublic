Feature: Adaptive music that responds to game state
=====================================================

Context
-------
Currently, music is selected sequentially from fixed playlists (menu vs game) via
GetNextMusicTrack(). Track transitions only happen when a track ends (or nearly ends),
using the existing crossfade infrastructure. The game has no awareness of combat
intensity, player health, or enemy presence when choosing music.

The game has 4 menu tracks (ambient/relaxed) and 4 game tracks with suggestive names:
S31-OnPatrol (exploration), S31-TheGearsofProgress (building/ambient), S31-HighAlert
(combat), S31-UnexpectedTrouble (intense combat). These naturally map to intensity tiers.

AudioManager already supports mid-track transitions via PlayMusic() with crossfade.
The game layer can query frame state: spaceship counts, player health/armor/shields,
blaster/missile counts, and player proximity to enemies -- all available from the
RenderFrame at the client's grid coord (`mClientGridCoord`).

Why
---
- Sequential playlists feel disconnected from gameplay -- calm music during combat
  breaks immersion
- The existing track names already suggest intensity tiers (OnPatrol vs HighAlert)
- The crossfade infrastructure already handles smooth transitions
- Game state (enemy count, player health, projectile count) is readily accessible
  from the render frame without touching deterministic frame logic

Changes (3 files)
-----------------

1. Projects/BrokenEngineSandbox/Source/AdaptiveMusic.h  [NEW]
   New header defining the adaptive music system (client-only, #ifdef BT_CLIENT):

     enum class MusicIntensity : uint8_t
     {
         kCalm,       // No enemies nearby, full health
         kTension,    // Enemies present but not engaged
         kCombat,     // Active combat (taking/dealing damage, projectiles flying)
         kHighCombat, // Intense combat (low health, many enemies, heavy fire)
     };

     struct AdaptiveMusic
     {
         void Update(float fDeltaTime, const game::Frame& rFrame);
         common::crc_t GetTrack();

         MusicIntensity meCurrentIntensity = MusicIntensity::kCalm;
         float mfIntensityTimer = 0.0f;         // Time spent at evaluated intensity
         float mfTimeSinceLastTransition = 0.0f; // Cooldown to prevent rapid switching
     };

   Update() evaluates game state each frame to compute a desired intensity:
   - Count alive spaceships (iCount minus those with kExploding flag) from
     SpaceshipsPostRender in the flagship player's render frame
   - Check flagship player armor/shields from PlayersPostRender
   - Count active blasters and missiles from the interpolate collections
   - Compare player position to nearest spaceship positions for proximity

   Intensity thresholds (tunable constants in the .cpp):
   - kCalm: 0 alive enemies within range
   - kTension: 1+ alive enemies exist but no projectiles and health is high
   - kCombat: projectiles in flight OR player recently took damage (armor decreased)
   - kHighCombat: player armor below 40% AND 3+ enemies AND projectiles active

   Hysteresis: intensity must persist for a minimum duration before triggering a
   transition (e.g., 3 seconds for escalation, 6 seconds for de-escalation). A
   cooldown (e.g., 10 seconds) prevents rapid back-and-forth transitions.

   GetTrack() returns the CRC for the track matching meCurrentIntensity:
   - kCalm       -> kAudioMusicS31OnPatrolwavCrc
   - kTension    -> kAudioMusicS31TheGearsofProgresswavCrc
   - kCombat     -> kAudioMusicS31HighAlertwavCrc
   - kHighCombat -> kAudioMusicS31UnexpectedTroublewavCrc

2. Projects/BrokenEngineSandbox/Source/AdaptiveMusic.cpp  [NEW]
   Implementation of AdaptiveMusic::Update() and GetTrack():

   Update():
   - Early-out if the flagship player's frame is not available
   - Get SpaceshipsPostRender from the frame, count alive enemies (skip kExploding)
   - Get PlayersPostRender, find flagship player index, read armor and shields
   - Get BlastersInterpolate and MissilesInterpolate counts for projectile activity
   - Use Game's mfPreviousClientArmor member field to detect recent damage (armor
     decreased) -- already maintained in Game.cpp (updated ~line 343, reset ~line 463,
     restored from save meta ~line 907)
   - Compute desired MusicIntensity from the above signals
   - Apply hysteresis: track how long the desired intensity has been sustained
   - If sustained long enough and cooldown has elapsed, update meCurrentIntensity
     and reset mfTimeSinceLastTransition

   GetTrack():
   - Switch on meCurrentIntensity, return the corresponding track CRC
   - Uses the existing 4 game music track CRCs (no new audio assets needed)

3. Projects/BrokenEngineSandbox/Source/Game.h + Game.cpp  [MODIFIED]
   Replace the sequential game playlist with adaptive music:

   Game.h:
   - Add #include "AdaptiveMusic.h"
   - Replace mGameMusicPlaylist[4] and miGameMusicIndex with:
       AdaptiveMusic mAdaptiveMusic {};
   - Keep mMenuMusicPlaylist and miMenuMusicIndex unchanged (menu stays sequential)

   Game.cpp:
   - In the SetNextMusicTrackCallback lambda (Game constructor), change the
     else branch of GetNextMusicTrack() to return mAdaptiveMusic.GetTrack()
     instead of sequential indexing
   - Add mAdaptiveMusic.Update() call. This must run from a location that
     executes every frame on the client with access to the render frame.
     The best location is inside the existing SetNextMusicTrackCallback --
     but that only fires on track end. Instead, add the Update call in
     Game::ChangeFrame() (which runs every client frame) after the frame
     is complete, guarded by #ifdef BT_CLIENT and !InMainMenu():

       #if defined(BT_CLIENT)
       if (!InMainMenu() && mCoordFrames.contains(mClientGridCoord))
       {
           const Frame& rFrame = RenderFrame(mClientGridCoord);
           float fDeltaTime = /* real-time delta from AudioManager's timer or similar */;
           mAdaptiveMusic.Update(fDeltaTime, rFrame);
       }
       #endif

   - When adaptive music decides to transition (intensity changed), call
     engine::gpAudioManager->PlayMusic(mAdaptiveMusic.GetTrack()) directly
     to trigger a mid-track crossfade. This bypasses the normal end-of-track
     callback path. The Update() method returns bool indicating a transition
     occurred, and the caller invokes PlayMusic().
   - GetNextMusicTrack() still serves as the end-of-track callback: when a
     track finishes naturally, it returns the track for the current intensity
     (which may have changed during playback), providing seamless looping
     within an intensity tier.

Notes
-----
- Menu music is unchanged -- adaptive music only applies during gameplay
- The current live mGameMusicPlaylist order is: UnexpectedTrouble, HighAlert,
  OnPatrol, TheGearsofProgress (Game.h:179) -- the tier->CRC map in GetTrack()
  should be written against the CRC constants, not playlist indices
- No new audio assets required; the 4 existing game tracks map directly to
  intensity tiers
- No frame state is modified -- all queries are read-only against the render
  frame, preserving determinism
- The system lives entirely in the game layer (Projects/), not in Engine/,
  following the existing pattern where engine provides mechanism (crossfade)
  and game provides logic (track selection)
- Hysteresis constants should be tuned by feel; initial values are starting
  points. Consider exposing them in the tweaks screen for iteration
- Future: additional intensity tiers, per-biome track sets, or layered stems
  (multiple simultaneous streams mixed by intensity) could extend this system
