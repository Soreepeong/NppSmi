#include "NppSmi.h"
#include "PluginInterface.h"
#include <shlwapi.h>
#include <fstream>
#include "MpcHcRemote.h"
#include <regex>

namespace UsefulRegexs {
	const std::regex SYNC_MATCHER("<sync(?=\\s)[^<>]*\\sstart=(['\"]?)(\\d+)\\1(?=\\s|>)[^<>]*>(?:<P>)?", std::regex_constants::icase);
	const std::regex TAG_REMOVER("</?p\\b[^<>]*>", std::regex_constants::icase);
	const std::regex BR_REPLACER("<br\\b[^<>]*>", std::regex_constants::icase);
	const std::regex MULTILINE_MATCHER("\n+");
	const std::regex SPACE_MATCHER("\\s+");
}

NppSmi::NppSmi(HINSTANCE hModule, const struct NppData &data, const std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> & menus)
	: m_hModule(hModule)
	, m_hNpp(data._nppHandle)
	, m_hSc1(data._scintillaMainHandle)
	, m_hSc2(data._scintillaSecondHandle)
	, m_configFilePath{ 0 }
	, m_moduleName{ 0 }
	, FUNCTIONS(std::get<0>(menus))
	, SHORTCUT_KEYS(std::get<1>(menus)) {

	WSADATA w;
	WSAStartup((MAKEWORD(2, 2)), &w);

	SendMessage(m_hNpp, NPPM_GETPLUGINSCONFIGDIR, sizeof m_configFilePath, reinterpret_cast<LPARAM>(m_configFilePath));
	if (PathFileExists(m_configFilePath) == FALSE)
		CreateDirectory(m_configFilePath, nullptr);
	PathAppend(m_configFilePath, PLUGIN_CONFIG_FILENAME);

	nlohmann::json config;
	try {
		std::ifstream configFile(m_configFilePath);
		config = nlohmann::json::parse(configFile);
	} catch (std::exception&) {
		// Ignore malformed JSON
	}
	m_config.autoOpenMedia = ReadConfig(config, { "autoOpenMedia" }, m_config.autoOpenMedia);
	m_config.forceShortcutIfSmi = ReadConfig(config, { "forceShortcutIfSmi" }, m_config.forceShortcutIfSmi);

	GetModuleFileName(hModule, m_moduleName, sizeof m_moduleName);
	_tcsncpy_s(m_moduleName, MAX_PATH, _tcsrchr(m_moduleName, '\\') + 1, MAX_PATH);

	m_hhkLowLevelKeyboard = SetWindowsHookEx(WH_KEYBOARD_LL, static_cast<HOOKPROC>([](int nCode, WPARAM wParam, LPARAM lParam) {
		return INSTANCE->LowLevelKeyboardProc(nCode, wParam, lParam);
	}), m_hModule, 0);

	m_prevWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_hNpp, GWLP_WNDPROC));
	SetWindowLongPtr(m_hNpp, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(static_cast<WNDPROC>([](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		return INSTANCE->OnBeforeNppWndProc(hwnd, uMsg, wParam, lParam);
	})));
}

NppSmi::NppSmi(HINSTANCE hModule, const NppData &data)
	: NppSmi(hModule, data, GetMenuFunctions()) {
}

NppSmi::~NppSmi() {

	SetWindowLongPtr(m_hNpp, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_prevWndProc));
	UnhookWindowsHookEx(m_hhkLowLevelKeyboard);

	WSACleanup();

	typedef nlohmann::json JSON;
	std::ofstream configFile(m_configFilePath);
	configFile << JSON::object({
		{ "autoOpenMedia", m_config.autoOpenMedia },
		{ "forceShortcutIfSmi", m_config.forceShortcutIfSmi },
		}).dump();
}

void NppSmi::OnScintillaMessage(SCNotification* notifyCode) {
	switch (notifyCode->nmhdr.code) {
		case NPPN_BUFFERACTIVATED:
			DetermineCurrentDocumentIsSmi();
			break;

		case NPPN_FILEOPENED:
			DetermineCurrentDocumentIsSmi();
			if (m_isCurrentDocumentSMI)
				SendMessage(m_hNpp, NPPM_SETCURRENTLANGTYPE, 0, L_HTML);
			break;
	}
}

void NppSmi::DetermineCurrentDocumentIsSmi() {
	TCHAR extension[MAX_PATH];
	SendMessage(m_hNpp, NPPM_GETEXTPART, MAX_PATH, reinterpret_cast<LPARAM>(extension));
	m_isCurrentDocumentSMI = 0 == StrNCmpI(extension, TEXT(".SMI"), 4);
}

LRESULT NppSmi::OnBeforeNppWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	return CallWindowProc(m_prevWndProc, hwnd, uMsg, wParam, lParam);
}

LRESULT NppSmi::OnCalledByNppWndProc(UINT, WPARAM, LPARAM) {
	return TRUE;
}

LRESULT NppSmi::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION &&
		m_config.forceShortcutIfSmi &&
		m_isCurrentDocumentSMI &&
		GetForegroundWindow() == m_hNpp) {
		switch (wParam) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYUP:
				const auto &p = *reinterpret_cast<PKBDLLHOOKSTRUCT>(lParam);
				const auto ctrl = !!GetAsyncKeyState(VK_CONTROL);
				const auto alt = !!GetAsyncKeyState(VK_MENU);
				const auto shift = !!GetAsyncKeyState(VK_SHIFT);
				for (const auto &fn : FUNCTIONS) {
					if (fn._pShKey == nullptr)
						continue;

					const auto &key = *fn._pShKey;
					if (key._isCtrl != ctrl || key._isAlt != alt || key._isShift != shift || key._key != p.vkCode)
						continue;

					switch (wParam) {
						case WM_KEYDOWN:
						case WM_SYSKEYDOWN:
							PostMessage(m_hNpp, WM_COMMAND, fn._cmdID, 0);
					}
					return 1;
				}
		}
	}
	return CallNextHookEx(m_hhkLowLevelKeyboard, nCode, wParam, lParam);
}

HWND NppSmi::GetCurrentScintilla() const {
	auto which = -1;
	SendMessage(m_hNpp, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
	return which == 0 ? m_hSc1 : m_hSc2;
}

void NppSmi::SetMenuChecked(int menuIndex, bool isChecked) {
	CheckMenuItem(GetMenu(m_hNpp), FUNCTIONS[menuIndex]._cmdID, MF_BYCOMMAND | (isChecked ? MF_CHECKED : MF_UNCHECKED));
}

STRING NppSmi::FindOrAskSimilarMediaFile() const {
	const auto *szMediaTypes = TEXT("3G2;3GP;3GP2;3GPP;AMV;ASF;AVI;DIVX;EVO;F4V;FLV;GVI;HDMOV;IFO;K3G;M2T;M2TS;MKV;MK3D;MOV;MP2V;MP4;MPE;MPEG;MPG;MPV2;MQV;MTS;MTV;NSV;OGM;OGV;QT;RM;RMVB;RV;SKM;TP;TPR;TS;VOB;WEBM;WM;WMP;WMV;A52;AAC;AC3;AIF;AIFC;AIFF;ALAC;AMR;APE;AU;CDA;DTS;FLA;FLAC;M1A;M2A;M4A;M4B;M4P;MID;MKA;MP1;MP2;MP3;MPA;MPC;MPP;MP+;NSA;OFR;OFS;OGA;OGG;RA;SND;SPX;TTA;WAV;WAVE;WMA;WV");
	TCHAR szFile[MAX_PATH] = { 0, };
	TCHAR szBasePath[MAX_PATH + 3] = { 0, };
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof ofn);

	SendMessage(m_hNpp, NPPM_GETFULLCURRENTPATH, MAX_PATH, reinterpret_cast<WPARAM>(szBasePath));
	auto pos = _tcsrchr(szBasePath, L'.');
	if (pos != nullptr) {
		_stprintf(pos, L".*");
		WIN32_FIND_DATA file;
		const auto hList = FindFirstFile(szBasePath, &file);
		if (hList != INVALID_HANDLE_VALUE) {
			do {
				if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					_tcslwr(file.cFileName);
					pos = _tcsrchr(file.cFileName, L'.');
					if (pos) {
						pos++;
						_tcsupr(pos);
						if (_tcsstr(szMediaTypes, pos)) {
							::SendMessage(m_hNpp, NPPM_GETCURRENTDIRECTORY, MAX_PATH, reinterpret_cast<WPARAM>(szFile));
							auto fnLen = _tcslen(szFile);
							if (szFile[fnLen - 1] != TEXT('\\'))
								wsprintf(szFile + fnLen++, TEXT("\\"));
							_tcsncpy(szFile + fnLen, file.cFileName, min(_tcslen(file.cFileName), min(sizeof(szFile), sizeof(file.cFileName))));
							break;
						}
					}
				}
			} while (FindNextFile(hList, &file));
			FindClose(hList);
		}
	}
	if (szFile[0] == NULL) {
		ofn.lStructSize = sizeof ofn;
		ofn.hwndOwner = m_hNpp;
		ofn.lpstrTitle = TEXT("Select media file to use with");
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof szFile;
		ofn.lpstrFilter = TEXT("All Video\0*.3G2;*.3GP;*.3GP2;*.3GPP;*.AMV;*.ASF;*.AVI;*.AVS;*.DIVX;*.EVO;*.F4V;*.FLV;*.GVI;*.HDMOV;*.IFO;*.K3G;*.M2T;*.M2TS;*.MKV;*.MK3D;*.MOV;*.MP2V;*.MP4;*.MPE;*.MPEG;*.MPG;*.MPV2;*.MQV;*.MTS;*.MTV;*.NSV;*.OGM;*.OGV;*.QT;*.RM;*.RMVB;*.RV;*.SKM;*.TP;*.TPR;*.TS;*.VOB;*.WEBM;*.WM;*.WMP;*.WMV\0All Audio\0*.A52;*.AAC;*.AC3;*.AIF;*.AIFC;*.AIFF;*.ALAC;*.AMR;*.APE;*.AU;*.CDA;*.DTS;*.FLA;*.FLAC;*.M1A;*.M2A;*.M4A;*.M4B;*.M4P;*.MID;*.MKA;*.MP1;*.MP2;*.MP3;*.MPA;*.MPC;*.MPP;*.MP+;*.NSA;*.OFR;*.OFS;*.OGA;*.OGG;*.RA;*.SND;*.SPX;*.TTA;*.WAV;*.WAVE;*.WMA;*.WV\0All Media\0*.3G2;*.3GP;*.3GP2;*.3GPP;*.AMV;*.ASF;*.AVI;*.AVS;*.DIVX;*.EVO;*.F4V;*.FLV;*.GVI;*.HDMOV;*.IFO;*.K3G;*.M2T;*.M2TS;*.MKV;*.MK3D;*.MOV;*.MP2V;*.MP4;*.MPE;*.MPEG;*.MPG;*.MPV2;*.MQV;*.MTS;*.MTV;*.NSV;*.OGM;*.OGV;*.QT;*.RM;*.RMVB;*.RV;*.SKM;*.TP;*.TPR;*.TS;*.VOB;*.WEBM;*.WM;*.WMP;*.WMV;*.A52;*.AAC;*.AC3;*.AIF;*.AIFC;*.AIFF;*.ALAC;*.AMR;*.APE;*.AU;*.CDA;*.DTS;*.FLA;*.FLAC;*.M1A;*.M2A;*.M4A;*.M4B;*.M4P;*.MID;*.MKA;*.MP1;*.MP2;*.MP3;*.MPA;*.MPC;*.MPP;*.MP+;*.NSA;*.OFR;*.OFS;*.OGA;*.OGG;*.RA;*.SND;*.SPX;*.TTA;*.WAV;*.WAVE;*.WMA;*.WV\0All Files\0*.*\0");
		ofn.nFilterIndex = 3;
		ofn.lpstrFileTitle = nullptr;
		ofn.nMaxFileTitle = 0;
		::SendMessage(m_hNpp, NPPM_GETCURRENTDIRECTORY, sizeof szBasePath, reinterpret_cast<WPARAM>(szBasePath));
		ofn.lpstrInitialDir = szBasePath;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
		if (GetOpenFileName(&ofn) != TRUE)
			return TEXT("");
	}
	return szFile;
}

void NppSmi::TryOpenMedia() {
	HANDLE h = CreateThread(nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>([](PVOID p) -> DWORD {
		NppSmi* const self = reinterpret_cast<NppSmi*>(p);
		STRING mpcHcPath;
		bool mpcHcFound;
		std::tie(mpcHcFound, mpcHcPath) = MpcHcRemote::GetInstallationPath();
		if (!mpcHcFound)
			MessageBox(self->m_hNpp, mpcHcPath.c_str(), TEXT("NppSmi Error"), MB_ICONERROR | MB_OK);

		const auto mediaFile = self->FindOrAskSimilarMediaFile();

		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof si);
		ZeroMemory(&pi, sizeof pi);
		si.cb = sizeof si;

		auto commandLine = TEXT("\"") + mpcHcPath + TEXT("\" \"") + mediaFile + TEXT("\"");
		if (CreateProcess(mpcHcPath.c_str(), &commandLine[0], nullptr, nullptr, false, 0, nullptr, nullptr, &si, &pi)) {
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		} else
			self->FormatMessageAndShowError(GetLastError());

		return 0;
	}), this, 0, nullptr);
	if (!h)
		FormatMessageAndShowError(GetLastError());
	else
		CloseHandle(h);
}

void NppSmi::FormatMessageAndShowError(DWORD dwMessageId) {
	LPTSTR errorText = nullptr;

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
		dwMessageId,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPTSTR>(&errorText),
		0,
		nullptr);

	if (errorText) {
		STRING str = TEXT("Could not open MPC-HC: ") + STRING(errorText);
		MessageBox(m_hNpp, str.c_str(), TEXT("NppSmi"), MB_OK | MB_ICONERROR);
		LocalFree(errorText);
	}
}

void NppSmi::MenuFunctionToggleForceShortcutIfSmi() {
	SetMenuChecked(m_menuIndexToggleForceShortcutIfSmi, m_config.forceShortcutIfSmi = !m_config.forceShortcutIfSmi);
}


void NppSmi::MenuFunctionInsertBeginningTimecode() {
	const auto time = MpcHcRemote::GetCurrentPlayerTimestamp();
	const auto curScintilla = GetCurrentScintilla();

	if (time == -1) {
		if (m_config.autoOpenMedia)
			TryOpenMedia();
		return;
	}

	LockWindowUpdate(m_hNpp);

	SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);
	SendMessage(curScintilla, SCI_HOME, 0, 0);

	auto replaced = false;
	const auto curLine = static_cast<size_t>(SendMessage(curScintilla, SCI_LINEFROMPOSITION, SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0));
	const std::string line(static_cast<size_t>(SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0)) + 1, '\0');
	SendMessage(curScintilla, SCI_GETLINE, curLine, reinterpret_cast<WPARAM>(&line[0]));

	std::string buf;
	buf.resize(512);
	buf.resize(snprintf(&buf[0], buf.capacity(), "<Sync Start=%lld><P>", time));

	std::smatch match;
	if (std::regex_search(line, match, UsefulRegexs::SYNC_MATCHER) && match.size() > 1) {
		SendMessage(curScintilla, SCI_SETANCHOR, match.str(0).length() + SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0), 0);

		SendMessage(curScintilla, SCI_REPLACESEL, NULL, reinterpret_cast<LPARAM>(&buf[0]));
		replaced = true;
	}

	if (!replaced)
		SendMessage(curScintilla, SCI_ADDTEXT, buf.size(), reinterpret_cast<LPARAM>(&buf[0]));

	const auto lineCount = static_cast<size_t>(SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0));
	if (curLine == lineCount - 1) { // last line?
		SendMessage(curScintilla, SCI_LINEEND, 0, 0);
		SendMessage(curScintilla, SCI_ADDTEXT, 2, reinterpret_cast<LPARAM>("\r\n"));
	} else
		SendMessage(curScintilla, SCI_GOTOLINE, curLine + 1, 0);

	if (!replaced)
		SendMessage(curScintilla, SCI_LINESCROLL, 0, 1);

	SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
	LockWindowUpdate(nullptr);
}

void NppSmi::MenuFunctionInsertEndingTimecode() {
	const auto time = MpcHcRemote::GetCurrentPlayerTimestamp();
	const auto curScintilla = GetCurrentScintilla();

	if (time == -1) {
		TryOpenMedia();
		return;
	}

	std::string buf;
	buf.resize(512);
	buf.resize(snprintf(&buf[0], buf.capacity(), "<Sync Start=%zd><P>&nbsp;\r\n", time));

	LockWindowUpdate(m_hNpp);

	SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);
	SendMessage(curScintilla, SCI_HOME, 0, 0);
	SendMessage(curScintilla, SCI_ADDTEXT, buf.size(), reinterpret_cast<LPARAM>(&buf[0]));
	SendMessage(curScintilla, SCI_LINESCROLL, 0, 1);
	SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
	LockWindowUpdate(nullptr);
}

void NppSmi::MenuFunctionToggleOpenMediaAutomatically() {
	SetMenuChecked(m_menuIndexToggleOpenMediaAutomatically, m_config.autoOpenMedia = !m_config.autoOpenMedia);
}

void NppSmi::MenuFunctionOpenMedia() {
	TryOpenMedia();
}

void NppSmi::MenuFunctionPlayOrPause() {
	if (!MpcHcRemote::SendCommand(889)) {
		if (m_config.autoOpenMedia)
			TryOpenMedia();
	}
}

void NppSmi::MenuFunctionPlayCurrentLine() {
}

void NppSmi::MenuFunctionGoToCurrentLine() {
}

void NppSmi::MenuFunctionRewind() {
	const auto time = MpcHcRemote::GetCurrentPlayerTimestamp();
	if (time == -1) {
		if (m_config.autoOpenMedia)
			TryOpenMedia();
	} else
		MpcHcRemote::Seek(time - 3000);
}

void NppSmi::MenuFunctionFastForward() {
	const auto time = MpcHcRemote::GetCurrentPlayerTimestamp();
	if (time == -1) {
		if (m_config.autoOpenMedia)
			TryOpenMedia();
	} else
		MpcHcRemote::Seek(time + 3000);
}

TCHAR* const NppSmi::PLUGIN_NAME = TEXT("NppSmi");
TCHAR* const NppSmi::PLUGIN_CONFIG_FILENAME = TEXT("NppSmi.json");
std::shared_ptr<NppSmi> NppSmi::INSTANCE = nullptr;

#define MENU_FN_SHORTCUT(DESCRIPTION, FUNCTION, CTRL, ALT, SHIFT, VK) menu.insert(menu.end(),{ TEXT(DESCRIPTION), []() { INSTANCE->MenuFunction##FUNCTION(); }, 0, false, (keys->push_back({CTRL, ALT, SHIFT, VK}), &(keys->back()))})
#define MENU_FN_CHECK(DESCRIPTION, FUNCTION, CHECKED) menu.insert(menu.end(), { TEXT(DESCRIPTION), []() { INSTANCE->MenuFunction##FUNCTION(); }, 0, !!CHECKED, nullptr })
#define MENU_FN(DESCRIPTION, FUNCTION) menu.insert(menu.end(), { TEXT(DESCRIPTION), []() { INSTANCE->MenuFunction##FUNCTION(); }, 0, false, nullptr })
#define MENU_SEPARATOR() menu.insert(menu.end(), { TEXT("---"), nullptr, 0, false, nullptr })

std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> NppSmi::GetMenuFunctions() const {
	std::vector<FuncItem> menu;
	std::shared_ptr<std::list<ShortcutKey>> keys = std::make_shared<std::list<ShortcutKey>>();
	MENU_FN_CHECK("Prioritize shortcut keys if a SMI file is active", ToggleForceShortcutIfSmi, m_config.forceShortcutIfSmi);
	MENU_FN_SHORTCUT("Insert beginning timecode", InsertBeginningTimecode, false, false, false, VK_F5);
	MENU_FN_SHORTCUT("Insert ending timecode", InsertEndingTimecode, false, false, false, VK_F6);
	MENU_SEPARATOR();
	MENU_FN_CHECK("Open media automatically", ToggleOpenMediaAutomatically, m_config.autoOpenMedia);
	MENU_FN("Open media", OpenMedia);
	MENU_FN_SHORTCUT("Play/Pause", PlayOrPause, false, false, false, VK_F9);
	MENU_FN_SHORTCUT("Play current line", PlayCurrentLine, false, false, false, VK_F10);
	MENU_FN_SHORTCUT("Go to current line", GoToCurrentLine, false, false, false, VK_F8);
	MENU_FN_SHORTCUT("Rewind", Rewind, true, true, false, VK_LEFT);
	MENU_FN_SHORTCUT("Fast forward", FastForward, true, true, false, VK_RIGHT);
	return std::make_tuple(menu, keys);
}
