#include "config.hpp"

#include <charconv>
#include <string_view>
#include <thread>

namespace {

constexpr unsigned long kMaxThreads = 4096;

unsigned default_threads() {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 4 : n;
}

// Только цифры, без знака и пробелов.
bool parse_uint(std::string_view s, unsigned long& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    unsigned long value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) return false;
    out = value;
    return true;
}

}  // namespace

ArgsResult parse_args(int argc, const char* const* argv) {
    ArgsResult res;
    res.config.threads = default_threads();

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            res.help = true;
            continue;
        }
        if (arg.substr(0, 2) != "--") {
            res.error = "unexpected argument: " + std::string(arg);
            return res;
        }

        std::string_view name = arg;
        std::string_view value;
        bool has_value = false;
        if (const auto eq = arg.find('='); eq != std::string_view::npos) {
            name = arg.substr(0, eq);
            value = arg.substr(eq + 1);
            has_value = true;
        }

        if (name != "--port" && name != "--threads") {
            res.error = "unknown option: " + std::string(name);
            return res;
        }
        if (!has_value) {
            if (i + 1 >= argc) {
                res.error = "missing value for " + std::string(name);
                return res;
            }
            value = argv[++i];
        }

        unsigned long number = 0;
        if (!parse_uint(value, number)) {
            res.error = "invalid value for " + std::string(name) + ": " + std::string(value);
            return res;
        }

        if (name == "--port") {
            if (number < 1 || number > 65535) {
                res.error = "port must be in range 1..65535";
                return res;
            }
            res.config.port = static_cast<std::uint16_t>(number);
        } else {
            if (number < 1 || number > kMaxThreads) {
                res.error = "threads must be in range 1.." + std::to_string(kMaxThreads);
                return res;
            }
            res.config.threads = static_cast<unsigned>(number);
        }
    }
    return res;
}

std::string usage(const char* program) {
    std::string u = "Usage: ";
    u += program;
    u += " [--port N] [--threads N]\n"
         "  --port     TCP port to listen on (default 8080)\n"
         "  --threads  number of worker threads (default: number of CPU cores)\n"
         "  --help     show this message\n";
    return u;
}
