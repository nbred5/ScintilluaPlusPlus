// This file is part of Scintillua++.
// 
// Copyright (C)2017 Justin Dailey <dail8859@yahoo.com>
// 
// Scintillua++ is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include <string>
#include <fstream>
#include <streambuf>

#include "PluginDefinition.h"
#include "Version.h"
#include "AboutDialog.h"
#include "LanguageDialog.h"
#include "resource.h"
#include "Config.h"
#include "ScintillaGateway.h"
#include "Utilities.h"
#include "menuCmdID.h"

static HANDLE _hModule;
static NppData nppData;
static Configuration config;
static std::map<std::string, std::string> bufferLanguages;

// Helper functions
static HWND GetCurrentScintilla();
static bool DetermineLanguageFromFileName();

// Menu callbacks
static void editSettings();
static void showAbout();
static void setLanguage();
static void editLanguageDefinition();
static void createNewLanguageDefinition();

FuncItem funcItem[] = {
	{ TEXT("Set Language..."), setLanguage, 0, false, nullptr },
	{ TEXT(""), nullptr, 0, false, nullptr }, // separator
	{ TEXT("Create New Language Definition..."), createNewLanguageDefinition, 0, false, nullptr },
	{ TEXT("Edit Language Definition..."), editLanguageDefinition, 0, false, nullptr },
	{ TEXT("Edit Settings..."), editSettings, 0, false, nullptr },
	{ TEXT(""), nullptr, 0, false, nullptr }, // separator
	{ TEXT("About..."), showAbout, 0, false, nullptr }
};

static HWND GetCurrentScintilla() {
	int id;
	SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&id);
	if (id == 0) return nppData._scintillaMainHandle;
	else return nppData._scintillaSecondHandle;
}

static void GetFullPathFromBufferID(std::wstring &fname, uptr_t bufferid) {
	auto len = SendMessage(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferid, NULL);

	fname.resize(len + 1, L'\0');

	SendMessage(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferid, (LPARAM)&fname[0]);

	// Remove the null
	fname.pop_back();
}

static std::string DetermineLanguageFromFileName(const std::string &fileName) {
	for (const auto &kv : config.file_extensions) {
		for (const auto &ext : kv.second) {
			if (MatchWild(ext.c_str(), ext.size(), fileName.c_str(), true)) {
				return kv.first;
			}
		}
	}

	return std::string("");
}

static void SetLexer(const ScintillaGateway &editor, const std::string &language) {
	if (language.empty())
		return;

	wchar_t config_dir[MAX_PATH] = { 0 };
	SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)config_dir);
	wcscat_s(config_dir, MAX_PATH, L"\\Scintillua++");

	editor.SetLexerLanguage("lpeg");

	if (editor.GetLexer() == 1 /*SCLEX_NULL*/) {
		MessageBox(NULL, L"Failed to set LPeg lexer", NPP_PLUGIN_NAME, MB_OK | MB_ICONERROR);
		return;
	}

	editor.SetProperty("lexer.lpeg.home", UTF8FromString(config_dir).c_str());
	editor.SetProperty("lexer.lpeg.color.theme", config.theme.c_str());
	editor.SetProperty("fold", "1");

	editor.PrivateLexerCall(SCI_GETDIRECTFUNCTION, editor.GetDirectFunction());
	editor.PrivateLexerCall(SCI_SETDOCPOINTER, editor.GetDirectPointer());
	editor.PrivateLexerCall(SCI_SETLEXERLANGUAGE, reinterpret_cast<sptr_t>(language.c_str()));

	// Always show the folding margin. Since N++ doesn't recognize the file it won't have the margin showing.
	editor.SetMarginWidthN(2, 14);

	editor.Colourise(0, -1);

	// Check for errors
	char buffer[512] = { 0 };
	editor.PrivateLexerCall(SCI_GETSTATUS, reinterpret_cast<sptr_t>(buffer));
	if (strlen(buffer) > 0) {
		MessageBox(nppData._nppHandle, StringFromUTF8(buffer).c_str(), NPP_PLUGIN_NAME, MB_OK | MB_ICONERROR);
		return;
	}

	std::wstring ws = StringFromUTF8(language);
	ws += L" (lpeg)";
	SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(ws.c_str()));
}

static void CheckFileForNewLexer() {
	std::string language;
	
	ScintillaGateway editor(GetCurrentScintilla());

	wchar_t fileName[MAX_PATH] = { 0 };
	SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, 0, (LPARAM)fileName);
	std::string fileNameC = UTF8FromString(fileName).c_str();
	const auto search = bufferLanguages.find(fileNameC);
	if (search != bufferLanguages.end()) {
		language = search->second;
	}
	else {
		language = editor.GetLexerLanguage();
		bufferLanguages[fileNameC] = language;
	}

	if (config.over_ride || (editor.GetLexer() == 1) /*SCLEX_NULL*/) {
		wchar_t ext[MAX_PATH] = { 0 };
		SendMessage(nppData._nppHandle, NPPM_GETFILENAME, MAX_PATH, (LPARAM)ext);
		std::string languageFromFileName = DetermineLanguageFromFileName(UTF8FromString(ext).c_str());
		if (languageFromFileName.empty() && language != "null") {
			languageFromFileName = language;
		}
		SetLexer(editor, languageFromFileName);
	}
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  reasonForCall, LPVOID lpReserved) {
	switch (reasonForCall) {
		case DLL_PROCESS_ATTACH:
			_hModule = hModule;
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

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
	nppData = notepadPlusData;
	ConfigLoad(&nppData, &config);
}

extern "C" __declspec(dllexport) const wchar_t * getName() {
	return NPP_PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *nbF) {
	*nbF = sizeof(funcItem) / sizeof(funcItem[0]);
	return funcItem;
}

extern "C" __declspec(dllexport) void beNotified(const SCNotification *notify) {
	static bool isReady = false;
	static std::wstring fileBeingSaved;

	// Somehow we are getting notifications from other scintilla handles at times
	if (notify->nmhdr.hwndFrom != nppData._nppHandle &&
		notify->nmhdr.hwndFrom != nppData._scintillaMainHandle &&
		notify->nmhdr.hwndFrom != nppData._scintillaSecondHandle)
		return;

	switch (notify->nmhdr.code) {
#if _DEBUG
		case SCN_UPDATEUI:
			if (notify->updated & SC_UPDATE_SELECTION) {
				ScintillaGateway editor(GetCurrentScintilla());

				if (editor.GetLexerLanguage() == "lpeg") {
					// Make sure no errors occured
					if (editor.PrivateLexerCall(SCI_GETSTATUS, NULL) == 0) {
						char buffer[512] = { 0 };
						std::string text;

						editor.PrivateLexerCall(SCI_GETLEXERLANGUAGE, reinterpret_cast<sptr_t>(buffer));
						text = buffer;
						text += " (lpeg): ";
						editor.PrivateLexerCall(editor.GetStyleAt(editor.GetCurrentPos()), reinterpret_cast<sptr_t>(buffer));
						text += buffer;
						text += ' ';
						text += std::to_string(editor.GetStyleAt(editor.GetCurrentPos()));

						SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(StringFromUTF8(text).c_str()));
					}
				}
			}
			break;
#endif
		case NPPN_READY: {
			isReady = true;
			ConfigLoad(&nppData, &config);

			// Get the path to the external lexer
			wchar_t config_dir[MAX_PATH] = { 0 };
			SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)config_dir);
#ifdef _WIN64
			wcscat_s(config_dir, MAX_PATH, L"\\Scintillua++\\LexLPeg_64.dll");
#else
			wcscat_s(config_dir, MAX_PATH, L"\\Scintillua++\\LexLPeg.dll");
#endif
			std::string wconfig_dir = UTF8FromString(config_dir);

			ScintillaGateway editor1(nppData._scintillaMainHandle);
			ScintillaGateway editor2(nppData._scintillaSecondHandle);

			editor1.LoadLexerLibrary(wconfig_dir);
			editor2.LoadLexerLibrary(wconfig_dir);

			// Fall through - when launching N++, NPPN_BUFFERACTIVATED is received before
			// NPPN_READY. Thus the first file can get ignored so now we can check now...
		}
		case NPPN_FILERENAMED:
		case NPPN_BUFFERACTIVATED:
			if (!isReady)
				break;
			CheckFileForNewLexer();
			break;
		case NPPN_FILEBEFORESAVE: {
			// Notepad++ does not notify when a file has been renamed using the normal
			// "save as" dialog. So store the file name before the save then compare it
			// to the file name immediately after the save occurs. If they are different then
			// file has been renamed.
			// NOTE: this is different from the user doing "File > Rename..." which sends the
			// NPPN_FILERENAMED notification

			GetFullPathFromBufferID(fileBeingSaved, notify->nmhdr.idFrom);
			break;
		}
		case NPPN_FILESAVED: {
			std::wstring fileSaved;

			GetFullPathFromBufferID(fileSaved, notify->nmhdr.idFrom);

			if (fileSaved != fileBeingSaved) {
				// The file was saved as a different file name
				CheckFileForNewLexer();
			}
			else if (fileSaved.compare(GetIniFilePath(&nppData)) == 0) {
				// If the ini file was edited, reload it
				ConfigLoad(&nppData, &config);
			}
			fileBeingSaved.clear();
			break;
		}
	}
	return;
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam) {
	return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
	return TRUE;
}

static void editSettings() {
	//ConfigSave(&nppData, &config);
	SendMessage(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)GetIniFilePath(&nppData));
}

static void showAbout() {
	ShowAboutDialog((HINSTANCE)_hModule, MAKEINTRESOURCE(IDD_ABOUTDLG), nppData._nppHandle);
}

static void setLanguage() {
	std::string language = ShowLanguageDialog((HINSTANCE)_hModule, MAKEINTRESOURCE(IDD_LANGUAGEDLG), nppData._nppHandle, config);
	if (!language.empty()) {
		wchar_t fileName[MAX_PATH] = { 0 };
		SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, 0, (LPARAM)fileName);
		bufferLanguages[UTF8FromString(fileName).c_str()] = language;
		ScintillaGateway editor(GetCurrentScintilla());
		SetLexer(editor, language);
	}
}

static void editLanguageDefinition() {
	std::string language = ShowLanguageDialog((HINSTANCE)_hModule, MAKEINTRESOURCE(IDD_LANGUAGEDLG), nppData._nppHandle, config);
	if (!language.empty()) {
		wchar_t config_dir[MAX_PATH] = { 0 };
		SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)config_dir);
		wcscat_s(config_dir, MAX_PATH, L"\\Scintillua++\\");
		wcscat_s(config_dir, MAX_PATH, StringFromUTF8(language).c_str());
		wcscat_s(config_dir, MAX_PATH, L".lua");
		SendMessage(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)config_dir);
	}
}

static void createNewLanguageDefinition() {
	wchar_t config_dir[MAX_PATH] = { 0 };
	SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)config_dir);
	wcscat_s(config_dir, MAX_PATH, L"\\Scintillua++\\template.txt");
	std::ifstream t(config_dir);
	std::string str;

	t.seekg(0, std::ios::end);   
	str.reserve(t.tellg());
	t.seekg(0, std::ios::beg);

	str.assign((std::istreambuf_iterator<char>(t)),
				std::istreambuf_iterator<char>());
	SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, (LPARAM)IDM_FILE_NEW);
	ScintillaGateway editor(GetCurrentScintilla());
	editor.SetText(str);
	if (config.over_ride) {
		SetLexer(editor, "lua");
	}
	else {
		SendMessage(nppData._nppHandle, NPPM_SETCURRENTLANGTYPE, 0, (LPARAM)L_LUA);
	}
}