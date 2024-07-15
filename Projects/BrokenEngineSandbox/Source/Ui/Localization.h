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
	kStringContinue,
	kStringDefaults,
	kStringGameOver,
	kStringGamepad,
	kStringMainMenu,
	kStringMouse,
	kStringMoveWith,
	kStringGraphics,
	kStringOr,
	kStringPlay,
	kStringStart,
	kStringReady,
	kStringRestart,
	kStringResume,
	kStringWave,
	kStringSound,
	kStringQuit,

	kBaseStringsCount,
	kStringsCount = kBaseStringsCount,
};

inline char32_t gppTranslatedStrings[kStringsCount + 1][kLanguageCount][256]
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
	// kStringContinue
	{
		U"Continue", // English
		U"继续", // Chinese
		U"Continuar", // Spanish
		U"Continuar", // Portuguese
		U"Continuer", // French
		U"Weitermachen", // German
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
	// kStringPlay
	{
		U"PLAY", // English
		U"玩", // Chinese
		U"DESEMPEÑAR", // Spanish
		U"TOQUE", // Portuguese
		U"JOUER", // French
		U"ABSPIELEN", // German
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
	// kStringRestart
	{
		U"Restart", // English
		U"重新开始", // Chinese
		U"Reiniciar", // Spanish
		U"Reiniciar", // Portuguese
		U"Redémarrer", // French
		U"Neu starten", // German
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
	// kStringWave
	{
		U"WAVE: ", // English
		U"海浪: ", // Chinese
		U"OLA: ", // Spanish
		U"ACENO: ", // Portuguese
		U"VAGUE: ", // French
		U"WELLE: ", // German
	},
	// kStringSound
	{
		U"SOUND", // English
		U"声音", // Chinese
		U"SONIDO", // Spanish
		U"SOM", // Portuguese
		U"DU SON", // French
		U"KLANG", // German
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

inline std::u32string_view TranslatedString(Strings eString)
{
	if (gppTranslatedStrings[eString][geLanguage][0] == 0) [[unlikely]]
	{
		return gppTranslatedStrings[eString][kEnglish];
	}

	return gppTranslatedStrings[eString][geLanguage];
}

} // namespace game
