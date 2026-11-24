#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace http {

enum class Method { Get, Post, Put, Patch, Delete, Head, Options, Other };

struct Request {
    Method method = Method::Other;
    std::string method_name;  // исходное имя метода, например "GET"
    std::string target;       // как пришло: "/items?offset=0"
    std::string path;         // "/items"
    std::string query;        // "offset=0" (без '?')
    int version_minor = 1;    // 1 для HTTP/1.1, 0 для HTTP/1.0
    // Имена заголовков хранятся в нижнем регистре.
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool keep_alive = true;

    // Поиск заголовка без учёта регистра имени.
    std::optional<std::string_view> header(std::string_view name) const;
};

enum class ParseStatus {
    Complete,    // запрос разобран целиком
    Incomplete,  // данных пока не хватает, нужно дочитать из сокета
    Error,       // запрос некорректен, нужно ответить 400 и закрыть соединение
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    // Сколько байт входного буфера занял запрос (только при Complete).
    // Остаток буфера — это начало следующего запроса (pipelining).
    std::size_t consumed = 0;
    // HTTP-код ответа при Error.
    int error_status = 400;
};

// Ограничения, чтобы клиент не мог раздуть память сервера.
inline constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
inline constexpr std::size_t kMaxBodyBytes = 1024 * 1024;
inline constexpr std::size_t kMaxHeaderCount = 100;

// Разбирает один запрос из начала `data`. Функция не хранит состояния:
// при Incomplete вызывающий дочитывает данные и вызывает её снова.
// `out` валиден только при ParseStatus::Complete.
ParseResult parse_request(std::string_view data, Request& out);

}  // namespace http
