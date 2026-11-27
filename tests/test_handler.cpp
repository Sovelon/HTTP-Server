
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

#include "handler.hpp"

using namespace http;
using json = nlohmann::json;

// --------------------------------------------------
// TEST HELPERS
// --------------------------------------------------

class HandlerTest : public ::testing::Test {
protected:
    Storage storage;

    Request make_request(
        Method method,
        const std::string& path,
        const std::string& body = "",
        const std::string& query = ""
    ) {
        Request request;

        request.method = method;
        request.path = path;
        request.target = path;

        if (!query.empty()) {
            request.target += "?" + query;
        }

        request.query = query;
        request.body = body;

        return request;
    }

    json response_json(const Response& response) {
        return json::parse(response.body);
    }

    Item create_test_item() {
        return storage.create("Apple", 100);
    }
};

// --------------------------------------------------
// HEALTH
// --------------------------------------------------

TEST_F(HandlerTest, HealthReturns200) {
    const auto request = make_request(Method::Get, "/health");

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response_json(response)["status"], "ok");
}

TEST_F(HandlerTest, HealthRejectsWrongMethod) {
    const auto request = make_request(Method::Post, "/health");

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 405);
}

// --------------------------------------------------
// GET /items
// --------------------------------------------------

TEST_F(HandlerTest, GetItemsReturnsEmptyArray) {
    const auto request = make_request(Method::Get, "/items");

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);
    EXPECT_TRUE(response_json(response).is_array());
    EXPECT_TRUE(response_json(response).empty());
}

TEST_F(HandlerTest, GetItemsReturnsAllCreatedItems) {
    storage.create("Apple", 100);
    storage.create("Orange", 200);

    const auto request = make_request(Method::Get, "/items");

    const auto response = handle_request(request, storage);

    ASSERT_EQ(response.status, 200);

    const auto items = response_json(response);

    ASSERT_EQ(items.size(), 2);

    EXPECT_EQ(items[0]["name"], "Apple");
    EXPECT_EQ(items[1]["name"], "Orange");
}

TEST_F(HandlerTest, GetItemsSupportsPagination) {
    storage.create("First", 10);
    storage.create("Second", 20);
    storage.create("Third", 30);

    const auto request = make_request(
        Method::Get,
        "/items",
        "",
        "offset=1&limit=1"
    );

    const auto response = handle_request(request, storage);

    ASSERT_EQ(response.status, 200);

    const auto items = response_json(response);

    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["name"], "Second");
}

TEST_F(HandlerTest, GetItemsRejectsInvalidPagination) {
    const auto request = make_request(
        Method::Get,
        "/items",
        "",
        "offset=abc"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

TEST_F(HandlerTest, GetItemsRejectsZeroLimit) {
    const auto request = make_request(
        Method::Get,
        "/items",
        "",
        "limit=0"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

// --------------------------------------------------
// POST /items
// --------------------------------------------------

TEST_F(HandlerTest, PostItemsCreatesItem) {
    const auto request = make_request(
        Method::Post,
        "/items",
        R"({"name":"Apple","value":100})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 201);

    const auto item = response_json(response);

    EXPECT_EQ(item["id"], 1);
    EXPECT_EQ(item["name"], "Apple");
    EXPECT_EQ(item["value"], 100);
    EXPECT_EQ(item["version"], 1);
}

TEST_F(HandlerTest, PostItemsRejectsMalformedJson) {
    const auto request = make_request(
        Method::Post,
        "/items",
        R"({"name": "Apple",)"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

TEST_F(HandlerTest, PostItemsRequiresName) {
    const auto request = make_request(
        Method::Post,
        "/items",
        R"({"value":100})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

TEST_F(HandlerTest, PostItemsRequiresValue) {
    const auto request = make_request(
        Method::Post,
        "/items",
        R"({"name":"Apple"})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

TEST_F(HandlerTest, PostItemsRejectsWrongFieldTypes) {
    const auto request = make_request(
        Method::Post,
        "/items",
        R"({"name":123,"value":"hello"})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

// --------------------------------------------------
// GET /items/{id}
// --------------------------------------------------

TEST_F(HandlerTest, GetItemReturnsExistingItem) {
    const Item created = create_test_item();

    const auto request = make_request(
        Method::Get,
        "/items/1"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);

    const auto item = response_json(response);

    EXPECT_EQ(item["id"], created.id);
    EXPECT_EQ(item["name"], "Apple");
    EXPECT_EQ(item["value"], 100);
}

TEST_F(HandlerTest, GetItemReturns404ForMissingItem) {
    const auto request = make_request(
        Method::Get,
        "/items/999"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

// --------------------------------------------------
// PUT /items/{id}
// --------------------------------------------------

TEST_F(HandlerTest, PutItemReplacesItem) {
    create_test_item();

    const auto request = make_request(
        Method::Put,
        "/items/1",
        R"({"name":"Orange","value":200,"version":1})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);

    const auto item = response_json(response);

    EXPECT_EQ(item["name"], "Orange");
    EXPECT_EQ(item["value"], 200);
    EXPECT_EQ(item["version"], 2);
}

TEST_F(HandlerTest, PutItemReturns404ForMissingItem) {
    const auto request = make_request(
        Method::Put,
        "/items/999",
        R"({"name":"Orange","value":200})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

TEST_F(HandlerTest, PutItemReturns409ForVersionConflict) {
    create_test_item();

    const auto request = make_request(
        Method::Put,
        "/items/1",
        R"({"name":"Orange","value":200,"version":999})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 409);

    // Объект не должен был измениться.
    const auto current = storage.get(1);

    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(current->name, "Apple");
    EXPECT_EQ(current->value, 100);
    EXPECT_EQ(current->version, 1);
}

// --------------------------------------------------
// PATCH /items/{id}
// --------------------------------------------------

TEST_F(HandlerTest, PatchItemChangesOnlyProvidedFields) {
    create_test_item();

    const auto request = make_request(
        Method::Patch,
        "/items/1",
        R"({"value":300,"version":1})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);

    const auto item = response_json(response);

    EXPECT_EQ(item["name"], "Apple");
    EXPECT_EQ(item["value"], 300);
    EXPECT_EQ(item["version"], 2);
}

TEST_F(HandlerTest, PatchItemReturns409ForVersionConflict) {
    create_test_item();

    const auto request = make_request(
        Method::Patch,
        "/items/1",
        R"({"name":"Orange","version":999})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 409);
}

TEST_F(HandlerTest, PatchItemReturns404ForMissingItem) {
    const auto request = make_request(
        Method::Patch,
        "/items/999",
        R"({"value":300})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

TEST_F(HandlerTest, PatchItemRejectsWrongFieldTypes) {
    create_test_item();

    const auto request = make_request(
        Method::Patch,
        "/items/1",
        R"({"value":"three hundred"})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

// --------------------------------------------------
// DELETE /items/{id}
// --------------------------------------------------

TEST_F(HandlerTest, DeleteItemReturns204) {
    create_test_item();

    const auto request = make_request(
        Method::Delete,
        "/items/1"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 204);
    EXPECT_TRUE(response.body.empty());

    EXPECT_FALSE(storage.get(1).has_value());
}

TEST_F(HandlerTest, DeleteItemReturns404ForMissingItem) {
    const auto request = make_request(
        Method::Delete,
        "/items/999"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

// --------------------------------------------------
// POST /items/{id}/increment
// --------------------------------------------------

TEST_F(HandlerTest, IncrementItemIncreasesValue) {
    create_test_item();

    const auto request = make_request(
        Method::Post,
        "/items/1/increment",
        R"({"delta":50})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);

    const auto item = response_json(response);

    EXPECT_EQ(item["value"], 150);
    EXPECT_EQ(item["version"], 2);
}

TEST_F(HandlerTest, IncrementItemSupportsNegativeDelta) {
    create_test_item();

    const auto request = make_request(
        Method::Post,
        "/items/1/increment",
        R"({"delta":-30})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response_json(response)["value"], 70);
}

TEST_F(HandlerTest, IncrementItemReturns404ForMissingItem) {
    const auto request = make_request(
        Method::Post,
        "/items/999/increment",
        R"({"delta":1})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

TEST_F(HandlerTest, IncrementItemRequiresDelta) {
    create_test_item();

    const auto request = make_request(
        Method::Post,
        "/items/1/increment",
        R"({"value":10})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

TEST_F(HandlerTest, IncrementItemRejectsWrongDeltaType) {
    create_test_item();

    const auto request = make_request(
        Method::Post,
        "/items/1/increment",
        R"({"delta":"five"})"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 400);
}

// --------------------------------------------------
// ROUTING AND METHODS
// --------------------------------------------------

TEST_F(HandlerTest, UnknownPathReturns404) {
    const auto request = make_request(
        Method::Get,
        "/unknown"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 404);
}

TEST_F(HandlerTest, UnsupportedItemMethodReturns405) {
    create_test_item();

    const auto request = make_request(
        Method::Post,
        "/items/1"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 405);
}

TEST_F(HandlerTest, IncrementRejectsWrongMethod) {
    create_test_item();

    const auto request = make_request(
        Method::Get,
        "/items/1/increment"
    );

    const auto response = handle_request(request, storage);

    EXPECT_EQ(response.status, 405);
}
