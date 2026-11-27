#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace http {

// Объект, хранящийся в сервере.
struct Item {
    std::uint64_t id = 0;
    std::string name;
    std::int64_t value = 0;
    std::uint64_t version = 1;
};

// Результат операции над хранилищем.
enum class StorageStatus {
    Ok,
    NotFound,
    VersionConflict,
    ValueOverflow
};

// Результат операции, возвращающей Item.
struct StorageResult {
    StorageStatus status = StorageStatus::Ok;
    std::optional<Item> item;
};

// Хранилище объектов Item.
class Storage {
public:
    Storage() = default;

    // Запрещаем копирование хранилища.
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // Создание объекта. ID выдаётся сервером.
    Item create(const std::string& name, std::int64_t value);

    // Получение объекта по ID.
    std::optional<Item> get(std::uint64_t id) const;

    // Получение списка объектов с пагинацией.
    std::vector<Item> get_all(
        std::size_t offset,
        std::size_t limit
    ) const;

    // Полная замена объекта.
    StorageResult update(
        std::uint64_t id,
        const std::string& name,
        std::int64_t value,
        std::optional<std::uint64_t> expected_version = std::nullopt
    );

    // Частичное изменение объекта.
    StorageResult patch(
        std::uint64_t id,
        std::optional<std::string> name,
        std::optional<std::int64_t> value,
        std::optional<std::uint64_t> expected_version = std::nullopt
    );

    // Удаление объекта.
    StorageStatus remove(std::uint64_t id);

    // Атомарное увеличение value.
    StorageResult increment(
        std::uint64_t id,
        std::int64_t delta
    );

private:
    // Все объекты хранятся по ID.
    std::unordered_map<std::uint64_t, Item> items_;

    // Следующий свободный ID.
    std::uint64_t next_id_ = 1;

    // Несколько потоков могут одновременно читать хранилище.
    // Изменение данных допускается только одним потоком.
    mutable std::shared_mutex mutex_;
};

} // namespace http