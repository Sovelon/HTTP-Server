#include "handler.hpp"

namespace http {

Response handle_request(const Request& request) {
    if (request.path == "/health") {
        if (request.method != Method::Get) {
            Response r = json_response(405, R"({"error":"method not allowed"})");
            r.extra_headers.emplace_back("Allow", "GET");
            return r;
        }
        return json_response(200, R"({"status":"ok"})");
    }
    return json_response(404, R"({"error":"not found"})");
}

}  // namespace http
