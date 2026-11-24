#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "server.hpp"

namespace {

// Запускает сервер на свободном порту в отдельном потоке и останавливает при выходе из области видимости.
class RunningServer {
public:
    RunningServer() : thread_([this] { server_.run(stop_); }) {}
    ~RunningServer() {
        stop_.store(true);
        thread_.join();
    }
    std::uint16_t port() const { return server_.port(); }

private:
    http::Server server_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

class Client {
public:
    explicit Client(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd_, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        EXPECT_EQ(::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        timeval tv{};
        tv.tv_sec = 3;  // тест не должен зависать, если сервер молчит
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    ~Client() { ::close(fd_); }

    void send(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
            ASSERT_GT(n, 0);
            sent += static_cast<std::size_t>(n);
        }
    }

    // Читает ровно один HTTP-ответ (заголовки + тело по Content-Length).
    // Лишние байты следующего ответа остаются во внутреннем буфере.
    std::string read_response() {
        while (buffer_.find("\r\n\r\n") == std::string::npos) {
            if (!fill()) return {};
        }
        const std::size_t head_end = buffer_.find("\r\n\r\n") + 4;
        std::size_t body_len = 0;
        const std::string key = "Content-Length: ";
        if (const auto pos = buffer_.find(key); pos != std::string::npos && pos < head_end) {
            body_len = std::stoul(buffer_.substr(pos + key.size()));
        }
        while (buffer_.size() < head_end + body_len) {
            if (!fill()) return {};
        }
        std::string response = buffer_.substr(0, head_end + body_len);
        buffer_.erase(0, head_end + body_len);
        return response;
    }

    // true, если сервер закрыл соединение (recv вернул 0).
    bool closed_by_server() {
        char c;
        return ::recv(fd_, &c, 1, 0) == 0;
    }

private:
    bool fill() {
        char chunk[1024];
        const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buffer_.append(chunk, static_cast<std::size_t>(n));
        return true;
    }

    int fd_ = -1;
    std::string buffer_;
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Server, HealthReturnsOk) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
    const std::string r = c.read_response();
    EXPECT_TRUE(r.rfind("HTTP/1.1 200 OK\r\n", 0) == 0) << r;
    EXPECT_TRUE(contains(r, "Content-Type: application/json"));
    EXPECT_TRUE(contains(r, "Connection: keep-alive"));
    EXPECT_TRUE(r.size() >= 15 && r.substr(r.size() - 15) == R"({"status":"ok"})") << r;
}

TEST(Server, KeepAliveServesManyRequestsOnOneConnection) {
    RunningServer server;
    Client c(server.port());
    for (int i = 0; i < 5; ++i) {
        c.send("GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
        const std::string r = c.read_response();
        EXPECT_TRUE(r.rfind("HTTP/1.1 200 OK", 0) == 0) << "request #" << i << ": " << r;
    }
}

TEST(Server, PipelinedRequestsAreAnsweredInOrder) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /health HTTP/1.1\r\n\r\nGET /nope HTTP/1.1\r\n\r\nGET /health HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 200", 0) == 0);
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 404", 0) == 0);
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 200", 0) == 0);
}

TEST(Server, ConnectionCloseClosesTheSocket) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /health HTTP/1.1\r\nConnection: close\r\n\r\n");
    const std::string r = c.read_response();
    EXPECT_TRUE(contains(r, "Connection: close"));
    EXPECT_TRUE(c.closed_by_server());
}

TEST(Server, Http10ClosesByDefault) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /health HTTP/1.0\r\n\r\n");
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 200", 0) == 0);
    EXPECT_TRUE(c.closed_by_server());
}

TEST(Server, UnknownPathGives404) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /unknown HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 404 Not Found", 0) == 0);
}

TEST(Server, WrongMethodGives405WithAllowHeader) {
    RunningServer server;
    Client c(server.port());
    c.send("POST /health HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    const std::string r = c.read_response();
    EXPECT_TRUE(r.rfind("HTTP/1.1 405", 0) == 0) << r;
    EXPECT_TRUE(contains(r, "Allow: GET"));
}

TEST(Server, MalformedRequestGives400AndCloses) {
    RunningServer server;
    Client c(server.port());
    c.send("THIS IS NOT HTTP\r\n\r\n");
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 400", 0) == 0);
    EXPECT_TRUE(c.closed_by_server());
}

TEST(Server, SurvivesBadClientAndKeepsServing) {
    RunningServer server;
    {
        Client bad(server.port());
        bad.send("\x01\x02\x03 garbage \r\n\r\n");
        bad.read_response();
    }
    {
        // Клиент, который подключился и сразу ушёл.
        Client gone(server.port());
    }
    Client good(server.port());
    good.send("GET /health HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(good.read_response().rfind("HTTP/1.1 200", 0) == 0);
}

TEST(Server, RequestSplitAcrossSeveralWrites) {
    RunningServer server;
    Client c(server.port());
    c.send("GET /hea");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    c.send("lth HTTP/1.1\r\nHo");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    c.send("st: x\r\n\r\n");
    EXPECT_TRUE(c.read_response().rfind("HTTP/1.1 200", 0) == 0);
}

TEST(Server, StopsPromptlyEvenWithIdleKeepAliveConnection) {
    const auto start = std::chrono::steady_clock::now();
    {
        RunningServer server;
        Client idle(server.port());
        idle.send("GET /health HTTP/1.1\r\n\r\n");
        idle.read_response();
        // Соединение остаётся открытым и простаивает, пока RunningServer не будет уничтожен.
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}
