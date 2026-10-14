/*
	This file is part of Speech Plugin for Notepad++

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
*/

#pragma once

#include <windows.h>

enum Language
{
	LANGUAGE_AUTO,		// Same language as Notepad++
	LANGUAGE_ENGLISH,
	LANGUAGE_FRENCH,
};

enum TextId
{
	// Menu items, also written in contextMenu.xml (without '&')
	TXT_SPEAK_SELECTION,
	TXT_SPEAK_FROM_HERE,
	TXT_SPEAK_DOCUMENT,
	TXT_PLAY_PAUSE,
	TXT_PREVIOUS_SENTENCE,
	TXT_NEXT_SENTENCE,
	TXT_STOP_SPEECH,
	TXT_SPEAK_FASTER,
	TXT_SPEAK_SLOWER,
	TXT_SETTINGS,

	TXT_NO_SELECTION_TITLE,
	TXT_NO_SELECTION,
	TXT_TOO_LARGE,

	TXT_DLG_CAPTION,
	TXT_DLG_VOICE,
	TXT_DLG_DEFAULT_VOICE,
	TXT_DLG_SPEED,
	TXT_DLG_SLOWER,
	TXT_DLG_FASTER,
	TXT_DLG_CONTEXT_MENU,
	TXT_DLG_FOLLOW,
	TXT_DLG_TOOLBAR,
	TXT_DLG_LANGUAGE,
	TXT_DLG_LANGUAGE_AUTO,
	TXT_DLG_TEST,
	TXT_DLG_NORMAL,
	TXT_DLG_CANCEL,
	TXT_TEST_SENTENCE,
	TXT_RESTART,
	TXT_CONTEXT_MENU_ERROR,

	TXT_COUNT
};

const TCHAR *const TEXTS_ENGLISH[] =
{
	__TEXT("Speak &Selection"),
	__TEXT("Speak from &Here"),
	__TEXT("Speak &Document"),
	__TEXT("Play / &Pause"),
	__TEXT("Pre&vious Sentence"),
	__TEXT("&Next Sentence"),
	__TEXT("S&top Speech"),
	__TEXT("Speak &Faster"),
	__TEXT("Speak S&lower"),
	__TEXT("Speech Settin&gs..."),

	__TEXT("No selection"),
	__TEXT("Please select some text first"),
	__TEXT("The text is too large to be spoken"),

	__TEXT("Speech Settings"),
	__TEXT("&Voice:"),
	__TEXT("(Windows default voice)"),
	__TEXT("Reading speed:"),
	__TEXT("Slower"),
	__TEXT("Faster"),
	__TEXT("Show in the &right-click menu"),
	__TEXT("&Follow the reading (highlight the sentence, scroll)"),
	__TEXT("&Buttons in the toolbar"),
	__TEXT("Menu &language:"),
	__TEXT("Automatic (Notepad++ language)"),
	__TEXT("&Test"),
	__TEXT("&Normal"),
	__TEXT("Cancel"),
	__TEXT("This is the selected voice."),
	__TEXT("Restart Notepad++ to apply the changes."),
	__TEXT("Cannot update contextMenu.xml in the Notepad++ settings folder."),
};

const TCHAR *const TEXTS_FRENCH[] =
{
	__TEXT("Lire la &sélection"),
	__TEXT("Lire à partir d'&ici"),
	__TEXT("Lire le &document"),
	__TEXT("Lecture / &Pause"),
	__TEXT("Phrase pré&cédente"),
	__TEXT("Phrase s&uivante"),
	__TEXT("&Arrêter la lecture"),
	__TEXT("Lire plus &vite"),
	__TEXT("Lire plus &lentement"),
	__TEXT("Para&mètres de lecture..."),

	__TEXT("Aucune sélection"),
	__TEXT("Veuillez d'abord sélectionner du texte"),
	__TEXT("Le texte est trop long pour être lu"),

	__TEXT("Paramètres de lecture vocale"),
	__TEXT("&Voix :"),
	__TEXT("(Voix par défaut de Windows)"),
	__TEXT("Vitesse de lecture :"),
	__TEXT("Plus lent"),
	__TEXT("Plus rapide"),
	__TEXT("Ajouter au menu &clic droit"),
	__TEXT("&Suivre la lecture (surligner la phrase, faire défiler)"),
	__TEXT("&Boutons dans la barre d'outils"),
	__TEXT("&Langue des menus :"),
	__TEXT("Automatique (langue de Notepad++)"),
	__TEXT("&Tester"),
	__TEXT("&Normal"),
	__TEXT("Annuler"),
	__TEXT("Voici la voix sélectionnée."),
	__TEXT("Redémarrez Notepad++ pour appliquer les modifications."),
	__TEXT("Impossible de modifier contextMenu.xml dans le dossier de paramètres de Notepad++."),
};

static_assert(_countof(TEXTS_ENGLISH) == TXT_COUNT, "missing English text");
static_assert(_countof(TEXTS_FRENCH) == TXT_COUNT, "missing French text");

inline const TCHAR *GetText(Language language, TextId id)
{
	return (language == LANGUAGE_FRENCH) ? TEXTS_FRENCH[id] : TEXTS_ENGLISH[id];
}
