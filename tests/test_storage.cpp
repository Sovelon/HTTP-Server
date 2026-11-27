
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "storage.hpp"

using namespace http;

// --------------------------------------------------
// CREATE
// --------------------------------------------------

TEST(Storage, CreateItem) {
    Storage storage;

    const Item item = storage.create("Apple", 100);

    EXPECT_EQ(item.id, 1);
    EXPECT_EQ(item.name, "Apple");
    EXPECT_EQ(item.value, 100);
    EXPECT_EQ(item.version, 1);
}

TEST(Storage, IDsAreUniqueAndMonotonic) {
    Storage storage;

    const Item first = storage.create("First", 10);
    const Item second = storage.create("Second", 20);
    const Item third = storage.create("Third", 30);

    EXPECT_EQ(first.id, 1);
    EXPECT_EQ(second.id, 2);
    EXPECT_EQ(third.id, 3);
}

// --------------------------------------------------
// GET
// --------------------------------------------------

TEST(Storage, GetExistingItem) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.get(created.id);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "Apple");
    EXPECT_EQ(result->value, 100);
}

TEST(Storage, GetNonexistentItem) {
    Storage storage;

    EXPECT_FALSE(storage.get(999).has_value());
}

// --------------------------------------------------
// GET ALL
// --------------------------------------------------

TEST(Storage, GetAllItems) {
    Storage storage;

    storage.create("First", 10);
    storage.create("Second", 20);
    storage.create("Third", 30);

    const auto items = storage.get_all(0, 100);

    ASSERT_EQ(items.size(), 3);
    EXPECT_EQ(items[0].id, 1);
    EXPECT_EQ(items[1].id, 2);
    EXPECT_EQ(items[2].id, 3);
}

TEST(Storage, Pagination) {
    Storage storage;

    storage.create("First", 10);
    storage.create("Second", 20);
    storage.create("Third", 30);
    storage.create("Fourth", 40);

    const auto items = storage.get_all(1, 2);

    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].id, 2);
    EXPECT_EQ(items[1].id, 3);
}

TEST(Storage, PaginationOutOfRange) {
    Storage storage;

    storage.create("First", 10);

    EXPECT_TRUE(storage.get_all(100, 10).empty());
    EXPECT_TRUE(storage.get_all(0, 0).empty());
}

// --------------------------------------------------
// UPDATE
// --------------------------------------------------

TEST(Storage, UpdateItem) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.update(
        created.id,
        "Orange",
        200
    );

    ASSERT_EQ(result.status, StorageStatus::Ok);
    ASSERT_TRUE(result.item.has_value());

    EXPECT_EQ(result.item->name, "Orange");
    EXPECT_EQ(result.item->value, 200);
    EXPECT_EQ(result.item->version, 2);
}

TEST(Storage, UpdateNonexistentItem) {
    Storage storage;

    const auto result = storage.update(999, "Apple", 100);

    EXPECT_EQ(result.status, StorageStatus::NotFound);
}

TEST(Storage, UpdateVersionConflict) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.update(
        created.id,
        "Orange",
        200,
        999
    );

    EXPECT_EQ(result.status, StorageStatus::VersionConflict);

    const auto current = storage.get(created.id);

    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->name, "Apple");
    EXPECT_EQ(current->version, 1);
}

TEST(Storage, UpdateWithCorrectVersion) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.update(
        created.id,
        "Orange",
        200,
        created.version
    );

    ASSERT_EQ(result.status, StorageStatus::Ok);
    EXPECT_EQ(result.item->version, 2);
}

// --------------------------------------------------
// PATCH
// --------------------------------------------------

TEST(Storage, PatchOnlyValue) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.patch(
        created.id,
        std::nullopt,
        200
    );

    ASSERT_EQ(result.status, StorageStatus::Ok);
    ASSERT_TRUE(result.item.has_value());

    EXPECT_EQ(result.item->name, "Apple");
    EXPECT_EQ(result.item->value, 200);
    EXPECT_EQ(result.item->version, 2);
}

TEST(Storage, PatchOnlyName) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.patch(
        created.id,
        std::string("Orange"),
        std::nullopt
    );

    ASSERT_EQ(result.status, StorageStatus::Ok);

    EXPECT_EQ(result.item->name, "Orange");
    EXPECT_EQ(result.item->value, 100);
}

TEST(Storage, PatchVersionConflict) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.patch(
        created.id,
        std::string("Orange"),
        std::nullopt,
        999
    );

    EXPECT_EQ(result.status, StorageStatus::VersionConflict);

    const auto current = storage.get(created.id);

    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->name, "Apple");
}

TEST(Storage, EmptyPatchDoesNotChangeVersion) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.patch(
        created.id,
        std::nullopt,
        std::nullopt
    );

    ASSERT_EQ(result.status, StorageStatus::Ok);
    EXPECT_EQ(result.item->version, 1);
}

// --------------------------------------------------
// DELETE
// --------------------------------------------------

TEST(Storage, RemoveItem) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    EXPECT_EQ(storage.remove(created.id), StorageStatus::Ok);
    EXPECT_FALSE(storage.get(created.id).has_value());
}

TEST(Storage, RemoveNonexistentItem) {
    Storage storage;

    EXPECT_EQ(storage.remove(999), StorageStatus::NotFound);
}

// --------------------------------------------------
// INCREMENT
// --------------------------------------------------

TEST(Storage, IncrementItem) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.increment(created.id, 5);

    ASSERT_EQ(result.status, StorageStatus::Ok);
    ASSERT_TRUE(result.item.has_value());

    EXPECT_EQ(result.item->value, 105);
    EXPECT_EQ(result.item->version, 2);
}

TEST(Storage, IncrementWithNegativeDelta) {
    Storage storage;

    const Item created = storage.create("Apple", 100);

    const auto result = storage.increment(created.id, -30);

    ASSERT_EQ(result.status, StorageStatus::Ok);
    EXPECT_EQ(result.item->value, 70);
}

TEST(Storage, IncrementNonexistentItem) {
    Storage storage;

    const auto result = storage.increment(999, 1);

    EXPECT_EQ(result.status, StorageStatus::NotFound);
}

TEST(Storage, IncrementOverflow) {
    Storage storage;

    const Item created = storage.create(
        "Apple",
        std::numeric_limits<std::int64_t>::max()
    );

    const auto result = storage.increment(created.id, 1);

    EXPECT_EQ(result.status, StorageStatus::ValueOverflow);
}

// --------------------------------------------------
// CONCURRENCY
// --------------------------------------------------

TEST(Storage, ConcurrentIncrement) {
    Storage storage;

    const Item created = storage.create("Apple", 0);

    constexpr int thread_count = 8;
    constexpr int increments_per_thread = 1000;

    std::vector<std::thread> threads;

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&storage, id = created.id]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                const auto result = storage.increment(id, 1);
                EXPECT_EQ(result.status, StorageStatus::Ok);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    const auto result = storage.get(created.id);

    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(
        result->value,
        thread_count * increments_per_thread
    );

    EXPECT_EQ(
        result->version,
        1 + thread_count * increments_per_thread
    );
}
