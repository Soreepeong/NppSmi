#pragma once

#include <Windows.h>
#include <memory>
#include <string>

#pragma warning(push, 0)        
#include "nlohmann/json.hpp"
#pragma warning(pop)

#ifdef UNICODE
typedef std::wstring STRING;
#else
typedef std::string STRING;
#endif

class NppSmi {
	HINSTANCE const m_hModule;  // NOLINT
	HWND const m_hNpp;  // NOLINT
	HWND const m_hSc1;  // NOLINT
	HWND const m_hSc2;  // NOLINT
	TCHAR m_configFilePath[MAX_PATH];
	TCHAR m_moduleName[MAX_PATH];

public:
	static TCHAR* const PLUGIN_NAME;
	static TCHAR* const PLUGIN_CONFIG_FILENAME;
	static std::shared_ptr<NppSmi> INSTANCE;
	std::vector<struct FuncItem> const FUNCTIONS;
	std::shared_ptr<std::list<struct ShortcutKey>> const SHORTCUT_KEYS;

private:

	struct {
		bool autoOpenMedia = true;
		bool forceShortcutIfSmi = true;
	} m_config;

	bool m_isCurrentDocumentSMI = false;
	void DetermineCurrentDocumentIsSmi();

	template<typename T>
	static T ReadConfig(const nlohmann::json& json, std::initializer_list<std::string> path, T defaultValue) {
		auto itemIndex = 0;
		nlohmann::json::const_iterator currentPath;
		for (const auto& pathItem : path) {
			nlohmann::json::const_iterator newPath;
			if (itemIndex == 0) {
				newPath = json.find(pathItem);
				if (newPath == json.end())
					return defaultValue;
			} else {
				newPath = currentPath->find(pathItem);
				if (newPath == currentPath->end())
					return defaultValue;
			}

			currentPath = newPath;
			++itemIndex;
		}
		try {
			return currentPath->get<T>();
		} catch (std::exception&) {
			// ignore wrong type
			return defaultValue;
		}
	}

	WNDPROC m_prevWndProc;
	LRESULT OnBeforeNppWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	HHOOK m_hhkLowLevelKeyboard = nullptr;
	LRESULT LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

	HWND GetCurrentScintilla() const;
	void SetMenuChecked(int menuIndex, bool isChecked);
	
	STRING FindOrAskSimilarMediaFile() const;
	void TryOpenMedia();

#pragma push_macro("MENU_FUNCTION")
#pragma push_macro("MENU_SEPARATOR")
	int m_menuIndexCounter = 0;
#define MENU_FUNCTION(NAME) void MenuFunction##NAME(); const int m_menuIndex##NAME = m_menuIndexCounter++;
#define MENU_SEPARATOR(i) const int m_menuIndexUnused##i = m_menuIndexCounter++;
	MENU_FUNCTION(ToggleForceShortcutIfSmi)
	MENU_FUNCTION(InsertBeginningTimecode)
	MENU_FUNCTION(InsertEndingTimecode)
	MENU_FUNCTION(UpdateCurrentLineTimestamp)
	MENU_SEPARATOR(0)
	MENU_FUNCTION(ToggleOpenMediaAutomatically)
	MENU_FUNCTION(OpenMedia)
	MENU_FUNCTION(PlayOrPause)
	MENU_FUNCTION(PlayCurrentLine)
	MENU_FUNCTION(GoToCurrentLine)
	MENU_FUNCTION(Rewind)
	MENU_FUNCTION(FastForward)
#pragma pop_macro("MENU_FUNCTION")
#pragma pop_macro("MENU_SEPARATOR")

	std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> GetMenuFunctions() const;

	NppSmi(HINSTANCE hModule, const struct NppData &data, const std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> & menus);

public:
	NppSmi(HINSTANCE hModule, const struct NppData &data);
	~NppSmi();

	void OnScintillaMessage(struct SCNotification *notifyCode);
	LRESULT OnCalledByNppWndProc(UINT Message, WPARAM wParam, LPARAM lParam);
};


