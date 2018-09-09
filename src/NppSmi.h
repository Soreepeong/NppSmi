#pragma once

#include <Windows.h>
#include <memory>
#include <string>

#pragma warning(push, 0)        
#include "nlohmann/json.hpp"
#pragma warning(pop)

#ifdef UNICODE
typedef std::wstring SSTRING;
#else
typedef std::string SSTRING;
#endif

class NppSmi {
	HINSTANCE const m_hModule; // NOLINT(misc-misplaced-const)
	HWND const m_hNpp; // NOLINT(misc-misplaced-const)
	HWND const m_hSc1; // NOLINT(misc-misplaced-const)
	HWND const m_hSc2; // NOLINT(misc-misplaced-const)
	TCHAR m_configFilePath[MAX_PATH];
	TCHAR m_moduleName[MAX_PATH];

public:
	static const TCHAR* const PLUGIN_NAME;
	static const TCHAR* const PLUGIN_CONFIG_FILENAME;
	static std::shared_ptr<NppSmi> instance;

private:
	std::vector<struct FuncItem> const m_menuFunctions;
	std::shared_ptr<std::list<struct ShortcutKey>> const m_menuShortcutKeys;

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

	class ScintillaWorker;

	WNDPROC m_prevWndProc;
	LRESULT OnBeforeNppWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	HHOOK m_hhkLowLevelKeyboard = nullptr;
	LRESULT LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

	void SetMenuChecked(int menuIndex, bool isChecked);
	
	SSTRING FindOrAskSimilarMediaFile() const;
	void TryOpenMedia();
	void FormatMessageAndShowError(DWORD dwMessageId) const;

#pragma push_macro("MENU_FUNCTION")
#pragma push_macro("MENU_SEPARATOR")
	int m_menuIndexCounter = 0;
#define MENU_FUNCTION(NAME) void MenuFunction##NAME(); const int m_menuIndex##NAME = m_menuIndexCounter++;
#define MENU_SEPARATOR(i) const int m_menuIndexUnused##i = m_menuIndexCounter++;
	MENU_FUNCTION(ToggleForceShortcutIfSmi)
	MENU_FUNCTION(InsertBeginningTimecode)
	MENU_FUNCTION(InsertEndingTimecode)
	MENU_SEPARATOR(0)
	MENU_FUNCTION(ToggleOpenMediaAutomatically)
	MENU_FUNCTION(OpenMedia)
	MENU_FUNCTION(PlayOrPause)
	MENU_FUNCTION(GoToCurrentLine)
	MENU_FUNCTION(Rewind)
	MENU_FUNCTION(FastForward)
#pragma pop_macro("MENU_FUNCTION")
#pragma pop_macro("MENU_SEPARATOR")

	std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> CreateMenuFunctions() const;

	NppSmi(HINSTANCE hModule, const struct NppData &data, const std::tuple<std::vector<struct FuncItem>, std::shared_ptr<std::list<struct ShortcutKey>>> & menus);

public:
	NppSmi(HINSTANCE hModule, const struct NppData &data);
	NppSmi(const NppSmi&) = delete;
	NppSmi(NppSmi&&) = delete;
	NppSmi& operator =(const NppSmi&) = delete;
	NppSmi& operator =(NppSmi&&) = delete;
	~NppSmi();

	const std::vector<FuncItem> &GetMenuFunctions() const;
	void OnScintillaMessage(struct SCNotification *notifyCode);
	LRESULT OnCalledByNppWndProc(UINT Message, WPARAM wParam, LPARAM lParam);
};


