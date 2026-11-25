#pragma once
#include <string>
#include <iostream>
#include <vector>

namespace simplehttp {
    // 两个平台分支共用的返回结构
    struct Response {
        int status_code = 0;
        std::string body;
    };
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows：WinHTTP
// ---------------------------------------------------------------------------
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace simplehttp {
    // 带鉴权头的 GET（拉取 /models 这类接口要用）
    inline Response get_auth(const std::string& url_str, const std::string& api_key,
                             const std::string& auth_style = "both") {
        Response resp;
        std::wstring full_url;
        int len = MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, NULL, 0);
        if (len > 0) {
            full_url.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, &full_url[0], len);
        }

        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(full_url.c_str(), (DWORD)full_url.length(), 0, &urlComp)) return resp;

        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.dwExtraInfoLength > 0) {
            urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }

        HINTERNET hSession = WinHttpOpen(L"TabMind/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return resp;
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        WinHttpSetTimeouts(hSession, 30000, 30000, 60000, 60000);

        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return resp; }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return resp; }

        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        std::wstring headers;
        if (!api_key.empty()) {
            std::wstring w_key;
            int klen = MultiByteToWideChar(CP_UTF8, 0, api_key.c_str(), -1, NULL, 0);
            if (klen > 0) {
                w_key.resize(klen - 1);
                MultiByteToWideChar(CP_UTF8, 0, api_key.c_str(), -1, &w_key[0], klen);
            }
            if (auth_style == "api-key" || auth_style == "both") {
                headers += L"api-key: " + w_key + L"\r\n";
            }
            if (auth_style == "bearer" || auth_style == "both" || auth_style.empty()) {
                headers += L"Authorization: Bearer " + w_key + L"\r\n";
            }
        }

        BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);
        if (ok) {
            DWORD statusCode = 0, statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
            resp.status_code = static_cast<int>(statusCode);
            DWORD size = 0;
            std::vector<char> buffer;
            while (WinHttpQueryDataAvailable(hRequest, &size) && size > 0) {
                std::vector<char> temp(size);
                DWORD downloaded = 0;
                if (WinHttpReadData(hRequest, temp.data(), size, &downloaded) && downloaded > 0) {
                    buffer.insert(buffer.end(), temp.begin(), temp.begin() + downloaded);
                }
            }
            resp.body.assign(buffer.begin(), buffer.end());
        }
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    inline Response get(const std::string& url_str) {
        Response resp;
        std::wstring full_url;
        int len = MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, NULL, 0);
        if (len > 0) {
            full_url.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, &full_url[0], len);
        }

        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(full_url.c_str(), (DWORD)full_url.length(), 0, &urlComp)) {
            return resp;
        }

        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.dwExtraInfoLength > 0) {
            urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }

        HINTERNET hSession = WinHttpOpen(L"TabMind/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return resp;

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return resp; }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return resp;
        }

        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
                resp.status_code = static_cast<int>(statusCode);

                DWORD size = 0;
                std::vector<char> buffer;
                while (WinHttpQueryDataAvailable(hRequest, &size) && size > 0) {
                    std::vector<char> temp(size);
                    DWORD downloaded = 0;
                    if (WinHttpReadData(hRequest, temp.data(), size, &downloaded) && downloaded > 0) {
                        buffer.insert(buffer.end(), temp.begin(), temp.begin() + downloaded);
                    }
                }
                resp.body.assign(buffer.begin(), buffer.end());
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    inline Response post_json(const std::string& base_url, const std::string& api_key, const std::string& json_payload, const std::string& auth_style = "both") {
        Response resp;

        // 解析 URL
        std::wstring full_url;
        int len = MultiByteToWideChar(CP_UTF8, 0, base_url.c_str(), -1, NULL, 0);
        if (len > 0) {
            full_url.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, base_url.c_str(), -1, &full_url[0], len);
        }

        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(full_url.c_str(), (DWORD)full_url.length(), 0, &urlComp)) {
            std::cerr << "[WinHTTP] 无法解析 URL: " << base_url << std::endl;
            return resp;
        }

        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

        // 如果路径没有 /chat/completions 则拼接
        if (urlPath.find(L"/chat/completions") == std::wstring::npos) {
            if (!urlPath.empty() && urlPath.back() == L'/') {
                urlPath.pop_back();
            }
            urlPath += L"/chat/completions";
        }

        HINTERNET hSession = WinHttpOpen(L"TabMind/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return resp;

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        // 识图请求体可能较大，放宽超时
        WinHttpSetTimeouts(hSession, 60000, 60000, 120000, 120000);

        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return resp;
        }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return resp;
        }

        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!api_key.empty()) {
            std::wstring w_key;
            int klen = MultiByteToWideChar(CP_UTF8, 0, api_key.c_str(), -1, NULL, 0);
            if (klen > 0) {
                w_key.resize(klen - 1);
                MultiByteToWideChar(CP_UTF8, 0, api_key.c_str(), -1, &w_key[0], klen);
            }
            // 兼容 OpenAI(Bearer) 与小米 MiMo(api-key) 两种鉴权头
            if (auth_style == "api-key" || auth_style == "both") {
                headers += L"api-key: " + w_key + L"\r\n";
            }
            if (auth_style == "bearer" || auth_style == "both" || auth_style.empty()) {
                headers += L"Authorization: Bearer " + w_key + L"\r\n";
            }
        }

        BOOL bResults = WinHttpSendRequest(
            hRequest,
            headers.c_str(),
            (DWORD)headers.length(),
            (LPVOID)json_payload.c_str(),
            (DWORD)json_payload.length(),
            (DWORD)json_payload.length(),
            0
        );

        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }

        if (bResults) {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
            resp.status_code = static_cast<int>(statusCode);

            DWORD size = 0;
            std::vector<char> buffer;
            while (WinHttpQueryDataAvailable(hRequest, &size) && size > 0) {
                std::vector<char> temp(size);
                DWORD downloaded = 0;
                if (WinHttpReadData(hRequest, temp.data(), size, &downloaded) && downloaded > 0) {
                    buffer.insert(buffer.end(), temp.begin(), temp.begin() + downloaded);
                }
            }
            resp.body.assign(buffer.begin(), buffer.end());
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }
}

#else
// ---------------------------------------------------------------------------
// Linux / POSIX：libcurl
//
// 接口与 Windows 分支完全一致，调用方无需任何改动。
// libcurl 在 Linux 上线程安全（配合 CURLOPT_NOSIGNAL），可直接多线程并发调用。
// ---------------------------------------------------------------------------
#include <curl/curl.h>
#include <mutex>

namespace simplehttp {

namespace detail {

// libcurl 全局初始化，进程内只跑一次
inline void ensure_curl_global() {
    static std::once_flag flag;
    std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

inline size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// 保证 URL 指向 /chat/completions（与 Windows 分支的路径拼接规则一致）
inline std::string ensure_chat_path(const std::string& base_url) {
    std::string path = base_url;
    std::string query;

    auto qpos = path.find('?');
    if (qpos != std::string::npos) {
        query = path.substr(qpos);
        path = path.substr(0, qpos);
    }

    if (path.find("/chat/completions") == std::string::npos) {
        while (!path.empty() && path.back() == '/') path.pop_back();
        path += "/chat/completions";
    }
    return path + query;
}

// 公共请求执行体：负责鉴权头、超时、TLS 策略
inline Response perform(const std::string& url,
                        const std::string& method,
                        const std::string& api_key,
                        const std::string& auth_style,
                        const std::string& body,
                        long connect_timeout,
                        long total_timeout) {
    Response resp;
    ensure_curl_global();

    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    struct curl_slist* headers = nullptr;
    if (!body.empty()) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    if (!api_key.empty()) {
        // 兼容 OpenAI(Bearer) 与小米 MiMo(api-key) 两种鉴权头
        if (auth_style == "api-key" || auth_style == "both") {
            headers = curl_slist_append(headers, ("api-key: " + api_key).c_str());
        }
        if (auth_style == "bearer" || auth_style == "both" || auth_style.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        // 多线程下必须
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // 自动解压 gzip/deflate
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TabMind/1.0");

    // 与 Windows 分支保持一致：不校验证书链。
    // 部分中转 API 用自签/异常证书，严格校验会直接失败。
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status_code = static_cast<int>(code);
    } else {
        std::cerr << "[libcurl] 请求失败: " << curl_easy_strerror(rc)
                  << " (url: " << url << ")" << std::endl;
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

}  // namespace detail

// 带鉴权头的 GET（拉取 /models 这类接口要用）
inline Response get_auth(const std::string& url_str, const std::string& api_key,
                         const std::string& auth_style = "both") {
    return detail::perform(url_str, "GET", api_key, auth_style, "", 30L, 120L);
}

inline Response get(const std::string& url_str) {
    return detail::perform(url_str, "GET", "", "", "", 15L, 60L);
}

inline Response post_json(const std::string& base_url, const std::string& api_key,
                          const std::string& json_payload,
                          const std::string& auth_style = "both") {
    const std::string url = detail::ensure_chat_path(base_url);
    // 识图请求体可能较大，放宽超时（对应 Windows 分支的 120s）
    return detail::perform(url, "POST", api_key, auth_style, json_payload, 60L, 180L);
}

}  // namespace simplehttp

#endif  // _WIN32

