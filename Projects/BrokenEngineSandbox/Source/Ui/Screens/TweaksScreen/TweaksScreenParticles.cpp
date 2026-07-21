#include "TweaksScreen.h"

#include "Ui/ParticleWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gParticlesRegistrar
{
	// Missile
	{"Missile Particle Width", &gMissileExplosionParticleWidth},
	{"Missile Particle Length", &gMissileExplosionParticleLength},
	{"Missile Particle Length Spread", &gMissileExplosionParticleLengthSpread},
	{"Missile Particle Position Jitter", &gMissileExplosionParticlePositionJitter},
	{"Missile Particle Velocity Base", &gMissileExplosionParticleVelocityBase},
	{"Missile Particle Velocity Spread", &gMissileExplosionParticleVelocitySpread},
	{"Missile Particle Vertical Velocity Base", &gMissileExplosionParticleVerticalVelocityBase},
	{"Missile Particle Vertical Velocity Spread", &gMissileExplosionParticleVerticalVelocitySpread},
	{"Missile Particle Velocity Decay", &gMissileExplosionParticleVelocityDecay},
	{"Missile Particle Gravity", &gMissileExplosionParticleGravity},
	{"Missile Particle Visible Intensity", &gMissileExplosionParticleVisibleIntensity},
	{"Missile Particle Intensity Spread", &gMissileExplosionParticleIntensitySpread},
	{"Missile Particle Intensity Decay", &gMissileExplosionParticleIntensityDecay},
	{"Missile Particle Intensity Power", &gMissileExplosionParticleIntensityPower},
	// Player
	{"Player Particle Width", &gPlayerExplosionParticleWidth},
	{"Player Particle Length", &gPlayerExplosionParticleLength},
	{"Player Particle Length Spread", &gPlayerExplosionParticleLengthSpread},
	{"Player Particle Position Jitter", &gPlayerExplosionParticlePositionJitter},
	{"Player Particle Velocity Base", &gPlayerExplosionParticleVelocityBase},
	{"Player Particle Velocity Spread", &gPlayerExplosionParticleVelocitySpread},
	{"Player Particle Vertical Velocity Base", &gPlayerExplosionParticleVerticalVelocityBase},
	{"Player Particle Vertical Velocity Spread", &gPlayerExplosionParticleVerticalVelocitySpread},
	{"Player Particle Velocity Decay", &gPlayerExplosionParticleVelocityDecay},
	{"Player Particle Gravity", &gPlayerExplosionParticleGravity},
	{"Player Particle Visible Intensity", &gPlayerExplosionParticleVisibleIntensity},
	{"Player Particle Intensity Spread", &gPlayerExplosionParticleIntensitySpread},
	{"Player Particle Intensity Decay", &gPlayerExplosionParticleIntensityDecay},
	{"Player Particle Intensity Power", &gPlayerExplosionParticleIntensityPower},
	// Spaceship
	{"Spaceship Particle Width", &gSpaceshipExplosionParticleWidth},
	{"Spaceship Particle Length", &gSpaceshipExplosionParticleLength},
	{"Spaceship Particle Length Spread", &gSpaceshipExplosionParticleLengthSpread},
	{"Spaceship Particle Position Jitter", &gSpaceshipExplosionParticlePositionJitter},
	{"Spaceship Particle Velocity Base", &gSpaceshipExplosionParticleVelocityBase},
	{"Spaceship Particle Velocity Spread", &gSpaceshipExplosionParticleVelocitySpread},
	{"Spaceship Particle Vertical Velocity Base", &gSpaceshipExplosionParticleVerticalVelocityBase},
	{"Spaceship Particle Vertical Velocity Spread", &gSpaceshipExplosionParticleVerticalVelocitySpread},
	{"Spaceship Particle Velocity Decay", &gSpaceshipExplosionParticleVelocityDecay},
	{"Spaceship Particle Gravity", &gSpaceshipExplosionParticleGravity},
	{"Spaceship Particle Visible Intensity", &gSpaceshipExplosionParticleVisibleIntensity},
	{"Spaceship Particle Intensity Spread", &gSpaceshipExplosionParticleIntensitySpread},
	{"Spaceship Particle Intensity Decay", &gSpaceshipExplosionParticleIntensityDecay},
	{"Spaceship Particle Intensity Power", &gSpaceshipExplosionParticleIntensityPower},
};
}

void TweaksScreen::RenderParticlesSection()
{
	const int64_t iSection = giTweakSectionParticles;

	if (ImGui::BeginTabBar("ParticlesTabs"))
	{
		if (ImGui::BeginTabItem("Missile", nullptr, ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 0)
			{
				mApplySubtab.Clear(engine::SectionFlag(iSection));
			}
			if (!(mApplySubtab & engine::SectionFlag(iSection)))
			{
				mActiveSubtab[iSection] = 0;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           iSection, 1.0f, "Missile Particle Width");
			WrapperSlider("Length",          iSection, 1.0f, "Missile Particle Length");
			WrapperSlider("Length Spread",   iSection, 1.0f, "Missile Particle Length Spread");
			WrapperSlider("Position Jitter", iSection, 1.0f, "Missile Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            iSection, 1.0f, "Missile Particle Velocity Base");
			WrapperSlider("Velocity Spread",          iSection, 1.0f, "Missile Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   iSection, 1.0f, "Missile Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", iSection, 1.0f, "Missile Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           iSection, 1.0f, "Missile Particle Velocity Decay");
			WrapperSlider("Gravity",                  iSection, 1.0f, "Missile Particle Gravity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", iSection, 1.0f, "Missile Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  iSection, 1.0f, "Missile Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   iSection, 1.0f, "Missile Particle Intensity Decay");
			WrapperSlider("Intensity Power",   iSection, 1.0f, "Missile Particle Intensity Power");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Player", nullptr, ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 1)
			{
				mApplySubtab.Clear(engine::SectionFlag(iSection));
			}
			if (!(mApplySubtab & engine::SectionFlag(iSection)))
			{
				mActiveSubtab[iSection] = 1;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           iSection, 1.0f, "Player Particle Width");
			WrapperSlider("Length",          iSection, 1.0f, "Player Particle Length");
			WrapperSlider("Length Spread",   iSection, 1.0f, "Player Particle Length Spread");
			WrapperSlider("Position Jitter", iSection, 1.0f, "Player Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            iSection, 1.0f, "Player Particle Velocity Base");
			WrapperSlider("Velocity Spread",          iSection, 1.0f, "Player Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   iSection, 1.0f, "Player Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", iSection, 1.0f, "Player Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           iSection, 1.0f, "Player Particle Velocity Decay");
			WrapperSlider("Gravity",                  iSection, 1.0f, "Player Particle Gravity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", iSection, 1.0f, "Player Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  iSection, 1.0f, "Player Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   iSection, 1.0f, "Player Particle Intensity Decay");
			WrapperSlider("Intensity Power",   iSection, 1.0f, "Player Particle Intensity Power");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Spaceship", nullptr, ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & engine::SectionFlag(iSection)) && mActiveSubtab[iSection] == 2)
			{
				mApplySubtab.Clear(engine::SectionFlag(iSection));
			}
			if (!(mApplySubtab & engine::SectionFlag(iSection)))
			{
				mActiveSubtab[iSection] = 2;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           iSection, 1.0f, "Spaceship Particle Width");
			WrapperSlider("Length",          iSection, 1.0f, "Spaceship Particle Length");
			WrapperSlider("Length Spread",   iSection, 1.0f, "Spaceship Particle Length Spread");
			WrapperSlider("Position Jitter", iSection, 1.0f, "Spaceship Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            iSection, 1.0f, "Spaceship Particle Velocity Base");
			WrapperSlider("Velocity Spread",          iSection, 1.0f, "Spaceship Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   iSection, 1.0f, "Spaceship Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", iSection, 1.0f, "Spaceship Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           iSection, 1.0f, "Spaceship Particle Velocity Decay");
			WrapperSlider("Gravity",                  iSection, 1.0f, "Spaceship Particle Gravity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", iSection, 1.0f, "Spaceship Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  iSection, 1.0f, "Spaceship Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   iSection, 1.0f, "Spaceship Particle Intensity Decay");
			WrapperSlider("Intensity Power",   iSection, 1.0f, "Spaceship Particle Intensity Power");

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

} // namespace game

#endif // BT_CLIENT
