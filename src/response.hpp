#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace http {

struct Response {
    int status = 200;
    std::string body;  // JSON
    bool keep_alive = true;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

// Ответ с JSON-телом и Content-Type: application/json.
Response json_response(int status, std::string body);

std::string_view reason_phrase(int status);

// Готовый текст ответа для записи в сокет. Для 204 тело и Content-Length не добавляются.
std::string serialize(const Response& response);

}  // namespace http
