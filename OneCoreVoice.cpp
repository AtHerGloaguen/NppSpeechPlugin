/*
	This file is part of Speech Plugin for Notepad++

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
*/

#include "OneCoreVoice.h"

#include <windows.h>
// Conflicts with a C++/WinRT method name
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cmath>
#include <mutex>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::Playback;
using namespace winrt::Windows::Media::SpeechSynthesis;

namespace
{
	// Recursive: Completed() calls its handler immediately when the operation is already finished
	std::recursive_mutex gLock;

	SpeechSynthesizer gSynth{ nullptr };
	MediaPlayer gPlayer{ nullptr };
	IAsyncOperation<SpeechSynthesisStream> gPending{ nullptr };

	// Lets a synthesis finishing after a Stop or a newer Speak be ignored
	unsigned gGeneration = 0;
	bool gIsPaused = false;

	// Same feel as SAPI: rate 10 is about 3 times faster, -10 about 3 times slower
	double SpeakingRate(long rate)
	{
		double speakingRate = std::pow(3.0, rate / 10.0);

		// Range accepted by SpeechSynthesizerOptions::SpeakingRate
		if (speakingRate < 0.5)
			return 0.5;
		if (speakingRate > 6.0)
			return 6.0;
		return speakingRate;
	}

	VoiceInformation FindVoice(const std::wstring &voiceId)
	{
		for (VoiceInformation const &voice : SpeechSynthesizer::AllVoices())
		{
			if (_wcsicmp(voice.Id().c_str(), voiceId.c_str()) == 0)
				return voice;
		}

		return nullptr;
	}

	void StopLocked()
	{
		++gGeneration;
		gIsPaused = false;

		if (gPending)
		{
			gPending.Cancel();
			gPending = nullptr;
		}

		if (gPlayer)
		{
			gPlayer.Pause();
			gPlayer.Source(nullptr);
		}
	}
}

namespace OneCoreVoice
{
	// WinRT errors are exceptions: none of them must reach Notepad++

	std::vector<Info> EnumVoices()
	{
		std::vector<Info> voices;

		try
		{
			for (VoiceInformation const &voice : SpeechSynthesizer::AllVoices())
			{
				Info info;
				info.id = voice.Id().c_str();
				info.name = std::wstring(voice.DisplayName().c_str()) + L" - " + voice.Language().c_str();
				voices.push_back(info);
			}
		}
		catch (...)
		{
		}

		return voices;
	}

	void Speak(const std::wstring &txt, const std::wstring &voiceId, long rate)
	{
		try
		{
			std::lock_guard<std::recursive_mutex> lock(gLock);

			StopLocked();

			if (!gSynth)
				gSynth = SpeechSynthesizer();
			if (!gPlayer)
				gPlayer = MediaPlayer();

			// An uninstalled voice leaves the default one
			VoiceInformation voice = FindVoice(voiceId);
			if (voice)
				gSynth.Voice(voice);

			gSynth.Options().SpeakingRate(SpeakingRate(rate));

			unsigned generation = gGeneration;

			gPending = gSynth.SynthesizeTextToStreamAsync(txt);
			gPending.Completed([generation](IAsyncOperation<SpeechSynthesisStream> const &operation, AsyncStatus status)
			{
				try
				{
					std::lock_guard<std::recursive_mutex> lock(gLock);

					if ((status != AsyncStatus::Completed) || (generation != gGeneration) || !gPlayer)
						return;

					gPending = nullptr;

					SpeechSynthesisStream stream = operation.GetResults();
					gPlayer.Source(MediaSource::CreateFromStream(stream, stream.ContentType()));

					if (!gIsPaused)
						gPlayer.Play();
				}
				catch (...)
				{
				}
			});
		}
		catch (...)
		{
		}
	}

	void Stop()
	{
		try
		{
			std::lock_guard<std::recursive_mutex> lock(gLock);
			StopLocked();
		}
		catch (...)
		{
		}
	}

	void Pause()
	{
		try
		{
			std::lock_guard<std::recursive_mutex> lock(gLock);

			// Also covers a synthesis still running: it will not start playing
			if (gPlayer && !gIsPaused)
			{
				gPlayer.Pause();
				gIsPaused = true;
			}
		}
		catch (...)
		{
		}
	}

	void Resume()
	{
		try
		{
			std::lock_guard<std::recursive_mutex> lock(gLock);

			if (gPlayer && gIsPaused)
			{
				gIsPaused = false;

				if (gPlayer.Source())
					gPlayer.Play();
			}
		}
		catch (...)
		{
		}
	}

	void Release()
	{
		try
		{
			std::lock_guard<std::recursive_mutex> lock(gLock);

			StopLocked();

			if (gPlayer)
			{
				gPlayer.Close();
				gPlayer = nullptr;
			}

			if (gSynth)
			{
				gSynth.Close();
				gSynth = nullptr;
			}
		}
		catch (...)
		{
		}
	}
}
