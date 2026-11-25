#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <vector>

// 两个平台分支共用的事件定义
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
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows：WinHTTP WebSocket
// ---------------------------------------------------------------------------
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace simplews {
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

#else
// ---------------------------------------------------------------------------
// Linux / POSIX：自实现 WebSocket 客户端（RFC 6455）
//
// 为什么手写而不是引第三方库：OneBot 服务端（NapCat）跑在本机 127.0.0.1，
// 只需要明文 ws:// + 单路文本消息，手写约 300 行即可，零额外依赖，
// 比拉一个 websocketpp/boost 进来更适合 1.7G 内存的小机器。
//
// 支持：文本/二进制帧、分片重组、ping/pong 心跳、close 握手、断线重连。
// 限制：不支持 wss://（需要 TLS 层）。若要连公网 OneBot，请在前面套
//       stunnel / nginx 做 TLS，或改用 ws:// + 内网。
// ---------------------------------------------------------------------------
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <random>

namespace simplews {

namespace wsdetail {

// ---- base64：只用于握手时的 Sec-WebSocket-Key ----
inline std::string base64_encode(const unsigned char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += tbl[v & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

inline std::string make_ws_key() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    unsigned char raw[16];
    for (auto& b : raw) b = static_cast<unsigned char>(dist(rng));
    return base64_encode(raw, sizeof(raw));
}

// 读满 n 字节；返回 false 表示连接断开或出错
inline bool read_exact(int fd, void* buf, size_t n) {
    auto* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd, p + got, n - got, 0);
        if (r == 0) return false;                 // 对端正常关闭
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

// 写满 n 字节；MSG_NOSIGNAL 防止对端断开时进程被 SIGPIPE 打死
inline bool write_all(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = ::send(fd, p + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

// 带超时的 TCP 连接（非阻塞 connect + poll），避免 DNS/网络异常时卡死
inline int connect_timeout(int sockfd, const struct sockaddr* addr,
                           socklen_t addrlen, int timeout_ms) {
    int flags = ::fcntl(sockfd, F_GETFL, 0);
    ::fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(sockfd, addr, addrlen);
    if (rc == 0) {
        ::fcntl(sockfd, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) return -1;

    struct pollfd pfd{sockfd, POLLOUT, 0};
    int pr = ::poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return -1;

    int err = 0;
    socklen_t elen = sizeof(err);
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) return -1;
    if (err != 0) return -1;

    ::fcntl(sockfd, F_SETFL, flags);   // 恢复阻塞模式，后续读写更简单
    return 0;
}

}  // namespace wsdetail

class WebSocketClient {
public:
    WebSocketClient() : is_running_(false), fd_(-1) {}

    ~WebSocketClient() { stop(); }

    void set_callback(EventCallback cb) { callback_ = cb; }

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

        // 只 shutdown 不 close：close 由工作线程负责，避免 fd 双重释放。
        // shutdown 能立即唤醒阻塞在 recv() 的工作线程。
        {
            std::lock_guard<std::mutex> lk(fd_mutex_);
            if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
        }

        if (worker_thread_.joinable()) worker_thread_.join();
    }

    bool send_text(const std::string& text) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        int fd;
        {
            std::lock_guard<std::mutex> lk(fd_mutex_);
            fd = fd_;
        }
        if (fd < 0 || !is_running_) return false;
        return send_frame(fd, 0x1, text);
    }

private:
    std::string url_;
    std::atomic<bool> is_running_;
    EventCallback callback_;
    std::thread worker_thread_;

    std::mutex send_mutex_;     // 串行化写操作（帧不能交错）
    std::mutex fd_mutex_;       // 保护 fd_ 的关闭
    std::atomic<int> fd_;

    std::mt19937 mask_rng_{std::random_device{}()};

    // 发一帧（调用方需持有 send_mutex_）
    bool send_frame(int fd, uint8_t opcode, const std::string& payload) {
        std::string frame;
        frame.reserve(payload.size() + 14);
        frame.push_back(static_cast<char>(0x80 | opcode));   // FIN=1

        const size_t len = payload.size();
        if (len < 126) {
            frame.push_back(static_cast<char>(0x80 | len));  // MASK=1
        } else if (len <= 0xFFFF) {
            frame.push_back(static_cast<char>(0x80 | 126));
            frame.push_back(static_cast<char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<char>(len & 0xFF));
        } else {
            frame.push_back(static_cast<char>(0x80 | 127));
            for (int i = 7; i >= 0; --i) {
                frame.push_back(static_cast<char>(
                    (static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
            }
        }

        // 客户端发出的帧必须加掩码
        unsigned char mask[4];
        for (auto& b : mask) b = static_cast<unsigned char>(mask_rng_() & 0xFF);
        frame.append(reinterpret_cast<const char*>(mask), 4);

        for (size_t i = 0; i < len; ++i) {
            frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
        }

        return wsdetail::write_all(fd, frame.data(), frame.size());
    }

    // 发控制帧也走 send_mutex_，防止与业务消息交错
    bool send_control(int fd, uint8_t opcode, const std::string& payload) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        return send_frame(fd, opcode, payload);
    }

    void run_loop() {
        while (is_running_) {
            int fd = do_connect();
            if (fd >= 0) {
                {
                    std::lock_guard<std::mutex> lk(fd_mutex_);
                    fd_ = fd;
                }

                if (callback_ && is_running_) callback_({ EventType::Open, "", "" });
                receive_loop(fd);
                if (callback_ && is_running_) callback_({ EventType::Close, "", "" });

                {
                    std::lock_guard<std::mutex> lk(fd_mutex_);
                    if (fd_ == fd) fd_ = -1;
                }
                ::close(fd);   // 工作线程独占关闭
            } else {
                if (callback_ && is_running_) {
                    callback_({ EventType::Error, "",
                                "无法连接到 OneBot 服务端 (ws_url: " + url_ + ")" });
                }
            }

            if (!is_running_) break;

            // 与 Windows 版一致：重连前等 2 秒（细粒度可中断）
            for (int i = 0; i < 20 && is_running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    int do_connect() {
        std::string u = url_;
        bool secure = false;
        if (u.rfind("ws://", 0) == 0) {
            u = u.substr(5);
        } else if (u.rfind("wss://", 0) == 0) {
            u = u.substr(6);
            secure = true;
        } else {
            return -1;
        }

        if (secure) {
            if (callback_) {
                callback_({ EventType::Error, "",
                            "Linux 版不支持 wss://（无 TLS 层），请改用 ws:// 或在前端做 TLS 卸载" });
            }
            return -1;
        }

        std::string hostport = u;
        std::string path = "/";
        auto slash = u.find('/');
        if (slash != std::string::npos) {
            hostport = u.substr(0, slash);
            path = u.substr(slash);   // 保留 ?access_token=... 查询串
        }

        std::string host;
        std::string port = "80";
        auto colon = hostport.rfind(':');
        if (colon != std::string::npos) {
            host = hostport.substr(0, colon);
            port = hostport.substr(colon + 1);
        } else {
            host = hostport;
        }
        if (host.empty()) return -1;

        // DNS 解析
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
            return -1;
        }

        int fd = -1;
        for (struct addrinfo* p = res; p; p = p->ai_next) {
            int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s < 0) continue;
            if (wsdetail::connect_timeout(s, p->ai_addr,
                                          static_cast<socklen_t>(p->ai_addrlen), 5000) == 0) {
                fd = s;
                break;
            }
            ::close(s);
        }
        ::freeaddrinfo(res);
        if (fd < 0) return -1;

        // 关掉 Nagle，降低消息延迟
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        // ---- HTTP Upgrade 握手 ----
        const std::string key = wsdetail::make_ws_key();
        const std::string req =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + hostport + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "User-Agent: TabMind-WS/1.0\r\n"
            "\r\n";

        if (!wsdetail::write_all(fd, req.data(), req.size())) {
            ::close(fd);
            return -1;
        }

        // 逐字节读响应头（只在握手阶段，代价可忽略）
        std::string resp;
        char c;
        while (resp.size() < 8192) {
            if (!wsdetail::read_exact(fd, &c, 1)) break;
            resp += c;
            if (resp.size() >= 4 &&
                resp.compare(resp.size() - 4, 4, "\r\n\r\n") == 0) {
                break;
            }
        }

        // 期望 101 Switching Protocols
        if (resp.find(" 101") == std::string::npos) {
            ::close(fd);
            return -1;
        }

        return fd;
    }

    void receive_loop(int fd) {
        std::string full_msg;   // 分片重组缓冲

        while (is_running_) {
            unsigned char hdr[2];
            if (!wsdetail::read_exact(fd, hdr, 2)) break;

            const bool fin      = (hdr[0] & 0x80) != 0;
            const uint8_t opcode = hdr[0] & 0x0F;
            const bool masked   = (hdr[1] & 0x80) != 0;
            uint64_t len        = hdr[1] & 0x7F;

            if (len == 126) {
                unsigned char ext[2];
                if (!wsdetail::read_exact(fd, ext, 2)) break;
                len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
            } else if (len == 127) {
                unsigned char ext[8];
                if (!wsdetail::read_exact(fd, ext, 8)) break;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
            }

            // 防御异常帧：单帧超过 32MB 直接断开
            if (len > 32ull * 1024 * 1024) break;

            unsigned char mask[4] = {0, 0, 0, 0};
            if (masked && !wsdetail::read_exact(fd, mask, 4)) break;

            std::string payload;
            if (len > 0) {
                payload.resize(static_cast<size_t>(len));
                if (!wsdetail::read_exact(fd, &payload[0], payload.size())) break;
            }
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i) {
                    payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
                }
            }

            if (opcode == 0x8) {            // close
                send_control(fd, 0x8, "");
                break;
            }
            if (opcode == 0x9) {            // ping → pong
                send_control(fd, 0xA, payload);
                continue;
            }
            if (opcode == 0xA) continue;    // pong，忽略

            if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0) {
                full_msg += payload;
                if (fin) {
                    if (callback_ && is_running_) {
                        callback_({ EventType::Message, full_msg, "" });
                    }
                    full_msg.clear();
                }
            }
        }
    }
};

}  // namespace simplews

#endif  // _WIN32
