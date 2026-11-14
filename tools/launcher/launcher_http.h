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
    // Response headers the update checks care about (GitHub rate limiting / conditional requests).
    std::string ETag;
    int RateLimitRemaining = -1;     // X-RateLimit-Remaining, -1 when absent
    long long RateLimitReset = 0;    // X-RateLimit-Reset, epoch seconds, 0 when absent
    long long RetryAfterSeconds = 0; // Retry-After, 0 when absent
    bool bRateLimited = false;       // set by the updater's GitHub wrapper (a 403 / 429 rate-limit answer, or a blocked call)
    bool bFromCache = false;         // a 304 answered from the on-disk ETag cache (free: not counted by GitHub)
};

// ifNoneMatch: sent as If-None-Match; a 304 answer (Status 304, empty Body, bOk false) means "unchanged",
// and GitHub does not count those against the 60 requests / hour limit of unauthenticated calls.
HttpResponse HttpGet( const std::string &url, const std::string &userAgent,
                      uint32_t timeoutMs = 30000, const std::string &ifNoneMatch = std::string() );

// Streams 'url' into 'destFile'. progress(done, total) returns false to cancel
// (total is 0 when the server sends no Content-Length). The partial file is
// removed on failure or cancel.
bool HttpDownloadToFile( const std::string &url, const std::wstring &destFile,
                         const std::string &userAgent,
                         const std::function<bool( uint64_t, uint64_t )> &progress,
                         std::string &outError );

} // namespace Launcher
