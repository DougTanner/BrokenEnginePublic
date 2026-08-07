#pragma once

namespace game
{

class PauseMenuScreen
{
public:

	void Render();

private:

	// Resume / Graphics / Audio / Game Settings / Main Menu / Quit
	static constexpr int64_t kiMenuButtonCount = 6;

	float mfButtonHoverAnims[kiMenuButtonCount] {};
};

} // namespace game
