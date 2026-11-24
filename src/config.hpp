#pragma once

#include <cstdint>
#include <string>

// Параметры командной строки: ./server --port 8080 --threads 8
struct Config {
    std::uint16_t port = 8080;
    unsigned threads = 1;
};

struct ArgsResult {
    Config config;
    bool help = false;
    std::string error;  // пусто, если разбор прошёл успешно
};

// Принимает и "--port 8080", и "--port=8080". argv[0] пропускается.
ArgsResult parse_args(int argc, const char* const* argv);

std::string usage(const char* program);
