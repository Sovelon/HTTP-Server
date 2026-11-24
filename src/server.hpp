#pragma once

#include <atomic>
#include <cstdint>

namespace http {

// Простой TCP-сервер на POSIX sockets. Пока всё обслуживается в одном потоке:
// accept -> читаем запросы по keep-alive -> отвечаем. Пул потоков появится в v0.3.
class Server {
public:
    // Создаёт слушающий сокет. port = 0 — ОС выберет свободный порт (удобно в тестах).
    // Бросает std::runtime_error, если не удалось bind/listen.
    explicit Server(std::uint16_t port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Порт, на котором реально слушает сервер.
    std::uint16_t port() const { return port_; }

    // Принимает соединения, пока `stop` не станет true. Возвращается быстро
    // после установки флага: текущий запрос дорабатывается, соединение закрывается.
    void run(const std::atomic<bool>& stop);

private:
    void serve_connection(int fd, const std::atomic<bool>& stop);

    int listen_fd_ = -1;
    std::uint16_t port_ = 0;
};

}  // namespace http
