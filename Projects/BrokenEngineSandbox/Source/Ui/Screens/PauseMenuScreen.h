#pragma once

namespace game
{

class PauseMenuScreen
{
public:

	void Render();

private:

	// Resume / Graphics / Sound / Main Menu / Quit
	static constexpr int64_t kiMenuButtonCount = 5;

	float mfButtonHoverAnims[kiMenuButtonCount] {};
};

} // namespace game
