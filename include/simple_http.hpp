#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace simplehttp {
    struct Response {
        int status_code = 0;
        std::string body;
    };

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
