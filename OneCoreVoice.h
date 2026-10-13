/*
	This file is part of Speech Plugin for Notepad++

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

// Voices of Windows.Media.SpeechSynthesis ("OneCore" voices: Julie, Paul...), which SAPI cannot use.
// The text is synthesized locally, then played with a MediaPlayer.
namespace OneCoreVoice
{
	struct Info
	{
		std::wstring id;
		std::wstring name;
	};

	std::vector<Info> EnumVoices();

	// rate uses the SAPI range, -10 to 10
	void Speak(const std::wstring &txt, const std::wstring &voiceId, long rate);
	void Stop();
	void Pause();
	void Resume();

	// Must be called before COM is uninitialized
	void Release();
}
