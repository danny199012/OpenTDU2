#pragma once

// WinHTTP: the launcher's updater talks to the GitHub REST API and downloads a
// release asset. Nothing else in the launcher uses the network.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace Launcher {

struct HttpResponse {
    bool bOk = false;
    DWORD Status = 0;
    std::string Body;
    std::string Error;
};

HttpResponse HttpGet( const std::string &url, const std::string &userAgent,
                      uint32_t timeoutMs = 30000 );

// Streams 'url' into 'destFile'. progress(done, total) returns false to cancel
// (total is 0 when the server sends no Content-Length). The partial file is
// removed on failure or cancel.
bool HttpDownloadToFile( const std::string &url, const std::wstring &destFile,
                         const std::string &userAgent,
                         const std::function<bool( uint64_t, uint64_t )> &progress,
                         std::string &outError );

} // namespace Launcher
