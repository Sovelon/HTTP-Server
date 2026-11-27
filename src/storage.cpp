
#include "storage.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace http {

// --------------------------------------------------
// CREATE
// --------------------------------------------------

Item Storage::create(const std::string& name, std::int64_t value) {
    std::unique_lock lock(mutex_);

    if (next_id_ == 0) {
        throw std::overflow_error("Item ID overflow");
    }

    Item item;
    item.id = next_id_++;
    item.name = name;
    item.value = value;
    item.version = 1;

    items_.emplace(item.id, item);

    return item;
}

// --------------------------------------------------
// GET
// --------------------------------------------------

std::optional<Item> Storage::get(std::uint64_t id) const {
    std::shared_lock lock(mutex_);

    const auto it = items_.find(id);

    if (it == items_.end()) {
        return std::nullopt;
    }

    return it->second;
}

// --------------------------------------------------
// GET ALL
// --------------------------------------------------

std::vector<Item> Storage::get_all(
    std::size_t offset,
    std::size_t limit
) const {
    std::shared_lock lock(mutex_);

    std::vector<Item> result;
    result.reserve(items_.size());

    for (const auto& [id, item] : items_) {
        (void)id;
        result.push_back(item);
    }

    // unordered_map не гарантирует порядок.
    // Сортируем по ID для стабильного результата.
    std::sort(
        result.begin(),
        result.end(),
        [](const Item& a, const Item& b) {
            return a.id < b.id;
        }
    );

    if (offset >= result.size() || limit == 0) {
        return {};
    }

    const std::size_t end =
        offset + std::min(limit, result.size() - offset);

    return std::vector<Item>(
        result.begin() + static_cast<std::ptrdiff_t>(offset),
        result.begin() + static_cast<std::ptrdiff_t>(end)
    );
}

// --------------------------------------------------
// UPDATE
// --------------------------------------------------

StorageResult Storage::update(
    std::uint64_t id,
    const std::string& name,
    std::int64_t value,
    std::optional<std::uint64_t> expected_version
) {
    std::unique_lock lock(mutex_);

    const auto it = items_.find(id);

    if (it == items_.end()) {
        return {StorageStatus::NotFound, std::nullopt};
    }

    Item& item = it->second;

    // Проверяем optimistic locking.
    if (expected_version.has_value() &&
        expected_version.value() != item.version) {
        return {StorageStatus::VersionConflict, std::nullopt};
    }

    item.name = name;
    item.value = value;
    ++item.version;

    return {StorageStatus::Ok, item};
}

// --------------------------------------------------
// PATCH
// --------------------------------------------------

StorageResult Storage::patch(
    std::uint64_t id,
    std::optional<std::string> name,
    std::optional<std::int64_t> value,
    std::optional<std::uint64_t> expected_version
) {
    std::unique_lock lock(mutex_);

    const auto it = items_.find(id);

    if (it == items_.end()) {
        return {StorageStatus::NotFound, std::nullopt};
    }

    Item& item = it->second;

    if (expected_version.has_value() &&
        expected_version.value() != item.version) {
        return {StorageStatus::VersionConflict, std::nullopt};
    }

    // Меняем только переданные поля.
    const bool has_changes = name.has_value() || value.has_value();

    if (name.has_value()) {
        item.name = std::move(name.value());
    }

    if (value.has_value()) {
        item.value = value.value();
    }

    // Пустой PATCH не изменяет версию.
    if (has_changes) {
        ++item.version;
    }

    return {StorageStatus::Ok, item};
}

// --------------------------------------------------
// DELETE
// --------------------------------------------------

StorageStatus Storage::remove(std::uint64_t id) {
    std::unique_lock lock(mutex_);

    const auto it = items_.find(id);

    if (it == items_.end()) {
        return StorageStatus::NotFound;
    }

    items_.erase(it);

    return StorageStatus::Ok;
}

// --------------------------------------------------
// INCREMENT
// --------------------------------------------------

StorageResult Storage::increment(
    std::uint64_t id,
    std::int64_t delta
) {
    std::unique_lock lock(mutex_);

    const auto it = items_.find(id);

    if (it == items_.end()) {
        return {StorageStatus::NotFound, std::nullopt};
    }

    Item& item = it->second;

    // Проверяем переполнение signed int64_t.
    if ((delta > 0 &&
         item.value > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 &&
         item.value < std::numeric_limits<std::int64_t>::min() - delta)) {
        return {StorageStatus::ValueOverflow, std::nullopt};
    }

    item.value += delta;
    ++item.version;

    return {StorageStatus::Ok, item};
}

} // namespace http