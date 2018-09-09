#pragma once
#include <cstdint>
#include <string>

#ifdef UNICODE
typedef std::wstring SSTRING;
#else
typedef std::string SSTRING;
#endif

namespace MpcHcRemote {
	int64_t GetCurrentPlayerTimestamp();
	bool SendCommand(int cmd);
	bool Seek(int64_t position);
	std::tuple<bool, SSTRING> GetInstallationPath();
};

