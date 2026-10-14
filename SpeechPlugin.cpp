/*
	This file is part of Speech Plugin for Notepad++
	Copyright (C)2008 Jim Xochellis <dxoch at users.sourceforge.net>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/


#include "PluginInterface.h"
#include "resource.h"
#include "OneCoreVoice.h"
#include "Texts.h"
#include <shlobj.h>
#include <commctrl.h>

#include <objbase.h>
#include <sapi.h>

#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <climits>
#include <cassert>

const TCHAR PLUGIN_NAME[] = __TEXT("Speech");
const TCHAR INI_FILE_NAME[] = __TEXT("SpeechPlugin.ini");
const TCHAR INI_SECTION[] = __TEXT("Speech");
const TCHAR INI_KEY_RATE[] = __TEXT("Rate");
const TCHAR INI_KEY_VOICE[] = __TEXT("Voice");
const TCHAR INI_KEY_LANGUAGE[] = __TEXT("Language");
const TCHAR INI_KEY_FOLLOW[] = __TEXT("Follow");
const TCHAR INI_KEY_TOOLBAR[] = __TEXT("Toolbar");
const TCHAR *const LANGUAGE_CODES[] = { __TEXT("auto"), __TEXT("en"), __TEXT("fr") };

// Plugin menu, in this order
enum MenuItem
{
	MENU_SPEAK_SELECTION,
	MENU_SPEAK_FROM_HERE,
	MENU_SPEAK_DOCUMENT,
	MENU_SEPARATOR_1,
	MENU_PLAY_PAUSE,
	MENU_PREVIOUS,
	MENU_NEXT,
	MENU_STOP,
	MENU_SEPARATOR_2,
	MENU_FASTER,
	MENU_SLOWER,
	MENU_SETTINGS,
	nbFunc
};

// Speaking rate range accepted by ISpVoice::SetRate
const long RATE_MIN = -10;
const long RATE_MAX = 10;

// Messages of the plugin window, which receives the end of each spoken sentence
const UINT WM_SAPI_EVENT = WM_APP + 1;		// SAPI event queue to read
const UINT WM_SENTENCE_END = WM_APP + 2;	// wParam: utterance number

HINSTANCE gHInst = NULL;
NppData nppData;
bool gCanSpeek = false;
long gRate = 0;
// SAPI token id, or ONECORE_PREFIX + Windows.Media voice id, empty for the system default voice
const TCHAR ONECORE_PREFIX[] = __TEXT("OneCore:");
std::wstring gVoiceId;
Language gLanguageSetting = LANGUAGE_AUTO;
Language gLanguage = LANGUAGE_ENGLISH;	// Resolved once: Notepad++ builds the plugin menu only at startup
bool gFollow = true;					// highlight the sentence being read and scroll to it
bool gToolbar = true;					// buttons in the Notepad++ toolbar (read at startup)
std::wstring gConfigDir;
std::wstring gIniPath;
FuncItem funcItem[nbFunc];
HWND gNotifyWnd = NULL;
int gIndicator = -1;					// Scintilla indicator of the sentence being read, -1 if none

void Cleanup();
void LoadSettings();
Language ResolveLanguage(Language setting);
void CreateNotifyWindow();
void OnReady();
void OnBufferActivated();
void OnDocumentModified(const SCNotification *notification);
void OnFileBeforeClose(UINT_PTR bufferId);
void AddToolbarButtons();
void RefreshContextMenu();

void SpeakSelection();
void SpeakFromHere();
void SpeakDocument();
void PlayPause();
void PreviousSentence();
void NextSentence();
void StopSpeech();
void SpeakFaster();
void SpeakSlower();
void ShowSettings();

//---------------------------------------------------------

BOOL APIENTRY DllMain( HANDLE hModule,
                       DWORD  reasonForCall,
                       LPVOID /*lpReserved*/ )
{
	switch (reasonForCall)
	{
		case DLL_PROCESS_ATTACH:
		{
			gHInst = (HINSTANCE)hModule;

			funcItem[MENU_SPEAK_SELECTION]._pFunc = SpeakSelection;
			funcItem[MENU_SPEAK_FROM_HERE]._pFunc = SpeakFromHere;
			funcItem[MENU_SPEAK_DOCUMENT]._pFunc = SpeakDocument;
			funcItem[MENU_SEPARATOR_1]._pFunc = NULL;
			funcItem[MENU_PLAY_PAUSE]._pFunc = PlayPause;
			funcItem[MENU_PREVIOUS]._pFunc = PreviousSentence;
			funcItem[MENU_NEXT]._pFunc = NextSentence;
			funcItem[MENU_STOP]._pFunc = StopSpeech;
			funcItem[MENU_SEPARATOR_2]._pFunc = NULL;
			funcItem[MENU_FASTER]._pFunc = SpeakFaster;
			funcItem[MENU_SLOWER]._pFunc = SpeakSlower;
			funcItem[MENU_SETTINGS]._pFunc = ShowSettings;

			// Item names are set in setInfo, once the language is known

			for (int i = 0; i < nbFunc; ++i)
			{
				funcItem[i]._init2Check = false;
				funcItem[i]._pShKey = NULL;
			}
		}
		break;

		case DLL_PROCESS_DETACH:
			break;

		case DLL_THREAD_ATTACH:
			break;

		case DLL_THREAD_DETACH:
			break;
	}

	return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData notpadPlusData)
{
	nppData = notpadPlusData;
	gCanSpeek = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	LoadSettings();
	CreateNotifyWindow();

	gLanguage = ResolveLanguage(gLanguageSetting);

	lstrcpy(funcItem[MENU_SPEAK_SELECTION]._itemName, GetText(gLanguage, TXT_SPEAK_SELECTION));
	lstrcpy(funcItem[MENU_SPEAK_FROM_HERE]._itemName, GetText(gLanguage, TXT_SPEAK_FROM_HERE));
	lstrcpy(funcItem[MENU_SPEAK_DOCUMENT]._itemName, GetText(gLanguage, TXT_SPEAK_DOCUMENT));
	lstrcpy(funcItem[MENU_SEPARATOR_1]._itemName, __TEXT("-SEPARATOR-"));
	lstrcpy(funcItem[MENU_PLAY_PAUSE]._itemName, GetText(gLanguage, TXT_PLAY_PAUSE));
	lstrcpy(funcItem[MENU_PREVIOUS]._itemName, GetText(gLanguage, TXT_PREVIOUS_SENTENCE));
	lstrcpy(funcItem[MENU_NEXT]._itemName, GetText(gLanguage, TXT_NEXT_SENTENCE));
	lstrcpy(funcItem[MENU_STOP]._itemName, GetText(gLanguage, TXT_STOP_SPEECH));
	lstrcpy(funcItem[MENU_SEPARATOR_2]._itemName, __TEXT("-SEPARATOR-"));
	lstrcpy(funcItem[MENU_FASTER]._itemName, GetText(gLanguage, TXT_SPEAK_FASTER));
	lstrcpy(funcItem[MENU_SLOWER]._itemName, GetText(gLanguage, TXT_SPEAK_SLOWER));
	lstrcpy(funcItem[MENU_SETTINGS]._itemName, GetText(gLanguage, TXT_SETTINGS));
}

extern "C" __declspec(dllexport) const TCHAR * getName()
{
	return PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *nbF)
{
	*nbF = nbFunc;
	return funcItem;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification * notifyCode)
{
	switch (notifyCode->nmhdr.code)
	{
	case NPPN_TBMODIFICATION:
		if (gToolbar)
			AddToolbarButtons();
		break;

	case NPPN_READY:
		OnReady();
		break;

	case NPPN_BUFFERACTIVATED:
		OnBufferActivated();
		break;

	case NPPN_FILEBEFORECLOSE:
		OnFileBeforeClose(notifyCode->nmhdr.idFrom);
		break;

	case SCN_MODIFIED:
		OnDocumentModified(notifyCode);
		break;

	case NPPN_SHUTDOWN:
		Cleanup();
		break;

	default:
		return;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT /*Message*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return TRUE;
}

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode()
{
	return TRUE;
}
#endif

//======================================================================

struct VoiceStruct
{
	VoiceStruct()
	{
		mVoice = NULL;
		mIsPaused= false;
	}

	// The SAPI voice is created on first use and reused for every reading
	bool Create()
	{
		if (mVoice == NULL)
		{
			HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void **)&mVoice);

			if (FAILED(hr))
			{
				mVoice = NULL;
				return false;
			}

			// The end of each text is posted to the plugin window
			ULONGLONG interest = SPFEI(SPEI_END_INPUT_STREAM);
			mVoice->SetInterest(interest, interest);
			mVoice->SetNotifyWindowMessage(gNotifyWnd, WM_SAPI_EVENT, 0, 0);

			this->SetVoiceId(gVoiceId);
			mIsPaused= false;
		}

		return true;
	}

	// Returns the SAPI stream number of the text, 0 on failure
	ULONG Speak(const std::wstring &txt)
	{
		ULONG streamNumber = 0;

		if (Create())
		{
			// SPF_IS_NOT_XML: text starting with '<' (XML, HTML...) must be read, not parsed as SAPI markup
			if (FAILED(mVoice->Speak(txt.c_str(), SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_NOT_XML, &streamNumber)))
				streamNumber = 0;

			// A paused voice would otherwise stay silent
			this->Resume();
		}

		return streamNumber;
	}

	// Reads the SAPI events: true if the text of streamNumber has been spoken
	bool HasEnded(ULONG streamNumber)
	{
		bool ended = false;

		if (mVoice == NULL)
			return false;

		SPEVENT event;
		ULONG fetched = 0;

		while ((mVoice->GetEvents(1, &event, &fetched) == S_OK) && (fetched == 1))
		{
			if ((event.eEventId == SPEI_END_INPUT_STREAM) && (streamNumber != 0) && (event.ulStreamNum == streamNumber))
				ended = true;

			// Same as SpClearEvent of sphelper.h
			if ((event.elParamType == SPET_LPARAM_IS_POINTER) || (event.elParamType == SPET_LPARAM_IS_STRING))
				CoTaskMemFree((void *)event.lParam);
			else if ((event.elParamType == SPET_LPARAM_IS_TOKEN) || (event.elParamType == SPET_LPARAM_IS_OBJECT))
				((IUnknown *)event.lParam)->Release();
		}

		return ended;
	}

	void Stop()
	{
		if (mVoice != NULL)
		{
			// Purge the queue instead of releasing the voice while it is still speaking
			mVoice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
			this->Resume();
		}
	}

	void Release()
	{
		if (mVoice != NULL)
		{
			this->Stop();
			mVoice->SetNotifySink(NULL);
			mVoice->Release();
			mVoice = NULL;
		}
	}

	void Pause()
	{
		if ( (mVoice != NULL) && ! mIsPaused )
		{
			mVoice->Pause();
			mIsPaused= true;
		}
	}

	void Resume()
	{
		if ( (mVoice != NULL) && mIsPaused )
		{
			mVoice->Resume();
			mIsPaused= false;
		}
	}

	void SetRate(long rate)
	{
		if (mVoice != NULL)
			mVoice->SetRate(rate);
	}

	// Falls back to the default voice if the token cannot be found (voice uninstalled...)
	void SetVoiceId(const std::wstring &voiceId)
	{
		if (mVoice == NULL)
			return;

		ISpObjectToken *pToken = NULL;

		if ( ! voiceId.empty() &&
			SUCCEEDED(CoCreateInstance(CLSID_SpObjectToken, NULL, CLSCTX_ALL, IID_ISpObjectToken, (void **)&pToken)) &&
			FAILED(pToken->SetId(NULL, voiceId.c_str(), FALSE)) )
		{
			pToken->Release();
			pToken = NULL;
		}

		mVoice->SetVoice(pToken);

		if (pToken != NULL)
			pToken->Release();

		mVoice->SetRate(gRate);
	}

protected:
	ISpVoice *mVoice;
	bool mIsPaused;
};

VoiceStruct gVoice;

bool IsOneCoreVoice(const std::wstring &voiceId)
{
	return voiceId.compare(0, _countof(ONECORE_PREFIX) - 1, ONECORE_PREFIX) == 0;
}

HWND GetCurrentEditHandle();

//======================================================================
// Reading, sentence by sentence: a sentence can be repeated or skipped,
// and the sentence being read is highlighted in the document.

const size_t MAX_SENTENCE = 400;	// bytes; longer sentences are cut at a space

struct Sentence
{
	size_t textStart, textEnd;			// in Reading::text
	Sci_Position docStart, docEnd;		// in the document, updated when it is modified
};

enum ReadingStatus
{
	STATUS_IDLE,
	STATUS_SPEAKING,
	STATUS_PAUSED,
};

struct Reading
{
	// Copy of the text read: the document can change or be hidden while it is spoken
	std::string text;
	UINT codePage = CP_UTF8;
	sptr_t doc = 0;					// Scintilla document, 0 for a text which is not in a document
	UINT_PTR bufferId = 0;
	std::vector<Sentence> sentences;
	size_t index = 0;
	ReadingStatus status = STATUS_IDLE;
};

Reading gReading;
unsigned gUtterance = 0;		// incremented on each spoken sentence and stop, to ignore late notifications
ULONG gSapiStream = 0;			// SAPI stream number of the sentence being read

//---------------------------------------------------------
// Splitting in sentences

bool IsSpace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '\f') || (c == '\v');
}

bool IsByte(const std::string &text, size_t i, unsigned char value)
{
	return (i < text.size()) && ((unsigned char)text[i] == value);
}

// Length of the sentence end punctuation at text[i] (. ! ? or ...), 0 if none
size_t EndPunctuationLength(const std::string &text, size_t i, bool utf8)
{
	char c = text[i];

	if ((c == '.') || (c == '!') || (c == '?'))
		return 1;
	if (utf8 && IsByte(text, i, 0xE2) && IsByte(text, i + 1, 0x80) && IsByte(text, i + 2, 0xA6))
		return 3;
	if (!utf8 && IsByte(text, i, 0x85))		// Windows-1252
		return 1;
	return 0;
}

// Length of the closing quote or bracket at text[i], 0 if none
size_t ClosingLength(const std::string &text, size_t i, bool utf8)
{
	char c = text[i];

	if ((c == '"') || (c == '\'') || (c == ')') || (c == ']'))
		return 1;
	if (utf8)
	{
		if (IsByte(text, i, 0xC2) && IsByte(text, i + 1, 0xBB))		// »
			return 2;
		if (IsByte(text, i, 0xE2) && IsByte(text, i + 1, 0x80) && (IsByte(text, i + 2, 0x9D) || IsByte(text, i + 2, 0x99)))	// ” ’
			return 3;
	}
	else if (IsByte(text, i, 0xBB) || IsByte(text, i, 0x94) || IsByte(text, i, 0x92))
		return 1;
	return 0;
}

// Word before a period which does not end the sentence: initial (J. Dupont) or abbreviation (M. Dupont)
bool IsAbbreviation(const std::string &text, size_t dot)
{
	static const char *const ABBREVIATIONS[] =
	{
		"M", "MM", "Mme", "Mmes", "Mlle", "Dr", "Pr", "Me", "St", "Ste", "Mr", "Mrs", "Ms", "Jr", "Sr",
		"cf", "ex", "p", "art", "av", "bd", "vol", "vs", "No",
	};

	size_t start = dot;
	while ((start > 0) && isalpha((unsigned char)text[start - 1]))
		--start;
	if ((start == dot) || ((start > 0) && !IsSpace(text[start - 1]) && (text[start - 1] != '(')))
		return false;

	std::string word = text.substr(start, dot - start);
	if ((word.size() == 1) && isupper((unsigned char)word[0]))
		return true;
	for (const char *abbreviation : ABBREVIATIONS)
	{
		if (word == abbreviation)
			return true;
	}
	return false;
}

void AddSentence(const std::string &text, size_t start, size_t end, Sci_Position docBase, std::vector<Sentence> &sentences)
{
	for (;;)
	{
		while ((start < end) && IsSpace(text[start]))
			++start;
		while ((end > start) && IsSpace(text[end - 1]))
			--end;
		if (start >= end)
			return;

		size_t cut = end;
		if (end - start > MAX_SENTENCE)
		{
			// Too long: cut at a space, otherwise between two characters
			cut = text.rfind(' ', start + MAX_SENTENCE);
			if ((cut == std::string::npos) || (cut < start + MAX_SENTENCE / 2))
			{
				cut = start + MAX_SENTENCE;
				while ((cut > start) && (((unsigned char)text[cut] & 0xC0) == 0x80))
					--cut;
			}
		}

		sentences.push_back({ start, cut, docBase + (Sci_Position)start, docBase + (Sci_Position)cut });
		start = cut;
	}
}

// Splits text[from, to) at sentence ends and line breaks
void SplitSentences(const std::string &text, size_t from, size_t to, bool utf8, Sci_Position docBase, std::vector<Sentence> &sentences)
{
	size_t sentenceStart = from;
	size_t i = from;

	while (i < to)
	{
		if (text[i] == '\n')
		{
			AddSentence(text, sentenceStart, i + 1, docBase, sentences);
			sentenceStart = ++i;
			continue;
		}

		size_t length = EndPunctuationLength(text, i, utf8);
		if (length == 0)
		{
			++i;
			continue;
		}

		bool singlePeriod = (text[i] == '.') && ((i + 1 >= to) || (EndPunctuationLength(text, i + 1, utf8) == 0));
		size_t end = i;
		while ((end < to) && ((length = EndPunctuationLength(text, end, utf8)) != 0))
			end += length;

		// Closing quotes, possibly after a space (French: « citée. »)
		for (;;)
		{
			size_t next = end;
			if ((next < to) && (text[next] == ' '))
				++next;
			else if (utf8 && IsByte(text, next, 0xC2) && IsByte(text, next + 1, 0xA0))	// no-break space
				next += 2;
			length = (next < to) ? ClosingLength(text, next, utf8) : 0;
			if (length == 0)
				break;
			end = next + length;
		}

		if (((end >= to) || IsSpace(text[end])) && !(singlePeriod && IsAbbreviation(text, i)))
		{
			AddSentence(text, sentenceStart, end, docBase, sentences);
			sentenceStart = end;
		}
		i = end;
	}

	AddSentence(text, sentenceStart, to, docBase, sentences);
}

//---------------------------------------------------------
// Highlight of the sentence being read

// View showing the document being read, NULL if it is hidden
HWND FindReadingView()
{
	if (gReading.doc == 0)
		return NULL;

	HWND views[] = { nppData._scintillaMainHandle, nppData._scintillaSecondHandle };
	for (HWND view : views)
	{
		if (IsWindowVisible(view) && ((sptr_t)SendMessage(view, SCI_GETDOCPOINTER, 0, 0) == gReading.doc))
			return view;
	}
	return NULL;
}

void ClearIndicator(HWND view)
{
	if (gIndicator < 0)
		return;
	SendMessage(view, SCI_SETINDICATORCURRENT, gIndicator, 0);
	SendMessage(view, SCI_INDICATORCLEARRANGE, 0, SendMessage(view, SCI_GETLENGTH, 0, 0));
}

// Scrolls only when the position is out of the upper two thirds of the view, and brings it back at one third
void ScrollToPosition(HWND view, Sci_Position position)
{
	Sci_Position line = (Sci_Position)SendMessage(view, SCI_LINEFROMPOSITION, position, 0);
	Sci_Position displayLine = (Sci_Position)SendMessage(view, SCI_VISIBLEFROMDOCLINE, line, 0);
	Sci_Position firstLine = (Sci_Position)SendMessage(view, SCI_GETFIRSTVISIBLELINE, 0, 0);
	Sci_Position linesOnScreen = (Sci_Position)SendMessage(view, SCI_LINESONSCREEN, 0, 0);

	if ((displayLine < firstLine) || (displayLine >= firstLine + linesOnScreen * 2 / 3))
		SendMessage(view, SCI_SETFIRSTVISIBLELINE, (std::max)((Sci_Position)0, displayLine - linesOnScreen / 3), 0);

	// Horizontal scrolling, for long lines without word wrap
	SendMessage(view, SCI_SCROLLRANGE, position, position);
}

void HighlightSentence()
{
	if (!gFollow || (gReading.index >= gReading.sentences.size()))
		return;

	HWND view = FindReadingView();
	if (view == NULL)
		return;

	const Sentence &sentence = gReading.sentences[gReading.index];
	ClearIndicator(view);
	if (gIndicator >= 0)
		SendMessage(view, SCI_INDICATORFILLRANGE, sentence.docStart, sentence.docEnd - sentence.docStart);
	ScrollToPosition(view, sentence.docStart);
}

void ClearHighlight()
{
	HWND view = FindReadingView();
	if (view != NULL)
		ClearIndicator(view);
}

//---------------------------------------------------------
// Reading

void UpdateToolbar()
{
	// The toolbar button looks pressed while reading
	SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[MENU_PLAY_PAUSE]._cmdID, gReading.status == STATUS_SPEAKING);
}

std::wstring ToWide(const std::string &text, size_t start, size_t end, UINT codePage)
{
	// Invalid sequences are replaced by U+FFFD instead of failing
	int length = MultiByteToWideChar(codePage, 0, text.data() + start, (int)(end - start), NULL, 0);
	std::wstring wide((std::max)(length, 0), 0);
	if (length > 0)
		MultiByteToWideChar(codePage, 0, text.data() + start, (int)(end - start), &wide[0], length);
	return wide;
}

// Stops the speech in progress; the reading itself stays
void InterruptSpeech()
{
	++gUtterance;
	gSapiStream = 0;
	gVoice.Stop();
	OneCoreVoice::Stop();
}

void SpeakCurrentSentence()
{
	++gUtterance;
	gSapiStream = 0;

	const Sentence &sentence = gReading.sentences[gReading.index];
	std::wstring text = ToWide(gReading.text, sentence.textStart, sentence.textEnd, gReading.codePage);

	HighlightSentence();

	if (IsOneCoreVoice(gVoiceId))
		OneCoreVoice::Speak(text, gVoiceId.substr(_countof(ONECORE_PREFIX) - 1), gRate, gNotifyWnd, WM_SENTENCE_END, gUtterance);
	else
	{
		gSapiStream = gVoice.Speak(text);
		if (gSapiStream == 0)
			PostMessage(gNotifyWnd, WM_SENTENCE_END, gUtterance, 0);	// go on with the next sentence
	}
}

void StopSpeech()
{
	InterruptSpeech();
	ClearHighlight();

	gReading.status = STATUS_IDLE;
	gReading.text.clear();
	gReading.sentences.clear();
	gReading.index = 0;
	gReading.doc = 0;
	gReading.bufferId = 0;
	UpdateToolbar();
}

void OnSentenceEnd()
{
	if (gReading.status == STATUS_IDLE)
		return;

	if (++gReading.index < gReading.sentences.size())
		SpeakCurrentSentence();
	else
		StopSpeech();
}

// Starts reading gReading.text from the first sentence which does not end before startOffset.
// splitAtStart: a sentence starts at startOffset. docBase: position of the text in the document.
void StartReading(size_t startOffset, bool splitAtStart, Sci_Position docBase)
{
	bool utf8 = (gReading.codePage == CP_UTF8);

	gReading.sentences.clear();
	if (splitAtStart && (startOffset > 0))
	{
		SplitSentences(gReading.text, 0, startOffset, utf8, docBase, gReading.sentences);
		SplitSentences(gReading.text, startOffset, gReading.text.size(), utf8, docBase, gReading.sentences);
	}
	else
		SplitSentences(gReading.text, 0, gReading.text.size(), utf8, docBase, gReading.sentences);

	// First sentence which does not end before startOffset
	gReading.index = 0;
	while ((gReading.index < gReading.sentences.size()) && (gReading.sentences[gReading.index].textEnd <= startOffset))
		++gReading.index;

	if (!gCanSpeek || (gReading.index >= gReading.sentences.size()))
	{
		StopSpeech();
		return;
	}

	gReading.status = STATUS_SPEAKING;
	UpdateToolbar();
	SpeakCurrentSentence();
}

// Reads the document range [from, to) of editHandle, from startPos
void ReadDocument(HWND editHandle, Sci_Position from, Sci_Position to, Sci_Position startPos, bool splitAtStart)
{
	StopSpeech();

	Sci_Position length = to - from;
	if (length <= 0)
		return;

	// MultiByteToWideChar only takes int lengths
	if (length > INT_MAX)
	{
		MessageBox(nppData._nppHandle, GetText(gLanguage, TXT_TOO_LARGE), PLUGIN_NAME, 0);
		return;
	}

	const char *text = (const char *)SendMessage(editHandle, SCI_GETRANGEPOINTER, from, length);
	if (text == NULL)
		return;
	gReading.text.assign(text, (size_t)length);

	// Code page is 0 for ANSI documents and SC_CP_UTF8 for Unicode ones
	UINT codePage = (UINT)SendMessage(editHandle, SCI_GETCODEPAGE, 0, 0);
	gReading.codePage = (codePage == 0) ? CP_ACP : codePage;
	gReading.doc = (sptr_t)SendMessage(editHandle, SCI_GETDOCPOINTER, 0, 0);
	gReading.bufferId = (UINT_PTR)SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);

	StartReading((size_t)(startPos - from), splitAtStart, from);
}

// Reads a text which is not in a document (voice test)
void ReadText(const std::wstring &text)
{
	StopSpeech();

	int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
	gReading.text.assign((std::max)(length, 0), 0);
	if (length > 0)
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &gReading.text[0], length, NULL, NULL);
	gReading.codePage = CP_UTF8;

	StartReading(0, false, 0);
}

void PauseSpeech()
{
	gVoice.Pause();
	OneCoreVoice::Pause();
	gReading.status = STATUS_PAUSED;
	UpdateToolbar();
}

void ResumeSpeech()
{
	gVoice.Resume();
	OneCoreVoice::Resume();
	gReading.status = STATUS_SPEAKING;
	UpdateToolbar();
}

// Previous (-1) or next (+1) sentence; "previous" at the first sentence restarts it
void MoveSentence(int delta)
{
	if (gReading.status == STATUS_IDLE)
		return;

	size_t index = gReading.index;
	if (delta < 0)
		index = (index > 0) ? index - 1 : 0;
	else if (index + 1 < gReading.sentences.size())
		++index;
	else
		return;		// already the last sentence

	InterruptSpeech();
	gReading.index = index;
	gReading.status = STATUS_SPEAKING;
	UpdateToolbar();
	SpeakCurrentSentence();
}

LRESULT CALLBACK NotifyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SAPI_EVENT:
		if (gVoice.HasEnded(gSapiStream))
			OnSentenceEnd();
		return 0;

	case WM_SENTENCE_END:
		if ((unsigned)wParam == gUtterance)
			OnSentenceEnd();
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

void CreateNotifyWindow()
{
	const TCHAR CLASS_NAME[] = __TEXT("NppSpeechPluginNotify");

	WNDCLASS wc = {};
	wc.lpfnWndProc = NotifyWndProc;
	wc.hInstance = gHInst;
	wc.lpszClassName = CLASS_NAME;
	RegisterClass(&wc);

	// Message-only window
	gNotifyWnd = CreateWindow(CLASS_NAME, __TEXT(""), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, gHInst, NULL);
}

//---------------------------------------------------------
// Commands

void SpeakDocument()
{
	HWND editHandle = GetCurrentEditHandle();

	assert(editHandle != NULL);

	if (editHandle != NULL)
	{
		Sci_Position txtLen = (Sci_Position)SendMessage(editHandle, SCI_GETTEXTLENGTH, 0, 0);
		ReadDocument(editHandle, 0, txtLen, 0, false);
	}
}

void SpeakSelection()
{
	HWND editHandle = GetCurrentEditHandle();

	assert(editHandle != NULL);

	if (editHandle != NULL)
	{
		Sci_Position selStart = (Sci_Position)SendMessage(editHandle, SCI_GETSELECTIONSTART, 0, 0);
		Sci_Position selEnd = (Sci_Position)SendMessage(editHandle, SCI_GETSELECTIONEND, 0, 0);

		if( selEnd > selStart )
		{
			ReadDocument(editHandle, selStart, selEnd, selStart, false);
		}
		else
		{
			MessageBox(nppData._nppHandle, GetText(gLanguage, TXT_NO_SELECTION), GetText(gLanguage, TXT_NO_SELECTION_TITLE), 0);
		}
	}
}

// Reads the document from the selection (from the start of its first word),
// or without selection from the sentence of the cursor
void SpeakFromHere()
{
	HWND editHandle = GetCurrentEditHandle();

	if (editHandle == NULL)
		return;

	Sci_Position txtLen = (Sci_Position)SendMessage(editHandle, SCI_GETTEXTLENGTH, 0, 0);
	Sci_Position selStart = (Sci_Position)SendMessage(editHandle, SCI_GETSELECTIONSTART, 0, 0);
	Sci_Position selEnd = (Sci_Position)SendMessage(editHandle, SCI_GETSELECTIONEND, 0, 0);

	if (selEnd > selStart)
	{
		Sci_Position start = (Sci_Position)SendMessage(editHandle, SCI_WORDSTARTPOSITION, selStart, TRUE);
		ReadDocument(editHandle, 0, txtLen, start, true);
	}
	else
		ReadDocument(editHandle, 0, txtLen, selStart, false);
}

// Play / pause: when nothing is read, reads from the selection or the cursor
void PlayPause()
{
	switch (gReading.status)
	{
	case STATUS_SPEAKING:
		PauseSpeech();
		break;
	case STATUS_PAUSED:
		ResumeSpeech();
		break;
	default:
		SpeakFromHere();
		break;
	}
}

void PreviousSentence()
{
	MoveSentence(-1);
}

void NextSentence()
{
	MoveSentence(+1);
}

//---------------------------------------------------------
// Notepad++ notifications

const COLORREF HIGHLIGHT_COLOR = RGB(128, 128, 128);

void OnReady()
{
	int indicator = 0;
	if (SendMessage(nppData._nppHandle, NPPM_ALLOCATEINDICATOR, 1, (LPARAM)&indicator))
	{
		gIndicator = indicator;

		// Light grey box under the text, readable with light and dark themes
		HWND views[] = { nppData._scintillaMainHandle, nppData._scintillaSecondHandle };
		for (HWND view : views)
		{
			SendMessage(view, SCI_INDICSETSTYLE, gIndicator, INDIC_FULLBOX);
			SendMessage(view, SCI_INDICSETFORE, gIndicator, HIGHLIGHT_COLOR);
			SendMessage(view, SCI_INDICSETALPHA, gIndicator, 70);
			SendMessage(view, SCI_INDICSETOUTLINEALPHA, gIndicator, 0);
			SendMessage(view, SCI_INDICSETUNDER, gIndicator, TRUE);
		}
	}

	RefreshContextMenu();
}

// The indicator is stored in the document: a document shown again can still have an old highlight
void OnBufferActivated()
{
	HWND view = GetCurrentEditHandle();

	if ((gReading.status != STATUS_IDLE) && ((sptr_t)SendMessage(view, SCI_GETDOCPOINTER, 0, 0) == gReading.doc))
		HighlightSentence();
	else
		ClearIndicator(view);
}

void OnFileBeforeClose(UINT_PTR bufferId)
{
	if ((gReading.status != STATUS_IDLE) && (gReading.bufferId != 0) && (bufferId == gReading.bufferId))
		StopSpeech();
}

Sci_Position MovePosition(Sci_Position position, const SCNotification *notification, bool isEnd)
{
	Sci_Position at = notification->position;
	Sci_Position length = notification->length;

	if (notification->modificationType & SC_MOD_INSERTTEXT)
		return (position > at || (!isEnd && position == at)) ? position + length : position;

	// Deleted text
	if (position <= at)
		return position;
	return (position >= at + length) ? position - length : at;
}

// Keeps the positions of the sentences right when the document being read is edited
void OnDocumentModified(const SCNotification *notification)
{
	if ((gReading.status == STATUS_IDLE) || (gReading.doc == 0) ||
		!(notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
		return;

	// The document is notified by each view showing it: only one is used
	HWND view = FindReadingView();
	if ((view == NULL) || ((HWND)notification->nmhdr.hwndFrom != view))
		return;

	for (Sentence &sentence : gReading.sentences)
	{
		sentence.docStart = MovePosition(sentence.docStart, notification, false);
		sentence.docEnd = MovePosition(sentence.docEnd, notification, true);
	}
}

void AddToolbarButton(MenuItem item, int bitmapId, int iconId, int darkIconId)
{
	// Icons drawn at the size of the large toolbar for the screen DPI; Notepad++ scales them down
	UINT dpi = GetDpiForWindow(nppData._nppHandle);
	int size = MulDiv(32, (dpi != 0) ? dpi : 96, 96);

	toolbarIconsWithDarkMode icons = {};
	icons.hToolbarBmp = (HBITMAP)LoadImage(gHInst, MAKEINTRESOURCE(bitmapId), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
	icons.hToolbarIcon = (HICON)LoadImage(gHInst, MAKEINTRESOURCE(iconId), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
	icons.hToolbarIconDarkMode = (HICON)LoadImage(gHInst, MAKEINTRESOURCE(darkIconId), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);

	SendMessage(nppData._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, funcItem[item]._cmdID, (LPARAM)&icons);
}

void AddToolbarButtons()
{
	AddToolbarButton(MENU_PREVIOUS, IDB_PREVIOUS, IDI_PREVIOUS, IDI_PREVIOUS_DARK);
	AddToolbarButton(MENU_PLAY_PAUSE, IDB_PLAY_PAUSE, IDI_PLAY_PAUSE, IDI_PLAY_PAUSE_DARK);
	AddToolbarButton(MENU_NEXT, IDB_NEXT, IDI_NEXT, IDI_NEXT_DARK);
	AddToolbarButton(MENU_STOP, IDB_STOP, IDI_STOP, IDI_STOP_DARK);
}

//---------------------------------------------------------

long ClampRate(long rate)
{
	if (rate < RATE_MIN)
		return RATE_MIN;
	if (rate > RATE_MAX)
		return RATE_MAX;
	return rate;
}

// Applies the rate to the current voice too, so that a running reading changes speed
void SetSpeechRate(long rate)
{
	gRate = ClampRate(rate);
	gVoice.SetRate(gRate);
}

void SetSpeechVoice(const std::wstring &voiceId)
{
	// Switching between SAPI and OneCore must not leave the other engine speaking
	if (IsOneCoreVoice(voiceId) != IsOneCoreVoice(gVoiceId))
		StopSpeech();

	gVoiceId = voiceId;

	if (!IsOneCoreVoice(gVoiceId))
		gVoice.SetVoiceId(gVoiceId);
}

void LoadSettings()
{
	TCHAR configDir[MAX_PATH] = {};

	SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)configDir);

	if (configDir[0] != 0)
	{
		gConfigDir = configDir;
		gIniPath = gConfigDir + __TEXT("\\") + INI_FILE_NAME;

		// GetPrivateProfileInt cannot return negative values
		TCHAR value[16] = {};
		GetPrivateProfileString(INI_SECTION, INI_KEY_RATE, __TEXT("0"), value, _countof(value), gIniPath.c_str());
		gRate = ClampRate(_wtol(value));

		TCHAR voiceId[MAX_PATH] = {};
		GetPrivateProfileString(INI_SECTION, INI_KEY_VOICE, __TEXT(""), voiceId, _countof(voiceId), gIniPath.c_str());
		gVoiceId = voiceId;

		TCHAR language[16] = {};
		GetPrivateProfileString(INI_SECTION, INI_KEY_LANGUAGE, LANGUAGE_CODES[LANGUAGE_AUTO], language, _countof(language), gIniPath.c_str());
		for (int i = 0; i < _countof(LANGUAGE_CODES); ++i)
		{
			if (_wcsicmp(language, LANGUAGE_CODES[i]) == 0)
				gLanguageSetting = (Language)i;
		}

		gFollow = GetPrivateProfileInt(INI_SECTION, INI_KEY_FOLLOW, 1, gIniPath.c_str()) != 0;
		gToolbar = GetPrivateProfileInt(INI_SECTION, INI_KEY_TOOLBAR, 1, gIniPath.c_str()) != 0;
	}
}

void SaveSettings()
{
	if (!gIniPath.empty())
	{
		WritePrivateProfileString(INI_SECTION, INI_KEY_RATE, std::to_wstring(gRate).c_str(), gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_VOICE, gVoiceId.c_str(), gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_LANGUAGE, LANGUAGE_CODES[gLanguageSetting], gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_FOLLOW, gFollow ? __TEXT("1") : __TEXT("0"), gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_TOOLBAR, gToolbar ? __TEXT("1") : __TEXT("0"), gIniPath.c_str());
	}
}

//---------------------------------------------------------

struct VoiceInfo
{
	std::wstring id;
	std::wstring name;
};

// Lists the SAPI voices, then the OneCore ones which SAPI cannot see
std::vector<VoiceInfo> EnumVoices()
{
	std::vector<VoiceInfo> voices;
	ISpObjectTokenCategory *pCategory = NULL;

	if ( ! gCanSpeek ||
		FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, IID_ISpObjectTokenCategory, (void **)&pCategory)) )
		return voices;

	IEnumSpObjectTokens *pEnum = NULL;

	if (SUCCEEDED(pCategory->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(pCategory->EnumTokens(NULL, NULL, &pEnum)))
	{
		ISpObjectToken *pToken = NULL;

		while (pEnum->Next(1, &pToken, NULL) == S_OK)
		{
			LPWSTR id = NULL;
			LPWSTR name = NULL;

			if (SUCCEEDED(pToken->GetId(&id)))
			{
				VoiceInfo info;
				info.id = id;

				// The default value of a voice token is its display name
				info.name = SUCCEEDED(pToken->GetStringValue(NULL, &name)) ? name : id;
				voices.push_back(info);
			}

			CoTaskMemFree(id);
			CoTaskMemFree(name);
			pToken->Release();
		}

		pEnum->Release();
	}

	pCategory->Release();

	for (const OneCoreVoice::Info &oneCore : OneCoreVoice::EnumVoices())
	{
		VoiceInfo info;
		info.id = ONECORE_PREFIX + oneCore.id;
		info.name = oneCore.name;
		voices.push_back(info);
	}

	return voices;
}

//---------------------------------------------------------

// The Notepad++ settings folder is two levels above plugins\config
std::wstring GetNppSettingsDir()
{
	std::wstring path = gConfigDir;

	for (int i = 0; i < 2; ++i)
	{
		size_t pos = path.find_last_of(__TEXT('\\'));
		if (pos == std::wstring::npos)
			return std::wstring();
		path.resize(pos);
	}

	return path;
}

bool ReadFileContent(const std::wstring &path, std::string &content)
{
	std::ifstream file(path, std::ios::binary);

	if (!file)
		return false;

	content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}

// Notepad++ copies its translation file to nativeLang.xml, which does not exist in English
Language ResolveLanguage(Language setting)
{
	if (setting != LANGUAGE_AUTO)
		return setting;

	std::string content;
	std::wstring settingsDir = GetNppSettingsDir();

	if (!settingsDir.empty() && ReadFileContent(settingsDir + __TEXT("\\nativeLang.xml"), content) &&
		(content.find("filename=\"french.xml\"") != std::string::npos))
		return LANGUAGE_FRENCH;

	return LANGUAGE_ENGLISH;
}

std::string ToUtf8(const std::wstring &text)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
	std::string utf8(len, 0);
	if (len > 0)
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], len, NULL, NULL);
	return utf8;
}

// Notepad++ only reads plugin commands for its right-click menu from contextMenu.xml
const char CONTEXT_MENU_END[] = "</ScintillaContextMenu>";
const char CONTEXT_MENU_MARK[] = "PluginEntryName=\"Speech\"";
const TextId CONTEXT_MENU_ITEMS[] =
{
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
};

std::wstring GetContextMenuPath()
{
	std::wstring settingsDir = GetNppSettingsDir();
	return settingsDir.empty() ? settingsDir : settingsDir + __TEXT("\\contextMenu.xml");
}

bool ReadContextMenu(std::string &content)
{
	return ReadFileContent(GetContextMenuPath(), content) && (content.find(CONTEXT_MENU_END) != std::string::npos);
}

bool HasSpeechContextMenu()
{
	std::string content;
	return ReadContextMenu(content) && (content.find(CONTEXT_MENU_MARK) != std::string::npos);
}

// Rewrites our entries, so that an older list of items or another language is updated too.
// The names must match the plugin menu of the next Notepad++ session, hence the language parameter.
bool SetSpeechContextMenu(bool enable, Language language, bool &changed)
{
	std::wstring path = GetContextMenuPath();
	std::string content;

	changed = false;

	if (!ReadContextMenu(content))
		return false;

	const std::string original = content;

	const char *eol = (content.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

	// Remove our previous entries, whole lines
	size_t mark;
	while ((mark = content.find(CONTEXT_MENU_MARK)) != std::string::npos)
	{
		size_t lineStart = content.rfind('\n', mark);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		size_t lineEnd = content.find('\n', mark);
		lineEnd = (lineEnd == std::string::npos) ? content.size() : lineEnd + 1;
		content.erase(lineStart, lineEnd - lineStart);
	}

	if (enable)
	{
		size_t endPos = content.rfind(CONTEXT_MENU_END);
		size_t lineStart = content.rfind('\n', endPos);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;

		std::string items;
		for (TextId item : CONTEXT_MENU_ITEMS)
		{
			// Notepad++ compares the names without their '&' shortcut marks
			std::wstring name = GetText(language, item);
			name.erase(std::remove(name.begin(), name.end(), __TEXT('&')), name.end());

			items += "        <Item FolderName=\"Speech\" " + std::string(CONTEXT_MENU_MARK) +
				" PluginCommandItemName=\"" + ToUtf8(name) + "\"/>" + eol;
		}

		content.insert(lineStart, items);
	}

	if (content == original)
		return true;

	changed = true;

	CopyFile(path.c_str(), (path + __TEXT(".bak")).c_str(), FALSE);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(content.data(), content.size());
	return file.good();
}

// Entries written by an older version of the plugin are updated for the next start of Notepad++
void RefreshContextMenu()
{
	bool changed = false;

	if (HasSpeechContextMenu())
		SetSpeechContextMenu(true, gLanguage, changed);
}

void SpeakFaster()
{
	SetSpeechRate(gRate + 1);
	SaveSettings();
}

void SpeakSlower()
{
	SetSpeechRate(gRate - 1);
	SaveSettings();
}

void UpdateRateLabel(HWND hDlg)
{
	std::wstring label = (gRate > 0) ? __TEXT("+") + std::to_wstring(gRate) : std::to_wstring(gRate);
	SetDlgItemText(hDlg, IDC_RATE_VALUE, label.c_str());
}

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static long sInitialRate = 0;
	static std::wstring sInitialVoiceId;
	static bool sInitialContextMenu = false;
	static std::vector<VoiceInfo> sVoices;

	switch (message)
	{
	case WM_INITDIALOG:
	{
		sInitialRate = gRate;
		sInitialVoiceId = gVoiceId;
		sInitialContextMenu = HasSpeechContextMenu();

		SetWindowText(hDlg, GetText(gLanguage, TXT_DLG_CAPTION));
		SetDlgItemText(hDlg, IDC_VOICE_LABEL, GetText(gLanguage, TXT_DLG_VOICE));
		SetDlgItemText(hDlg, IDC_RATE_LABEL, GetText(gLanguage, TXT_DLG_SPEED));
		SetDlgItemText(hDlg, IDC_SLOWER_LABEL, GetText(gLanguage, TXT_DLG_SLOWER));
		SetDlgItemText(hDlg, IDC_FASTER_LABEL, GetText(gLanguage, TXT_DLG_FASTER));
		SetDlgItemText(hDlg, IDC_FOLLOW, GetText(gLanguage, TXT_DLG_FOLLOW));
		SetDlgItemText(hDlg, IDC_TOOLBAR, GetText(gLanguage, TXT_DLG_TOOLBAR));
		SetDlgItemText(hDlg, IDC_CONTEXT_MENU, GetText(gLanguage, TXT_DLG_CONTEXT_MENU));
		SetDlgItemText(hDlg, IDC_LANGUAGE_LABEL, GetText(gLanguage, TXT_DLG_LANGUAGE));
		SetDlgItemText(hDlg, IDC_VOICE_TEST, GetText(gLanguage, TXT_DLG_TEST));
		SetDlgItemText(hDlg, IDC_RATE_RESET, GetText(gLanguage, TXT_DLG_NORMAL));
		SetDlgItemText(hDlg, IDCANCEL, GetText(gLanguage, TXT_DLG_CANCEL));

		// Same order as the Language enum; language names are not translated
		SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_ADDSTRING, 0, (LPARAM)GetText(gLanguage, TXT_DLG_LANGUAGE_AUTO));
		SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_ADDSTRING, 0, (LPARAM)__TEXT("English"));
		SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_ADDSTRING, 0, (LPARAM)__TEXT("Français"));
		SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_SETCURSEL, gLanguageSetting, 0);

		// Combo item 0 is the default voice, item i+1 is sVoices[i]
		sVoices = EnumVoices();
		SendDlgItemMessage(hDlg, IDC_VOICE_COMBO, CB_ADDSTRING, 0, (LPARAM)GetText(gLanguage, TXT_DLG_DEFAULT_VOICE));
		int selected = 0;
		for (size_t i = 0; i < sVoices.size(); ++i)
		{
			SendDlgItemMessage(hDlg, IDC_VOICE_COMBO, CB_ADDSTRING, 0, (LPARAM)sVoices[i].name.c_str());
			if (_wcsicmp(sVoices[i].id.c_str(), gVoiceId.c_str()) == 0)
				selected = (int)i + 1;
		}
		SendDlgItemMessage(hDlg, IDC_VOICE_COMBO, CB_SETCURSEL, selected, 0);

		CheckDlgButton(hDlg, IDC_FOLLOW, gFollow ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_TOOLBAR, gToolbar ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CONTEXT_MENU, sInitialContextMenu ? BST_CHECKED : BST_UNCHECKED);
		if (GetContextMenuPath().empty())
			EnableWindow(GetDlgItem(hDlg, IDC_CONTEXT_MENU), FALSE);

		SendMessage(nppData._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, NppDarkMode::dmfInit, (LPARAM)hDlg);

		// TBM_SETRANGE packs both bounds as unsigned words, which breaks the negative minimum
		SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_SETRANGEMIN, FALSE, RATE_MIN);
		SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_SETRANGEMAX, FALSE, RATE_MAX);
		SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_SETPAGESIZE, 0, 5);
		SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_SETPOS, TRUE, gRate);
		UpdateRateLabel(hDlg);
		return TRUE;
	}

	case WM_HSCROLL:
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_RATE_SLIDER))
		{
			SetSpeechRate((long)SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_GETPOS, 0, 0));
			UpdateRateLabel(hDlg);
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_RATE_RESET:
			SetSpeechRate(0);
			SendDlgItemMessage(hDlg, IDC_RATE_SLIDER, TBM_SETPOS, TRUE, gRate);
			UpdateRateLabel(hDlg);
			return TRUE;

		case IDC_VOICE_COMBO:
			if (HIWORD(wParam) == CBN_SELCHANGE)
			{
				int sel = (int)SendDlgItemMessage(hDlg, IDC_VOICE_COMBO, CB_GETCURSEL, 0, 0);
				SetSpeechVoice((sel > 0) ? sVoices[sel - 1].id : std::wstring());
			}
			return TRUE;

		case IDC_VOICE_TEST:
			ReadText(GetText(gLanguage, TXT_TEST_SENTENCE));
			return TRUE;

		case IDOK:
		{
			int languageSel = (int)SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_GETCURSEL, 0, 0);
			if ((languageSel >= LANGUAGE_AUTO) && (languageSel <= LANGUAGE_FRENCH))
				gLanguageSetting = (Language)languageSel;

			gFollow = (IsDlgButtonChecked(hDlg, IDC_FOLLOW) == BST_CHECKED);
			if (gFollow)
				HighlightSentence();
			else
				ClearHighlight();

			bool toolbar = (IsDlgButtonChecked(hDlg, IDC_TOOLBAR) == BST_CHECKED);
			bool restart = (toolbar != gToolbar);
			gToolbar = toolbar;

			SaveSettings();

			Language nextLanguage = ResolveLanguage(gLanguageSetting);
			restart = restart || (nextLanguage != gLanguage);

			bool contextMenu = (IsDlgButtonChecked(hDlg, IDC_CONTEXT_MENU) == BST_CHECKED);
			if (contextMenu || sInitialContextMenu)
			{
				bool changed = false;

				if (!SetSpeechContextMenu(contextMenu, nextLanguage, changed))
					MessageBox(hDlg, GetText(gLanguage, TXT_CONTEXT_MENU_ERROR), PLUGIN_NAME, MB_ICONWARNING);
				restart = restart || changed;
			}

			if (restart)
				MessageBox(hDlg, GetText(gLanguage, TXT_RESTART), PLUGIN_NAME, MB_ICONINFORMATION);

			EndDialog(hDlg, IDOK);
			return TRUE;
		}

		case IDCANCEL:
			SetSpeechRate(sInitialRate);
			SetSpeechVoice(sInitialVoiceId);
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

void ShowSettings()
{
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);

	DialogBox(gHInst, MAKEINTRESOURCE(IDD_SETTINGS), nppData._nppHandle, SettingsDlgProc);
}

//---------------------------------------------------------

HWND GetCurrentEditHandle()
{
	int currentEdit = 0;

	SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&currentEdit);

	return (currentEdit == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

void Cleanup()
{
	StopSpeech();

	if ( gCanSpeek )
	{
		// The voice must be released before COM is uninitialized
		gVoice.Release();
		OneCoreVoice::Release();

		CoUninitialize();
		gCanSpeek = false;
	}

	if (gNotifyWnd != NULL)
	{
		DestroyWindow(gNotifyWnd);
		gNotifyWnd = NULL;
	}
}
