#include "ParticleWrappers.h"

namespace game
{

// Explosions - Particle visible intensity (shared across all explosion types)
engine::Wrapper gExplosionParticleVisibleIntensity(2.0f, 0.0f, 4.0f);

// Missile explosion particles
engine::Wrapper gMissileExplosionParticleWidth(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleLength(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleLengthSpread(0.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticlePositionJitter(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleVelocityBase(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleVelocitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleVerticalVelocityBase(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleVerticalVelocitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleVelocityDecay(1.0f, 0.0f, 5.0f);
engine::Wrapper gMissileExplosionParticleGravity(1.0f, 0.0f, 5.0f);
engine::Wrapper gMissileExplosionParticleIntensitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gMissileExplosionParticleIntensityDecay(1.0f, 0.0f, 5.0f);
engine::Wrapper gMissileExplosionParticleIntensityPower(1.0f, 0.0f, 5.0f);

// Player explosion particles
engine::Wrapper gPlayerExplosionParticleWidth(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleLength(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleLengthSpread(0.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticlePositionJitter(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleVelocityBase(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleVelocitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleVerticalVelocityBase(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleVerticalVelocitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleVelocityDecay(1.0f, 0.0f, 5.0f);
engine::Wrapper gPlayerExplosionParticleGravity(1.0f, 0.0f, 5.0f);
engine::Wrapper gPlayerExplosionParticleIntensitySpread(1.0f, 0.0f, 3.0f);
engine::Wrapper gPlayerExplosionParticleIntensityDecay(1.0f, 0.0f, 5.0f);
engine::Wrapper gPlayerExplosionParticleIntensityPower(1.0f, 0.0f, 5.0f);

// Spaceship explosion particles
engine::Wrapper gSpaceshipExplosionParticleWidth(0.5f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleLength(1.5f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleLengthSpread(0.0f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticlePositionJitter(3.0f, 0.0f, 10.0f);
engine::Wrapper gSpaceshipExplosionParticleVelocityBase(0.25f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleVelocitySpread(1.25f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleVerticalVelocityBase(0.25f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleVerticalVelocitySpread(0.75f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleVelocityDecay(3.0f, 0.0f, 5.0f);
engine::Wrapper gSpaceshipExplosionParticleGravity(1.0f, 0.0f, 5.0f);
engine::Wrapper gSpaceshipExplosionParticleIntensitySpread(3.0f, 0.0f, 3.0f);
engine::Wrapper gSpaceshipExplosionParticleIntensityDecay(1.5f, 0.0f, 5.0f);
engine::Wrapper gSpaceshipExplosionParticleIntensityPower(0.5f, 0.0f, 5.0f);

} // namespace game
