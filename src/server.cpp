#include "server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "handler.hpp"
#include "http_parser.hpp"
#include "response.hpp"

namespace http {
namespace {

// Как часто проверяем флаг остановки, пока ждём данные.
constexpr int kPollIntervalMs = 100;
// Если клиент молчит дольше этого времени, закрываем keep-alive соединение.
constexpr std::chrono::seconds kIdleTimeout{10};
constexpr std::size_t kReadChunk = 4096;

// RAII-обёртка над файловым дескриптором, чтобы не забыть close().
class ScopedFd {
public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() {
        if (fd_ >= 0) ::close(fd_);
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

// Пишет все байты, учитывая частичные записи и прерывания сигналом.
bool send_all(int fd, std::string_view data) {
#ifdef MSG_NOSIGNAL
    constexpr int kFlags = MSG_NOSIGNAL;  // не получать SIGPIPE, если клиент закрылся
#else
    constexpr int kFlags = 0;
#endif
    while (!data.empty()) {
        const ssize_t n = ::send(fd, data.data(), data.size(), kFlags);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

void configure_client_socket(int fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));  // macOS
#endif
    timeval tv{};
    tv.tv_sec = 5;  // не зависаем на записи медленному клиенту
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

}  // namespace

Server::Server(std::uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw_errno("socket");

    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        errno = saved;
        throw_errno("bind port " + std::to_string(port));
    }
    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        errno = saved;
        throw_errno("listen");
    }

    // Узнаём реальный порт (важно, если просили port = 0).
    socklen_t len = sizeof(addr);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port_ = ntohs(addr.sin_port);
    } else {
        port_ = port;
    }
}

Server::~Server() {
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

void Server::run(const std::atomic<bool>& stop) {
    while (!stop.load()) {
        pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, kPollIntervalMs);
        if (ready < 0) {
            if (errno == EINTR) continue;  // сигнал: проверим stop на следующем витке
            throw_errno("poll");
        }
        if (ready == 0) continue;

        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EAGAIN) continue;
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            continue;
        }
        ScopedFd guard(client);
        configure_client_socket(client);
        serve_connection(client, stop);
    }
}

void Server::serve_connection(int fd, const std::atomic<bool>& stop) {
    using Clock = std::chrono::steady_clock;

    std::string buffer;
    char chunk[kReadChunk];
    auto last_activity = Clock::now();

    while (!stop.load()) {
        // 1. Сначала пробуем разобрать то, что уже лежит в буфере
        //    (там может быть несколько запросов подряд — pipelining).
        if (!buffer.empty()) {
            Request request;
            const ParseResult parsed = parse_request(buffer, request);

            if (parsed.status == ParseStatus::Complete) {
                Response response = handle_request(request);
                response.keep_alive = response.keep_alive && request.keep_alive;
                if (!send_all(fd, serialize(response))) return;
                buffer.erase(0, parsed.consumed);
                last_activity = Clock::now();
                if (!response.keep_alive) return;
                continue;
            }
            if (parsed.status == ParseStatus::Error) {
                Response response = json_response(parsed.error_status, R"({"error":"bad request"})");
                response.keep_alive = false;
                send_all(fd, serialize(response));
                return;
            }
            // Incomplete: нужно дочитать.
        }

        // 2. Ждём новые данные, периодически проверяя stop и таймаут простоя.
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, kPollIntervalMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (ready == 0) {
            if (Clock::now() - last_activity > kIdleTimeout) return;
            continue;
        }

        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (n == 0) return;  // клиент закрыл соединение
        buffer.append(chunk, static_cast<std::size_t>(n));
        last_activity = Clock::now();
    }
}

}  // namespace http
