#pragma once

namespace game
{

enum Language
{
	kEnglish,
	kChinese,
	kSpanish,
	kPortuguese,
	kFrench,
	kGerman,

	kLanguageCount,
};

inline Language geLanguage = kEnglish;

enum Strings
{
	kStringComplete,
	kStringDefaults,
	kStringGameOver,
	kStringGamepad,
	kStringGameSettings,
	kStringMainMenu,
	kStringMouse,
	kStringMoveWith,
	kStringGraphics,
	kStringOr,
	kStringLocalServer,
	kStringRemoteServer,
	kStringStart,
	kStringReady,
	kStringRespawn,
	kStringResume,
	kStringAudio,
	kStringQuit,
	kStringPaused,

	kBaseStringsCount,
	kStringsCount = kBaseStringsCount,
};

// Outer extent is deduced ([]) on purpose: a dropped/extra string row then changes std::size and trips the
// static_assert below. Pinning it to [kStringsCount + 1] would make that guard a tautology that catches nothing.
inline char32_t gppTranslatedStrings[][kLanguageCount][256]
{
	// kStringComplete
	{
		U"COMPLETE", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
	// kStringDefaults
	{
		U"RESET TO DEFAULT", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
	// kStringGameOver
	{
		U"GAME OVER", // English
		U"游戏结束", // Chinese
		U"JUEGO TERMINADO", // Spanish
		U"FIM DE JOGO", // Portuguese
		U"JEU TERMINÉ", // French
		U"SPIEL IST AUS", // German
	},
	// kStringGamepad
	{
		U"GAMEPAD", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
	// kStringGameSettings (machine translations — needs localization pass)
	{
		U"GAME SETTINGS", // English
		U"游戏设置", // Chinese
		U"AJUSTES DEL JUEGO", // Spanish
		U"AJUSTES DO JOGO", // Portuguese
		U"PARAMÈTRES DU JEU", // French
		U"SPIELEINSTELLUNGEN", // German
	},
	// kStringMainMenu
	{
		U"MAIN MENU", // English
		U"主菜单", // Chinese
		U"MENÚ PRINCIPAL", // Spanish
		U"MENU PRINCIPAL", // Portuguese
		U"MENU PRINCIPAL", // French
		U"HAUPTMENÜ", // German
	},
	// kStringMouse
	{
		U"MOUSE", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
	// kStringMoveWith
	{
		U"MOVE WITH", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
	// kStringGraphics
	{
		U"GRAPHICS", // English
		U"图形", // Chinese
		U"GRÁFICOS", // Spanish
		U"GRÁFICOS", // Portuguese
		U"GRAPHIQUE", // French
		U"GRAFIK", // German
	},
	// kStringOr
	{
		U"OR", // English
		U"或者", // Chinese
		U"O", // Spanish
		U"Ou", // Portuguese
		U"Ou", // French
		U"Oder", // German
	},
	// kStringLocalServer
	{
		U"LOCAL SERVER", // English
		U"本地服务器", // Chinese
		U"SERVIDOR LOCAL", // Spanish
		U"SERVIDOR LOCAL", // Portuguese
		U"SERVEUR LOCAL", // French
		U"LOKALER SERVER", // German
	},
	// kStringRemoteServer
	{
		U"REMOTE SERVER", // English
		U"远程服务器", // Chinese
		U"SERVIDOR REMOTO", // Spanish
		U"SERVIDOR REMOTO", // Portuguese
		U"SERVEUR DISTANT", // French
		U"REMOTE-SERVER", // German
	},
	// kStringStart
	{
		U"Start", // English
		U"开始", // Chinese
		U"Comenzar", // Spanish
		U"Começar", // Portuguese
		U"Commencer", // French
		U"Start", // German
	},
	// kStringReady
	{
		U"I'm Ready", // English
		U"我准备好了", // Chinese
		U"Estoy lista", // Spanish
		U"Estou pronto", // Portuguese
		U"Je suis prêt", // French
		U"Ich bin bereit", // German
	},
	// kStringRespawn
	{
		U"Respawn", // English
		U"重生", // Chinese
		U"Reaparecer", // Spanish
		U"Renascer", // Portuguese
		U"Réapparaître", // French
		U"Wiederbeleben", // German
	},
	// kStringResume
	{
		U"Resume", // English
		U"重新开始", // Chinese
		U"Reanudar", // Spanish
		U"Retomar", // Portuguese
		U"Recommencer", // French
		U"Wieder aufnehmen", // German
	},
	// kStringAudio
	{
		U"AUDIO", // English
		U"音频", // Chinese
		U"AUDIO", // Spanish
		U"ÁUDIO", // Portuguese
		U"AUDIO", // French
		U"AUDIO", // German
	},
	// kStringQuit
	{
		U"QUIT", // English
		U"辞职", // Chinese
		U"RENUNCIAR", // Spanish
		U"SAIR", // Portuguese
		U"QUITTER", // French
		U"VERLASSEN", // German
	},
	// kStringPaused (machine translations — needs localization pass)
	{
		U"PAUSED", // English
		U"已暂停", // Chinese
		U"PAUSADO", // Spanish
		U"PAUSADO", // Portuguese
		U"EN PAUSE", // French
		U"PAUSIERT", // German
	},

	// kStringsCount
	{
		U"", // English
		U"", // Chinese
		U"", // Spanish
		U"", // Portuguese
		U"", // French
		U"", // German
	},
};
// One outer row per Strings enumerator plus the trailing all-empty sentinel row; static_assert catches a dropped row,
// which would otherwise silently shift every later string's translations. (Inner [kLanguageCount][256] extents are
// fixed by the array type, so per-language drift within a row stays a positional authoring contract.)
static_assert(std::size(gppTranslatedStrings) == static_cast<size_t>(kStringsCount) + 1);

inline std::u32string_view TranslatedString(Strings eString)
{
	if (gppTranslatedStrings[eString][geLanguage][0] == 0) [[unlikely]]
	{
		return gppTranslatedStrings[eString][kEnglish];
	}

	return gppTranslatedStrings[eString][geLanguage];
}

inline void InitializeLocalization()
{
	std::setlocale(LC_ALL, "en_US.utf8");
	for (int64_t i = 0; i < kStringsCount; ++i)
	{
		for (int64_t j = 0; j < kLanguageCount; ++j)
		{
			int64_t k = 0;
			while (gppTranslatedStrings[i][j][k] != 0)
			{
				gppTranslatedStrings[i][j][k] = towupper(static_cast<wint_t>(gppTranslatedStrings[i][j][k]));
				++k;
			}
		}
	}
}

} // namespace game
