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
const TCHAR *const LANGUAGE_CODES[] = { __TEXT("auto"), __TEXT("en"), __TEXT("fr") };
const int nbFunc = 10;

// Speaking rate range accepted by ISpVoice::SetRate
const long RATE_MIN = -10;
const long RATE_MAX = 10;

HINSTANCE gHInst = NULL;
NppData nppData;
bool gCanSpeek = false;
long gRate = 0;
// SAPI token id, or ONECORE_PREFIX + Windows.Media voice id, empty for the system default voice
const TCHAR ONECORE_PREFIX[] = __TEXT("OneCore:");
std::wstring gVoiceId;
Language gLanguageSetting = LANGUAGE_AUTO;
Language gLanguage = LANGUAGE_ENGLISH;	// Resolved once: Notepad++ builds the plugin menu only at startup
std::wstring gConfigDir;
std::wstring gIniPath;
FuncItem funcItem[nbFunc];

void Cleanup();
void LoadSettings();
Language ResolveLanguage(Language setting);

void SpeakDocument();
void SpeakSelection();
void StopSpeech();
void PauseSpeech();
void ResumeSpeech();
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

			funcItem[0]._pFunc = SpeakSelection;
			funcItem[1]._pFunc = SpeakDocument;
			funcItem[2]._pFunc = NULL;
			funcItem[3]._pFunc = StopSpeech;
			funcItem[4]._pFunc = PauseSpeech;
			funcItem[5]._pFunc = ResumeSpeech;
			funcItem[6]._pFunc = NULL;
			funcItem[7]._pFunc = SpeakFaster;
			funcItem[8]._pFunc = SpeakSlower;
			funcItem[9]._pFunc = ShowSettings;

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

	gLanguage = ResolveLanguage(gLanguageSetting);

	lstrcpy(funcItem[0]._itemName, GetText(gLanguage, TXT_SPEAK_SELECTION));
	lstrcpy(funcItem[1]._itemName, GetText(gLanguage, TXT_SPEAK_DOCUMENT));
	lstrcpy(funcItem[2]._itemName, __TEXT("-SEPARATOR-"));
	lstrcpy(funcItem[3]._itemName, GetText(gLanguage, TXT_STOP_SPEECH));
	lstrcpy(funcItem[4]._itemName, GetText(gLanguage, TXT_PAUSE_SPEECH));
	lstrcpy(funcItem[5]._itemName, GetText(gLanguage, TXT_RESUME_SPEECH));
	lstrcpy(funcItem[6]._itemName, __TEXT("-SEPARATOR-"));
	lstrcpy(funcItem[7]._itemName, GetText(gLanguage, TXT_SPEAK_FASTER));
	lstrcpy(funcItem[8]._itemName, GetText(gLanguage, TXT_SPEAK_SLOWER));
	lstrcpy(funcItem[9]._itemName, GetText(gLanguage, TXT_SETTINGS));
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
	case NPPN_SHUTDOWN:
	{
		Cleanup();
	}
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

			this->SetVoiceId(gVoiceId);
			mIsPaused= false;
		}

		return true;
	}

	void Speak(const std::wstring &txt)
	{
		if (Create())
		{
			// SPF_IS_NOT_XML: text starting with '<' (XML, HTML...) must be read, not parsed as SAPI markup
			mVoice->Speak(txt.c_str(), SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_NOT_XML, NULL);

			// A paused voice would otherwise stay silent
			this->Resume();
		}
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

void SpeekText(const std::wstring &txt);
void SpeakRange(HWND editHandle, Sci_Position start, Sci_Position end);
HWND GetCurrentEditHandle();

//---------------------------------------------------------

void SpeakDocument()
{
	HWND editHandle = GetCurrentEditHandle();

	assert(editHandle != NULL);

	if (editHandle != NULL)
	{
		Sci_Position txtLen = (Sci_Position)SendMessage(editHandle, SCI_GETTEXTLENGTH, 0, 0);

		if ( txtLen > 0 )
			SpeakRange(editHandle, 0, txtLen);
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
			SpeakRange(editHandle, selStart, selEnd);
		}
		else
		{
			MessageBox(nppData._nppHandle, GetText(gLanguage, TXT_NO_SELECTION), GetText(gLanguage, TXT_NO_SELECTION_TITLE), 0);
		}
	}
}

bool IsOneCoreVoice(const std::wstring &voiceId)
{
	return voiceId.compare(0, _countof(ONECORE_PREFIX) - 1, ONECORE_PREFIX) == 0;
}

void StopSpeech()
{
	gVoice.Stop();
	OneCoreVoice::Stop();
}

void PauseSpeech()
{
	gVoice.Pause();
	OneCoreVoice::Pause();
}

void ResumeSpeech()
{
	gVoice.Resume();
	OneCoreVoice::Resume();
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
	}
}

void SaveSettings()
{
	if (!gIniPath.empty())
	{
		WritePrivateProfileString(INI_SECTION, INI_KEY_RATE, std::to_wstring(gRate).c_str(), gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_VOICE, gVoiceId.c_str(), gIniPath.c_str());
		WritePrivateProfileString(INI_SECTION, INI_KEY_LANGUAGE, LANGUAGE_CODES[gLanguageSetting], gIniPath.c_str());
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
	TXT_SPEAK_DOCUMENT,
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
			SpeekText(GetText(gLanguage, TXT_TEST_SENTENCE));
			return TRUE;

		case IDOK:
		{
			int languageSel = (int)SendDlgItemMessage(hDlg, IDC_LANGUAGE_COMBO, CB_GETCURSEL, 0, 0);
			if ((languageSel >= LANGUAGE_AUTO) && (languageSel <= LANGUAGE_FRENCH))
				gLanguageSetting = (Language)languageSel;

			SaveSettings();

			Language nextLanguage = ResolveLanguage(gLanguageSetting);
			bool restart = (nextLanguage != gLanguage);

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

// Reads [start, end) from Scintilla and converts it from the document code page to UTF-16
void SpeakRange(HWND editHandle, Sci_Position start, Sci_Position end)
{
	Sci_Position len = end - start;

	// MultiByteToWideChar only takes int lengths
	if (len > INT_MAX)
	{
		MessageBox(nppData._nppHandle, GetText(gLanguage, TXT_TOO_LARGE), PLUGIN_NAME, 0);
		return;
	}

	std::string buf(len + 1, 0);

	struct Sci_TextRangeFull tr{};
	tr.chrg.cpMin = start;
	tr.chrg.cpMax = end;
	tr.lpstrText = &buf[0];

	SendMessage(editHandle, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);

	// Code page is 0 for ANSI documents and SC_CP_UTF8 for Unicode ones
	UINT codePage = (UINT)SendMessage(editHandle, SCI_GETCODEPAGE, 0, 0);
	if (codePage == 0)
		codePage = CP_ACP;

	// Invalid sequences are replaced by U+FFFD instead of failing
	int wlen = MultiByteToWideChar(codePage, 0, buf.c_str(), (int)len, NULL, 0);

	if (wlen > 0)
	{
		std::wstring wbuf(wlen, 0);
		MultiByteToWideChar(codePage, 0, buf.c_str(), (int)len, &wbuf[0], wlen);

		SpeekText(wbuf);
	}
}

void SpeekText(const std::wstring &txt)
{
	if ( ! txt.empty() && gCanSpeek )
	{
		if (IsOneCoreVoice(gVoiceId))
			OneCoreVoice::Speak(txt, gVoiceId.substr(_countof(ONECORE_PREFIX) - 1), gRate);
		else
			gVoice.Speak(txt);
	}
}

HWND GetCurrentEditHandle()
{
	int currentEdit = 0;

	SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&currentEdit);

	return (currentEdit == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

void Cleanup()
{
	if ( gCanSpeek )
	{
		// The voice must be released before COM is uninitialized
		gVoice.Release();
		OneCoreVoice::Release();

		CoUninitialize();
		gCanSpeek = false;
	}
}
