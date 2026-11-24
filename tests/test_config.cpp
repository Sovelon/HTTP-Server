#include <gtest/gtest.h>

#include <vector>

#include "config.hpp"

namespace {

ArgsResult parse(std::vector<const char*> args) {
    args.insert(args.begin(), "server");
    return parse_args(static_cast<int>(args.size()), args.data());
}

}  // namespace

TEST(Config, SeparateValues) {
    const auto r = parse({"--port", "9090", "--threads", "8"});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.config.port, 9090);
    EXPECT_EQ(r.config.threads, 8u);
    EXPECT_FALSE(r.help);
}

TEST(Config, EqualsSyntax) {
    const auto r = parse({"--port=8081", "--threads=2"});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.config.port, 8081);
    EXPECT_EQ(r.config.threads, 2u);
}

TEST(Config, DefaultsWhenNoArguments) {
    const auto r = parse({});
    ASSERT_TRUE(r.error.empty());
    EXPECT_EQ(r.config.port, 8080);
    EXPECT_GE(r.config.threads, 1u);
}

TEST(Config, HelpFlag) {
    EXPECT_TRUE(parse({"--help"}).help);
    EXPECT_TRUE(parse({"-h"}).help);
}

TEST(Config, InvalidPort) {
    EXPECT_FALSE(parse({"--port", "0"}).error.empty());
    EXPECT_FALSE(parse({"--port", "65536"}).error.empty());
    EXPECT_FALSE(parse({"--port", "-1"}).error.empty());
    EXPECT_FALSE(parse({"--port", "abc"}).error.empty());
    EXPECT_FALSE(parse({"--port", ""}).error.empty());
    EXPECT_FALSE(parse({"--port", "99999999999999999999"}).error.empty());
}

TEST(Config, InvalidThreads) {
    EXPECT_FALSE(parse({"--threads", "0"}).error.empty());
    EXPECT_FALSE(parse({"--threads", "100000"}).error.empty());
    EXPECT_FALSE(parse({"--threads", "x"}).error.empty());
}

TEST(Config, MissingValue) {
    EXPECT_FALSE(parse({"--port"}).error.empty());
    EXPECT_FALSE(parse({"--threads"}).error.empty());
}

TEST(Config, UnknownOptionAndPositionalArgument) {
    EXPECT_FALSE(parse({"--verbose"}).error.empty());
    EXPECT_FALSE(parse({"8080"}).error.empty());
}
