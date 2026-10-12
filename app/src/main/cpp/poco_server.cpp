
#include "poco_server.hpp"
#include "include/log.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

namespace XposedPoco {
namespace {

constexpr uint16_t kPort = 27183;
constexpr size_t kMaxCommandLength = 64;

enum class DumpState {
    Idle,
    Pending,
    Processing,
    Complete
};

std::atomic<bool> g_serverStarted{false};

std::mutex g_mutex;
std::condition_variable g_cv;
DumpState g_state = DumpState::Idle;
RequestType g_requestType = RequestType::Dump;
std::string g_result;

// 指定したバイト数をすべて送信する
bool SendAll(int fd, const void* data, size_t size) {
    const auto* p = static_cast<const char*>(data);

    while (size > 0) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return false;
        }

        p += n;
        size -= static_cast<size_t>(n);
    }

    return true;
}

// 4バイトのネットワークバイトオーダー長 + UTF-8本文
bool SendFrame(int fd, const std::string& body) {
    if (body.size() > UINT32_MAX) {
        return false;
    }

    const uint32_t length =
        htonl(static_cast<uint32_t>(body.size()));

    return SendAll(fd, &length, sizeof(length)) &&
           SendAll(fd, body.data(), body.size());
}

// 改行までコマンドを読む
bool ReadLine(int fd, std::string& line) {
    line.clear();

    while (line.size() < kMaxCommandLength) {
        char c = '\0';
        ssize_t n = recv(fd, &c, 1, 0);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return false;
        }

        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }

        line.push_back(c);
    }

    return false;
}

void HandleClient(int fd) {
    std::string command;

    while (ReadLine(fd, command)) {
        if (command == "PING") {
            if (!SendFrame(fd, "PONG")) {
                break;
            }
            continue;
        }

        if (command != "DUMP" && command != "CLICKABLES") {
            if (!SendFrame(fd, "ERROR: unknown command")) {
                break;
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            if (g_state != DumpState::Idle) {
                if (!SendFrame(fd, "ERROR: dump busy")) {
                    break;
                }
                continue;
            }

            g_result.clear();
            g_requestType = (command == "CLICKABLES")
                ? RequestType::Clickables
                : RequestType::Dump;
            g_state = DumpState::Pending;
        }

        LOGI("Received DUMP command");
        g_cv.notify_all();

        std::string result;

        {
            std::unique_lock<std::mutex> lock(g_mutex);

            const bool completed = g_cv.wait_for(
                lock,
                std::chrono::seconds(30),
                [] {
                    return g_state == DumpState::Complete;
                }
            );

            if (!completed) {
                g_state = DumpState::Idle;
                g_result.clear();
                result = "ERROR: dump timeout";
            } else {
                result = std::move(g_result);
                g_result.clear();
                g_state = DumpState::Idle;
            }
        }

        g_cv.notify_all();

        if (!SendFrame(fd, result)) {
            break;
        }
    }

    close(fd);
}

void ServerThread() {
    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);

    if (serverFd < 0) {
        LOGI("socket failed: %s", strerror(errno));
        return;
    }

    int reuse = 1;
    setsockopt(
        serverFd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            serverFd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        ) != 0) {
        LOGI("bind failed: %s", strerror(errno));
        close(serverFd);
        return;
    }

    if (listen(serverFd, 4) != 0) {
        LOGI("listen failed: %s", strerror(errno));
        close(serverFd);
        return;
    }

    LOGI("TCP command server listening on 127.0.0.1:%u",
              kPort);

    while (true) {
        const int clientFd = accept(serverFd, nullptr, nullptr);

        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }

            LOGI("accept failed: %s", strerror(errno));
            continue;
        }

        LOGI("PC client connected");
        HandleClient(clientFd);
        LOGI("PC client disconnected");
    }
}

} // namespace

void StartCommandServer() {
    bool expected = false;

    if (!g_serverStarted.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread(ServerThread).detach();
}

bool TryBeginRequest(RequestType& type) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_state != DumpState::Pending) {
        return false;
    }

    type = g_requestType;
    g_state = DumpState::Processing;
    return true;
}

void CompleteDumpRequest(std::string json) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_state != DumpState::Processing) {
            return;
        }

        g_result = std::move(json);
        g_state = DumpState::Complete;
    }

    g_cv.notify_all();
}

} // namespace XposedPoco
