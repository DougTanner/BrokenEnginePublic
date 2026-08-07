#pragma once

namespace game
{

class MainMenuScreen
{
public:

	void Render();

private:

	// Local Server / Remote Server / Graphics / Audio / Game Settings / Quit
	static constexpr int64_t kiMenuButtonCount = 6;

	float mfButtonHoverAnims[kiMenuButtonCount] {};
};

} // namespace game
