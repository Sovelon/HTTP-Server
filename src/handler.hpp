#pragma once

#include "http_parser.hpp"
#include "response.hpp"
#include "storage.hpp"

namespace http {

// Обрабатывает HTTP-запрос, используя общее хранилище.
Response handle_request(
    const Request& request,
    Storage& storage
);

} // namespace http