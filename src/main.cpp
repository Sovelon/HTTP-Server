#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "config.hpp"
#include "server.hpp"

namespace {

std::atomic<bool> g_stop{false};
static_assert(std::atomic<bool>::is_always_lock_free,
              "atomic<bool> must be lock-free to be used in a signal handler");

extern "C" void handle_signal(int) {
    g_stop.store(true);
}

void install_signal_handlers() {
    // Запись в закрытый сокет не должна убивать процесс.
    std::signal(SIGPIPE, SIG_IGN);

    struct sigaction sa {};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // без SA_RESTART: poll/accept прерываются и мы замечаем флаг
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    const ArgsResult args = parse_args(argc, argv);
    if (!args.error.empty()) {
        std::cerr << "error: " << args.error << "\n\n" << usage(argv[0]);
        return 2;
    }
    if (args.help) {
        std::cout << usage(argv[0]);
        return 0;
    }

    install_signal_handlers();

    try {
        http::Server server(args.config.port);
        std::cout << "listening on port " << server.port()
                  << " (threads=" << args.config.threads
                  << ", ignored until the thread pool is implemented)" << std::endl;
        server.run(g_stop);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }

    std::cout << "server stopped" << std::endl;
    return 0;
}
