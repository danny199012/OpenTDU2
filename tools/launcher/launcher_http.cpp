#include "launcher_http.h"

#include "launcher_util.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>

namespace Launcher {

namespace {

struct UrlParts {
    std::wstring Host;
    std::wstring Path;
    INTERNET_PORT Port = 0;
    bool bSecure = false;
};

bool CrackUrl( const std::string &url, UrlParts &outParts, std::string &outError ) {
    const std::wstring wide = Utf8ToWide( url );
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof( components );
    components.dwHostNameLength = static_cast<DWORD>( -1 );
    components.dwUrlPathLength = static_cast<DWORD>( -1 );
    components.dwExtraInfoLength = static_cast<DWORD>( -1 );
    if ( !WinHttpCrackUrl( wide.c_str(), static_cast<DWORD>( wide.size() ), 0, &components ) ) {
        outError = "Invalid URL: " + url;
        return false;
    }
    outParts.Host.assign( components.lpszHostName, components.dwHostNameLength );
    outParts.Path.assign( components.lpszUrlPath, components.dwUrlPathLength );
    if ( components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0 ) {
        outParts.Path.append( components.lpszExtraInfo, components.dwExtraInfoLength );
    }
    outParts.Port = components.nPort;
    outParts.bSecure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if ( outParts.Path.empty() ) {
        outParts.Path = L"/";
    }
    return true;
}

class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle( HINTERNET handle ) : mHandle( handle ) {}
    ~InternetHandle() { Reset(); }
    InternetHandle( const InternetHandle & ) = delete;
    InternetHandle &operator=( const InternetHandle & ) = delete;
    InternetHandle &operator=( InternetHandle &&other ) noexcept {
        if ( this != &other ) {
            Reset();
            mHandle = other.mHandle;
            other.mHandle = nullptr;
        }
        return *this;
    }
    HINTERNET Get() const { return mHandle; }
    explicit operator bool() const { return mHandle != nullptr; }
    void Reset() {
        if ( mHandle != nullptr ) {
            WinHttpCloseHandle( mHandle );
            mHandle = nullptr;
        }
    }

private:
    HINTERNET mHandle = nullptr;
};

bool OpenRequest( const std::string &url, const std::string &userAgent, uint32_t timeoutMs,
                  InternetHandle &outSession, InternetHandle &outConnection,
                  InternetHandle &outRequest, std::string &outError ) {
    UrlParts parts;
    if ( !CrackUrl( url, parts, outError ) ) {
        return false;
    }
    outSession = InternetHandle( WinHttpOpen(
        Utf8ToWide( userAgent ).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
    if ( !outSession ) {
        outError = "WinHttpOpen failed";
        return false;
    }
    const int timeout = static_cast<int>( timeoutMs );
    WinHttpSetTimeouts( outSession.Get(), timeout, timeout, timeout, timeout );
    outConnection = InternetHandle(
        WinHttpConnect( outSession.Get(), parts.Host.c_str(), parts.Port, 0 ) );
    if ( !outConnection ) {
        outError = "WinHttpConnect failed";
        return false;
    }
    outRequest = InternetHandle( WinHttpOpenRequest(
        outConnection.Get(), L"GET", parts.Path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.bSecure ? WINHTTP_FLAG_SECURE : 0 ) );
    if ( !outRequest ) {
        outError = "WinHttpOpenRequest failed";
        return false;
    }
    // GitHub's API rejects a request without a User-Agent; WinHttpOpen's agent
    // is sent, and the JSON accept header keeps the answer stable.
    const wchar_t *headers = L"Accept: application/vnd.github+json\r\n"
                             L"X-GitHub-Api-Version: 2022-11-28\r\n";
    WinHttpAddRequestHeaders( outRequest.Get(), headers, static_cast<DWORD>( -1 ),
                              WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE );
    return true;
}

} // namespace

HttpResponse HttpGet( const std::string &url, const std::string &userAgent,
                      uint32_t timeoutMs ) {
    HttpResponse response;
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    if ( !OpenRequest( url, userAgent, timeoutMs, session, connection, request,
                       response.Error ) ) {
        return response;
    }
    if ( !WinHttpSendRequest( request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ||
         !WinHttpReceiveResponse( request.Get(), nullptr ) ) {
        response.Error = "Request failed (" + std::to_string( GetLastError() ) + ")";
        return response;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof( status );
    WinHttpQueryHeaders( request.Get(),
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                         WINHTTP_NO_HEADER_INDEX );
    response.Status = status;

    for ( ;; ) {
        DWORD available = 0;
        if ( !WinHttpQueryDataAvailable( request.Get(), &available ) ) {
            response.Error = "Read failed (" + std::to_string( GetLastError() ) + ")";
            return response;
        }
        if ( available == 0 ) {
            break;
        }
        const DWORD toRead = available > 65536 ? 65536 : available;
        std::string chunk( toRead, '\0' );
        DWORD read = 0;
        if ( !WinHttpReadData( request.Get(), chunk.data(), toRead, &read ) ) {
            response.Error = "Read failed (" + std::to_string( GetLastError() ) + ")";
            return response;
        }
        response.Body.append( chunk.data(), read );
        if ( response.Body.size() > 8u * 1024u * 1024u ) {
            response.Error = "Response is too large";
            return response;
        }
    }
    response.bOk = status >= 200 && status < 300;
    if ( !response.bOk && response.Error.empty() ) {
        response.Error = "HTTP status " + std::to_string( status );
    }
    return response;
}

bool HttpDownloadToFile( const std::string &url, const std::wstring &destFile,
                         const std::string &userAgent,
                         const std::function<bool( uint64_t, uint64_t )> &progress,
                         std::string &outError ) {
    outError.clear();
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    if ( !OpenRequest( url, userAgent, 60000, session, connection, request, outError ) ) {
        return false;
    }
    if ( !WinHttpSendRequest( request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ||
         !WinHttpReceiveResponse( request.Get(), nullptr ) ) {
        outError = "Request failed (" + std::to_string( GetLastError() ) + ")";
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof( status );
    WinHttpQueryHeaders( request.Get(),
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                         WINHTTP_NO_HEADER_INDEX );
    if ( status < 200 || status >= 300 ) {
        outError = "HTTP status " + std::to_string( status );
        return false;
    }
    uint64_t total = 0;
    {
        DWORD length = 0;
        DWORD lengthSize = sizeof( length );
        if ( WinHttpQueryHeaders( request.Get(),
                                  WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                                  WINHTTP_NO_HEADER_INDEX ) ) {
            total = length;
        }
    }

    FILE *file = _wfopen( destFile.c_str(), L"wb" );
    if ( file == nullptr ) {
        outError = "Cannot write " + WideToUtf8( destFile );
        return false;
    }
    uint64_t done = 0;
    for ( ;; ) {
        DWORD available = 0;
        if ( !WinHttpQueryDataAvailable( request.Get(), &available ) ) {
            outError = "Read failed (" + std::to_string( GetLastError() ) + ")";
            break;
        }
        if ( available == 0 ) {
            break;
        }
        const DWORD toRead = available > 65536 ? 65536 : available;
        std::string chunk( toRead, '\0' );
        DWORD read = 0;
        if ( !WinHttpReadData( request.Get(), chunk.data(), toRead, &read ) ) {
            outError = "Read failed (" + std::to_string( GetLastError() ) + ")";
            break;
        }
        if ( read > 0 ) {
            fwrite( chunk.data(), 1, read, file );
            done += read;
            if ( progress && !progress( done, total ) ) {
                outError = "Cancelled";
                break;
            }
        }
    }
    fclose( file );
    if ( !outError.empty() ) {
        DeleteFileW( destFile.c_str() );
        return false;
    }
    return true;
}

} // namespace Launcher

