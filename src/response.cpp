#include "response.hpp"

namespace http {

Response json_response(int status, std::string body) {
    Response r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

std::string_view reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

std::string serialize(const Response& r) {
    const bool has_body = (r.status != 204);

    std::string out;
    out.reserve(128 + r.body.size());
    out += "HTTP/1.1 ";
    out += std::to_string(r.status);
    out += ' ';
    out += reason_phrase(r.status);
    out += "\r\n";
    if (has_body) {
        out += "Content-Type: application/json\r\n";
        out += "Content-Length: ";
        out += std::to_string(r.body.size());
        out += "\r\n";
    }
    out += r.keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    for (const auto& [name, value] : r.extra_headers) {
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
    }
    out += "\r\n";
    if (has_body) out += r.body;
    return out;
}

}  // namespace http
