#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderParticlesSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kParticles);

	if (ImGui::BeginTabBar("ParticlesTabs"))
	{
		if (ImGui::BeginTabItem("Missile", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 0;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           kiSection, 1.0f, "Missile Particle Width");
			WrapperSlider("Length",          kiSection, 1.0f, "Missile Particle Length");
			WrapperSlider("Length Spread",   kiSection, 1.0f, "Missile Particle Length Spread");
			WrapperSlider("Position Jitter", kiSection, 1.0f, "Missile Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            kiSection, 1.0f, "Missile Particle Velocity Base");
			WrapperSlider("Velocity Spread",          kiSection, 1.0f, "Missile Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   kiSection, 1.0f, "Missile Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", kiSection, 1.0f, "Missile Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           kiSection, 1.0f, "Missile Particle Velocity Decay");
			WrapperSlider("Gravity",                  kiSection, 1.0f, "Missile Particle Gravity");

			WrapperSeparatorText("Lighting");
			WrapperSlider("Lighting Area",      kiSection, 1.0f, "Explosion Particle Lighting Area");
			WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Explosion Particle Lighting Intensity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", kiSection, 1.0f, "Explosion Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  kiSection, 1.0f, "Missile Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   kiSection, 1.0f, "Missile Particle Intensity Decay");
			WrapperSlider("Intensity Power",   kiSection, 1.0f, "Missile Particle Intensity Power");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Player", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 1;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           kiSection, 1.0f, "Player Particle Width");
			WrapperSlider("Length",          kiSection, 1.0f, "Player Particle Length");
			WrapperSlider("Length Spread",   kiSection, 1.0f, "Player Particle Length Spread");
			WrapperSlider("Position Jitter", kiSection, 1.0f, "Player Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            kiSection, 1.0f, "Player Particle Velocity Base");
			WrapperSlider("Velocity Spread",          kiSection, 1.0f, "Player Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   kiSection, 1.0f, "Player Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", kiSection, 1.0f, "Player Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           kiSection, 1.0f, "Player Particle Velocity Decay");
			WrapperSlider("Gravity",                  kiSection, 1.0f, "Player Particle Gravity");

			WrapperSeparatorText("Lighting");
			WrapperSlider("Lighting Area",      kiSection, 1.0f, "Explosion Particle Lighting Area");
			WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Explosion Particle Lighting Intensity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", kiSection, 1.0f, "Explosion Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  kiSection, 1.0f, "Player Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   kiSection, 1.0f, "Player Particle Intensity Decay");
			WrapperSlider("Intensity Power",   kiSection, 1.0f, "Player Particle Intensity Power");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Spaceship", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 2;
			}

			WrapperSeparatorText("Size");
			WrapperSlider("Width",           kiSection, 1.0f, "Spaceship Particle Width");
			WrapperSlider("Length",          kiSection, 1.0f, "Spaceship Particle Length");
			WrapperSlider("Length Spread",   kiSection, 1.0f, "Spaceship Particle Length Spread");
			WrapperSlider("Position Jitter", kiSection, 1.0f, "Spaceship Particle Position Jitter");

			WrapperSeparatorText("Speed");
			WrapperSlider("Velocity Base",            kiSection, 1.0f, "Spaceship Particle Velocity Base");
			WrapperSlider("Velocity Spread",          kiSection, 1.0f, "Spaceship Particle Velocity Spread");
			WrapperSlider("Vertical Velocity Base",   kiSection, 1.0f, "Spaceship Particle Vertical Velocity Base");
			WrapperSlider("Vertical Velocity Spread", kiSection, 1.0f, "Spaceship Particle Vertical Velocity Spread");
			WrapperSlider("Velocity Decay",           kiSection, 1.0f, "Spaceship Particle Velocity Decay");
			WrapperSlider("Gravity",                  kiSection, 1.0f, "Spaceship Particle Gravity");

			WrapperSeparatorText("Lighting");
			WrapperSlider("Lighting Area",      kiSection, 1.0f, "Explosion Particle Lighting Area");
			WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Explosion Particle Lighting Intensity");

			WrapperSeparatorText("Visible");
			WrapperSlider("Visible Intensity", kiSection, 1.0f, "Explosion Particle Visible Intensity");
			WrapperSlider("Intensity Spread",  kiSection, 1.0f, "Spaceship Particle Intensity Spread");
			WrapperSlider("Intensity Decay",   kiSection, 1.0f, "Spaceship Particle Intensity Decay");
			WrapperSlider("Intensity Power",   kiSection, 1.0f, "Spaceship Particle Intensity Power");

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

} // namespace game

#endif // BT_CLIENT
