#include "MpcHcRemote.h"
#include <regex>
#include <WinSock2.h>
#include <Windows.h>
#include <tuple>
#include <cinttypes>

namespace MpcHcRemote {
	std::regex positionMatcher("<p id=\"position\">([0-9]+)</p>");

	std::pair<bool, std::string> GetRequest(const std::string &request) {
		struct sockaddr_in localhost;
		memset(&localhost, 0, sizeof(struct sockaddr_in));
		std::string s;
		char buf[65536];
		DWORD port = 0, len = 4;

		if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC\\Settings"), TEXT("WebServerPort"), 0x00010000 | RRF_RT_REG_DWORD, NULL, &port, &len) || port == 0) {
			return std::make_pair(false, "");
		}

		localhost.sin_family = AF_INET;
		localhost.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		localhost.sin_port = htons(static_cast<u_short>(port));

		SOCKET connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (connectSocket == INVALID_SOCKET) {
			return std::make_pair(false, "");
		}
		if (connect(connectSocket, reinterpret_cast<struct sockaddr*>(&localhost), sizeof localhost) == SOCKET_ERROR) {
			closesocket(connectSocket);
			connectSocket = INVALID_SOCKET;
		}
		if (connectSocket == INVALID_SOCKET)
			return std::make_pair(false, "");
		if (SOCKET_ERROR == send(connectSocket, request.c_str(), static_cast<int>(request.size()), 0)) {
			closesocket(connectSocket);
			return std::make_pair(false, "");
		}
		if (shutdown(connectSocket, SD_SEND) == SOCKET_ERROR) {
			closesocket(connectSocket);
			return std::make_pair(false, "");
		}


		// Receive data until the server closes the connection
		do {
			const auto readBytes = recv(connectSocket, buf, sizeof buf, 0);
			if (readBytes <= 0)
				break;
			s.append(buf, readBytes);
		} while (true);
		closesocket(connectSocket);
		return std::make_pair(true, s);
	}

	int64_t GetCurrentPlayerTimestamp() {
		bool result;
		std::string response;
		std::tie(result, response) = GetRequest("GET /variables.html HTTP/1.1\r\n\r\n");
		if (!result)
			return -1;

		std::smatch m;
		int64_t pos = -1;
		if (std::regex_search(response, m, positionMatcher))
			pos = strtoll(m[1].str().c_str(), nullptr, 10);
		return pos;
	}

	bool SendCommand(int cmd) {
		char req[65536], req2[1024];
		snprintf(req2, sizeof req2, "wm_command=%d", cmd);
		snprintf(req, sizeof req, "POST /command.html HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n%s", strlen(req2), req2);

		bool result;
		std::string response;
		std::tie(result, response) = GetRequest(req);
		return result;
	}

	bool Seek(int64_t position) {
		char req[65536], req2[1024];
		snprintf(req2, sizeof(req2), "wm_command=-1&position=%" PRId64 ":%" PRId64 ":%" PRId64 ":%" PRId64 "", position / 3600000, position / 60000 % 60, position / 1000 % 60, position % 1000);
		snprintf(req, sizeof(req), "POST /command.html HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n%s", strlen(req2), req2);

		bool result;
		std::string response;
		std::tie(result, response) = GetRequest(req);
		return result;
	}

	std::tuple<bool, SSTRING> GetInstallationPath() {
		TCHAR szMpcHcPath[MAX_PATH];
		DWORD len = MAX_PATH;
		DWORD res = 0;

		if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC"), TEXT("ExePath"), 0x00010000 | RRF_RT_REG_SZ, nullptr, &szMpcHcPath, &len)) {
			return std::make_tuple(false, TEXT("MPC-HC not found."));
		}
		if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC\\Settings"), TEXT("EnableWebServer"), 0x00010000 | RRF_RT_REG_DWORD, nullptr, &res, &len) || res == 0) {
			return std::make_tuple(false, TEXT("Web Server feature of MPC-HC is inactive."));
		}
		return std::make_tuple(true, szMpcHcPath);
	}

}
