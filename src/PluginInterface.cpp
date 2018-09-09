//this file is part of notepad++
//Copyright (C)2003 Don HO <donho@altern.org>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <memory>
#include "PluginInterface.h"
#include "NppSmi.h"

HINSTANCE g_hModule;

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD  reasonForCall, LPVOID /*lpReserved*/) {
	switch (reasonForCall) {
	case DLL_PROCESS_ATTACH:
		g_hModule = hModule;
		break;

	case DLL_PROCESS_DETACH:
		NppSmi::INSTANCE = nullptr;
		break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;
	}

	return TRUE;
}


extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
	NppSmi::INSTANCE = std::make_shared<NppSmi>(g_hModule, notepadPlusData);
}

extern "C" __declspec(dllexport) const TCHAR * getName() {
	return NppSmi::PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem const * getFuncsArray(int *nbF) {
	*nbF = static_cast<int>(NppSmi::INSTANCE->FUNCTIONS.size());
	return &NppSmi::INSTANCE->FUNCTIONS[0];
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *notifyCode) {
	NppSmi::INSTANCE->OnScintillaMessage(notifyCode);
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam) {
	return NppSmi::INSTANCE->OnCalledByNppWndProc(Message, wParam, lParam);
}

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() {
	return TRUE;
}
#endif //UNICODE
