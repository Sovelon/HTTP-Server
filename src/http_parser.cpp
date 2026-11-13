#include "http_parser.hpp"

#include <charconv>

namespace http {
namespace {

constexpr std::string_view kCrlf = "\r\n";
constexpr std::string_view kHeaderEnd = "\r\n\r\n";

char to_lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_lower_ascii(c);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) return false;
    }
    return true;
}

// Символы token по RFC 9110 (имена методов и заголовков).
bool is_token_char(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool is_token(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!is_token_char(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string_view trim_ows(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

Method method_from_name(std::string_view name) {
    if (name == "GET") return Method::Get;
    if (name == "POST") return Method::Post;
    if (name == "PUT") return Method::Put;
    if (name == "PATCH") return Method::Patch;
    if (name == "DELETE") return Method::Delete;
    if (name == "HEAD") return Method::Head;
    if (name == "OPTIONS") return Method::Options;
    return Method::Other;
}

ParseResult make_error() {
    ParseResult r;
    r.status = ParseStatus::Error;
    r.error_status = 400;
    return r;
}

// Значение Content-Length: только цифры, не больше лимита.
bool parse_content_length(std::string_view s, std::size_t& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    std::size_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) return false;
    if (value > kMaxBodyBytes) return false;
    out = value;
    return true;
}

// "GET /items?x=1 HTTP/1.1"
bool parse_request_line(std::string_view line, Request& req) {
    const auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return false;
    const auto sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;

    const std::string_view method = line.substr(0, sp1);
    const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = line.substr(sp2 + 1);

    if (!is_token(method)) return false;
    if (target.empty() || target.front() != '/') return false;
    for (char c : target) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= ' ' || u == 0x7f) return false;  // пробелы и управляющие символы
    }

    if (version == "HTTP/1.1") {
        req.version_minor = 1;
    } else if (version == "HTTP/1.0") {
        req.version_minor = 0;
    } else {
        return false;
    }

    req.method_name = std::string(method);
    req.method = method_from_name(method);
    req.target = std::string(target);

    const auto q = target.find('?');
    if (q == std::string_view::npos) {
        req.path = std::string(target);
        req.query.clear();
    } else {
        req.path = std::string(target.substr(0, q));
        req.query = std::string(target.substr(q + 1));
    }
    return true;
}

// "Name: value"
bool parse_header_line(std::string_view line, Request& req) {
    // obs-fold (продолжение заголовка с пробела) не поддерживаем.
    if (line.empty() || line.front() == ' ' || line.front() == '\t') return false;
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return false;

    const std::string_view name = line.substr(0, colon);
    if (!is_token(name)) return false;  // пробел перед ':' тоже сюда

    const std::string_view value = trim_ows(line.substr(colon + 1));
    for (char c : value) {
        const auto u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t') || u == 0x7f) return false;
    }

    req.headers.emplace_back(to_lower(name), std::string(value));
    return true;
}

}  // namespace

std::optional<std::string_view> Request::header(std::string_view name) const {
    for (const auto& [k, v] : headers) {
        if (iequals(k, name)) return std::string_view(v);
    }
    return std::nullopt;
}

ParseResult parse_request(std::string_view data, Request& out) {
    // 1. Ищем конец заголовков.
    const auto header_end = data.find(kHeaderEnd);
    if (header_end == std::string_view::npos) {
        // Заголовки ещё не пришли целиком. Если уже слишком много — это мусор.
        if (data.size() > kMaxHeaderBytes) return make_error();
        return ParseResult{ParseStatus::Incomplete, 0, 400};
    }
    if (header_end + kHeaderEnd.size() > kMaxHeaderBytes) return make_error();

    Request req;
    std::string_view head = data.substr(0, header_end);

    // 2. Строка запроса.
    const auto first_end = head.find(kCrlf);
    const std::string_view request_line =
        (first_end == std::string_view::npos) ? head : head.substr(0, first_end);
    if (!parse_request_line(request_line, req)) return make_error();

    // 3. Заголовки.
    if (first_end != std::string_view::npos) {
        std::string_view rest = head.substr(first_end + kCrlf.size());
        while (true) {
            const auto line_end = rest.find(kCrlf);
            const std::string_view line =
                (line_end == std::string_view::npos) ? rest : rest.substr(0, line_end);
            if (req.headers.size() >= kMaxHeaderCount) return make_error();
            if (!parse_header_line(line, req)) return make_error();
            if (line_end == std::string_view::npos) break;
            rest.remove_prefix(line_end + kCrlf.size());
        }
    }

    // 4. Content-Length и Transfer-Encoding.
    std::optional<std::size_t> content_length;
    for (const auto& [name, value] : req.headers) {
        if (name == "transfer-encoding") return make_error();  // chunked не поддерживаем
        if (name == "content-length") {
            std::size_t len = 0;
            if (!parse_content_length(value, len)) return make_error();
            if (content_length && *content_length != len) return make_error();  // конфликт
            content_length = len;
        }
    }
    const std::size_t body_len = content_length.value_or(0);

    // 5. Тело.
    const std::size_t body_start = header_end + kHeaderEnd.size();
    if (data.size() < body_start + body_len) {
        return ParseResult{ParseStatus::Incomplete, 0, 400};
    }
    req.body = std::string(data.substr(body_start, body_len));

    // 6. keep-alive: в HTTP/1.1 включён по умолчанию, в HTTP/1.0 — выключен.
    req.keep_alive = (req.version_minor == 1);
    if (const auto conn = req.header("connection")) {
        // Значение — список токенов через запятую.
        std::string_view list = *conn;
        while (!list.empty()) {
            const auto comma = list.find(',');
            const std::string_view token = trim_ows(list.substr(0, comma));
            if (iequals(token, "close")) req.keep_alive = false;
            else if (iequals(token, "keep-alive")) req.keep_alive = true;
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    }

    out = std::move(req);
    return ParseResult{ParseStatus::Complete, body_start + body_len, 400};
}

}  // namespace http
