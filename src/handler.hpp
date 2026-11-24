#pragma once

#include "http_parser.hpp"
#include "response.hpp"

namespace http {

// Маршрутизация запроса. Пока умеет только GET /health,
// остальные ручки добавятся к v0.2.
Response handle_request(const Request& request);

}  // namespace http
