#pragma once
#include <cstdint>
#include <string>

#ifdef UNICODE
typedef std::wstring SSTRING;
#else
typedef std::string SSTRING;
#endif

namespace MpcHcRemote {
	enum class MpcHcCommand : int {
		PLAY_PAUSE = 889
	};

	int64_t GetCurrentPlayerTimestamp();
	bool SendCommand(MpcHcCommand cmd);
	bool Seek(int64_t position);
	std::tuple<bool, SSTRING> GetInstallationPath();
};

