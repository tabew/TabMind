#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace simplews {
    enum class EventType {
        Open,
        Message,
        Close,
        Error
    };

    struct Event {
        EventType type;
        std::string message;
        std::string error;
    };

    using EventCallback = std::function<void(const Event&)>;

    class WebSocketClient {
    public:
        WebSocketClient() 
            : is_running_(false), h_session_(NULL), h_connect_(NULL), h_request_(NULL), h_websocket_(NULL) {}

        ~WebSocketClient() {
            stop();
        }

        void set_callback(EventCallback cb) {
            callback_ = cb;
        }

        bool connect(const std::string& ws_url) {
            stop();
            url_ = ws_url;
            is_running_ = true;
            worker_thread_ = std::thread(&WebSocketClient::run_loop, this);
            return true;
        }

        void stop() {
            if (!is_running_ && !worker_thread_.joinable()) return;
            is_running_ = false;

            // 关键：直接关闭 WinHTTP 句柄，立即唤醒并中断阻塞在 WinHttpWebSocketReceive 中的后台线程
            // 严禁调用同步阻塞的 WinHttpWebSocketClose，否则在多线程状态下会引发死锁/卡死
            close_handles_fast();

            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
        }

        bool send_text(const std::string& text) {
            // WinHttpWebSocketSend 与接收线程并发不安全，必须串行化
            std::lock_guard<std::mutex> lock(send_mutex_);
            HINTERNET ws = h_websocket_;
            if (!ws || !is_running_) return false;
            DWORD err = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                (PVOID)text.c_str(), (DWORD)text.length());
            return (err == ERROR_SUCCESS);
        }

    private:
        std::string url_;
        std::atomic<bool> is_running_;
        EventCallback callback_;
        std::thread worker_thread_;
        std::mutex send_mutex_;

        std::atomic<HINTERNET> h_session_;
        std::atomic<HINTERNET> h_connect_;
        std::atomic<HINTERNET> h_request_;
        std::atomic<HINTERNET> h_websocket_;

        void close_handles_fast() {
            HINTERNET ws = h_websocket_.exchange(NULL);
            if (ws) {
                // 优雅非阻塞关闭并释放句柄
                WinHttpWebSocketShutdown(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
                WinHttpCloseHandle(ws);
            }

            HINTERNET req = h_request_.exchange(NULL);
            if (req) WinHttpCloseHandle(req);

            HINTERNET conn = h_connect_.exchange(NULL);
            if (conn) WinHttpCloseHandle(conn);

            HINTERNET sess = h_session_.exchange(NULL);
            if (sess) WinHttpCloseHandle(sess);
        }

        void run_loop() {
            while (is_running_) {
                if (do_connect()) {
                    if (callback_ && is_running_) callback_({ EventType::Open, "", "" });
                    receive_loop();
                    if (callback_ && is_running_) callback_({ EventType::Close, "", "" });
                } else {
                    if (callback_ && is_running_) {
                        callback_({ EventType::Error, "", "无法连接到 OneBot 服务端 (ws_url: " + url_ + ")" });
                    }
                }

                close_handles_fast();
                if (!is_running_) break;

                // 细粒度可中断休眠，防止停止时被 sleep 阻塞 2 秒
                for (int i = 0; i < 20 && is_running_; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

        bool do_connect() {
            std::string temp_url = url_;
            bool is_secure = false;
            if (temp_url.find("ws://") == 0) {
                temp_url = "http://" + temp_url.substr(5);
            } else if (temp_url.find("wss://") == 0) {
                temp_url = "https://" + temp_url.substr(6);
                is_secure = true;
            }

            std::wstring w_url;
            int len = MultiByteToWideChar(CP_UTF8, 0, temp_url.c_str(), -1, NULL, 0);
            if (len > 0) {
                w_url.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, temp_url.c_str(), -1, &w_url[0], len);
            }

            URL_COMPONENTS urlComp = { 0 };
            urlComp.dwStructSize = sizeof(urlComp);
            urlComp.dwSchemeLength = (DWORD)-1;
            urlComp.dwHostNameLength = (DWORD)-1;
            urlComp.dwUrlPathLength = (DWORD)-1;
            urlComp.dwExtraInfoLength = (DWORD)-1;

            if (!WinHttpCrackUrl(w_url.c_str(), (DWORD)w_url.length(), 0, &urlComp)) {
                return false;
            }

            std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
            std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            if (urlComp.dwExtraInfoLength > 0) {
                urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
            }

            if (!is_running_) return false;

            HINTERNET sess = WinHttpOpen(L"TabMind-WS/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!sess) return false;
            h_session_ = sess;

            HINTERNET conn = WinHttpConnect(sess, hostName.c_str(), urlComp.nPort, 0);
            if (!conn) return false;
            h_connect_ = conn;

            DWORD flags = is_secure ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET req = WinHttpOpenRequest(conn, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!req) return false;
            h_request_ = req;

            if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
                return false;
            }

            if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                return false;
            }

            if (!WinHttpReceiveResponse(req, NULL)) {
                return false;
            }

            HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, NULL);
            if (!ws) return false;
            h_websocket_ = ws;

            WinHttpCloseHandle(req);
            h_request_ = NULL;
            return true;
        }

        void receive_loop() {
            std::vector<BYTE> buffer(65536);
            std::string full_msg;

            while (is_running_) {
                HINTERNET ws = h_websocket_;
                if (!ws) break;

                DWORD bytesRead = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;
                DWORD err = WinHttpWebSocketReceive(ws, buffer.data(), (DWORD)buffer.size(), &bytesRead, &bufferType);

                if (err != ERROR_SUCCESS || !is_running_) {
                    break;
                }

                if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                    break;
                }

                if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                    full_msg.append((char*)buffer.data(), bytesRead);
                    if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                        if (callback_ && is_running_) {
                            callback_({ EventType::Message, full_msg, "" });
                        }
                        full_msg.clear();
                    }
                }
            }
        }
    };
}
