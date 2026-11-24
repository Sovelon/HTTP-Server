#include <gtest/gtest.h>

#include <string>

#include "http_parser.hpp"

using http::Method;
using http::ParseStatus;
using http::Request;

namespace {

http::ParseResult parse(const std::string& raw, Request& req) {
    return http::parse_request(raw, req);
}

}  // namespace

TEST(HttpParser, SimpleGet) {
    Request req;
    const std::string raw = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const auto r = parse(raw, req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, raw.size());
    EXPECT_EQ(req.method, Method::Get);
    EXPECT_EQ(req.method_name, "GET");
    EXPECT_EQ(req.path, "/health");
    EXPECT_TRUE(req.query.empty());
    EXPECT_EQ(req.version_minor, 1);
    EXPECT_TRUE(req.body.empty());
    EXPECT_TRUE(req.keep_alive);
}

TEST(HttpParser, NoHeadersAtAll) {
    Request req;
    const auto r = parse("GET / HTTP/1.1\r\n\r\n", req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(req.path, "/");
    EXPECT_TRUE(req.headers.empty());
}

TEST(HttpParser, QueryStringIsSplitFromPath) {
    Request req;
    const auto r = parse("GET /items?offset=10&limit=5 HTTP/1.1\r\n\r\n", req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(req.target, "/items?offset=10&limit=5");
    EXPECT_EQ(req.path, "/items");
    EXPECT_EQ(req.query, "offset=10&limit=5");
}

TEST(HttpParser, PostWithBody) {
    Request req;
    const std::string body = R"({"name":"a","value":1})";
    const std::string raw = "POST /items HTTP/1.1\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    const auto r = parse(raw, req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, raw.size());
    EXPECT_EQ(req.method, Method::Post);
    EXPECT_EQ(req.body, body);
}

TEST(HttpParser, AllMethodsAreRecognised) {
    struct Case { const char* name; Method m; };
    const Case cases[] = {{"GET", Method::Get},       {"POST", Method::Post},
                          {"PUT", Method::Put},       {"PATCH", Method::Patch},
                          {"DELETE", Method::Delete}, {"HEAD", Method::Head},
                          {"OPTIONS", Method::Options}, {"BREW", Method::Other}};
    for (const auto& c : cases) {
        Request req;
        const auto r = parse(std::string(c.name) + " /x HTTP/1.1\r\n\r\n", req);
        ASSERT_EQ(r.status, ParseStatus::Complete) << c.name;
        EXPECT_EQ(req.method, c.m) << c.name;
        EXPECT_EQ(req.method_name, c.name);
    }
}

TEST(HttpParser, HeaderNamesAreCaseInsensitive) {
    Request req;
    const auto r = parse("GET / HTTP/1.1\r\nCoNtEnT-TyPe: application/json\r\n\r\n", req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    ASSERT_TRUE(req.header("content-type").has_value());
    EXPECT_EQ(*req.header("Content-Type"), "application/json");
    EXPECT_FALSE(req.header("x-missing").has_value());
}

TEST(HttpParser, HeaderValueIsTrimmed) {
    Request req;
    const auto r = parse("GET / HTTP/1.1\r\nX-Test:   spaced value \t\r\n\r\n", req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(*req.header("x-test"), "spaced value");
}

TEST(HttpParser, IncompleteHeaders) {
    Request req;
    EXPECT_EQ(parse("GET /health HTTP/1.1\r\nHost: loc", req).status, ParseStatus::Incomplete);
    EXPECT_EQ(parse("", req).status, ParseStatus::Incomplete);
}

TEST(HttpParser, IncompleteBody) {
    Request req;
    const auto r = parse("POST /items HTTP/1.1\r\nContent-Length: 10\r\n\r\n12345", req);
    EXPECT_EQ(r.status, ParseStatus::Incomplete);
}

TEST(HttpParser, ByteByByteFeedEventuallyCompletes) {
    const std::string body = "hello";
    const std::string raw = "POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\n" + body;
    Request req;
    for (std::size_t n = 0; n < raw.size(); ++n) {
        EXPECT_EQ(http::parse_request(std::string_view(raw).substr(0, n), req).status,
                  ParseStatus::Incomplete)
            << "prefix length " << n;
    }
    const auto r = http::parse_request(raw, req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(req.body, body);
}

TEST(HttpParser, PipelinedRequestsReportConsumedBytes) {
    const std::string first = "GET /health HTTP/1.1\r\n\r\n";
    const std::string second = "GET /items HTTP/1.1\r\n\r\n";
    const std::string raw = first + second;

    Request req;
    auto r = parse(raw, req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, first.size());
    EXPECT_EQ(req.path, "/health");

    r = http::parse_request(std::string_view(raw).substr(r.consumed), req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, second.size());
    EXPECT_EQ(req.path, "/items");
}

TEST(HttpParser, KeepAliveDefaults) {
    Request req;
    ASSERT_EQ(parse("GET / HTTP/1.1\r\n\r\n", req).status, ParseStatus::Complete);
    EXPECT_TRUE(req.keep_alive);

    ASSERT_EQ(parse("GET / HTTP/1.0\r\n\r\n", req).status, ParseStatus::Complete);
    EXPECT_EQ(req.version_minor, 0);
    EXPECT_FALSE(req.keep_alive);
}

TEST(HttpParser, ConnectionHeaderOverridesDefault) {
    Request req;
    ASSERT_EQ(parse("GET / HTTP/1.1\r\nConnection: close\r\n\r\n", req).status,
              ParseStatus::Complete);
    EXPECT_FALSE(req.keep_alive);

    ASSERT_EQ(parse("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", req).status,
              ParseStatus::Complete);
    EXPECT_TRUE(req.keep_alive);
}

TEST(HttpParser, MalformedRequestLine) {
    Request req;
    EXPECT_EQ(parse("GARBAGE\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET /only-two-parts\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET  / HTTP/1.1\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET /a b HTTP/1.1\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET noslash HTTP/1.1\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("G@T / HTTP/1.1\r\n\r\n", req).status, ParseStatus::Error);
}

TEST(HttpParser, UnsupportedVersion) {
    Request req;
    EXPECT_EQ(parse("GET / HTTP/2.0\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / HTTP/1.2\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / FTP/1.1\r\n\r\n", req).status, ParseStatus::Error);
}

TEST(HttpParser, MalformedHeaders) {
    Request req;
    EXPECT_EQ(parse("GET / HTTP/1.1\r\nNoColonHere\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / HTTP/1.1\r\nBad Name: x\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / HTTP/1.1\r\nName : x\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / HTTP/1.1\r\n: empty\r\n\r\n", req).status, ParseStatus::Error);
    EXPECT_EQ(parse("GET / HTTP/1.1\r\nA: 1\r\n folded\r\n\r\n", req).status, ParseStatus::Error);
}

TEST(HttpParser, BadContentLength) {
    Request req;
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n", req).status,
              ParseStatus::Error);
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", req).status,
              ParseStatus::Error);
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: 1 2\r\n\r\n", req).status,
              ParseStatus::Error);
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: \r\n\r\n", req).status,
              ParseStatus::Error);
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n", req).status,
              ParseStatus::Error);
}

TEST(HttpParser, ConflictingContentLengthIsRejected) {
    Request req;
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab", req)
                  .status,
              ParseStatus::Error);
    // Одинаковые значения допустимы.
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nab", req)
                  .status,
              ParseStatus::Complete);
}

TEST(HttpParser, TooLargeBodyIsRejectedBeforeReadingIt) {
    Request req;
    const std::string raw = "POST / HTTP/1.1\r\nContent-Length: " +
                            std::to_string(http::kMaxBodyBytes + 1) + "\r\n\r\n";
    EXPECT_EQ(parse(raw, req).status, ParseStatus::Error);
}

TEST(HttpParser, TransferEncodingIsRejected) {
    Request req;
    EXPECT_EQ(parse("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", req).status,
              ParseStatus::Error);
}

TEST(HttpParser, OversizedHeadersAreRejected) {
    Request req;
    // Нет конца заголовков и данных уже больше лимита.
    const std::string no_end = "GET / HTTP/1.1\r\nX: " + std::string(http::kMaxHeaderBytes, 'a');
    EXPECT_EQ(parse(no_end, req).status, ParseStatus::Error);

    // Конец заголовков есть, но сами заголовки больше лимита.
    const std::string with_end = no_end + "\r\n\r\n";
    EXPECT_EQ(parse(with_end, req).status, ParseStatus::Error);
}

TEST(HttpParser, TooManyHeadersAreRejected) {
    Request req;
    std::string raw = "GET / HTTP/1.1\r\n";
    for (std::size_t i = 0; i <= http::kMaxHeaderCount; ++i) raw += "H" + std::to_string(i) + ": v\r\n";
    raw += "\r\n";
    EXPECT_EQ(parse(raw, req).status, ParseStatus::Error);
}

TEST(HttpParser, BareLineFeedIsNotAHeaderTerminator) {
    Request req;
    // Без \r\n\r\n запрос считается неполным, а не разобранным.
    EXPECT_EQ(parse("GET / HTTP/1.1\nHost: x\n\n", req).status, ParseStatus::Incomplete);
}

TEST(HttpParser, BinaryGarbageDoesNotCrash) {
    Request req;
    std::string junk;
    for (int i = 0; i < 256; ++i) junk.push_back(static_cast<char>(i));
    junk += "\r\n\r\n";
    EXPECT_EQ(parse(junk, req).status, ParseStatus::Error);
}
