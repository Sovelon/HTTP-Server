#include "handler.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <exception>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace http {
namespace {

using json = nlohmann::json;

// --------------------------------------------------
// JSON HELPERS
// --------------------------------------------------

Response error_response(int status, const std::string& message) {
    return json_response(
        status,
        json{{"error", message}}.dump()
    );
}

json item_to_json(const Item& item) {
    return {
        {"id", item.id},
        {"name", item.name},
        {"value", item.value},
        {"version", item.version}
    };
}

Response item_response(int status, const Item& item) {
    return json_response(status, item_to_json(item).dump());
}

// --------------------------------------------------
// PARSING HELPERS
// --------------------------------------------------

bool parse_uint64(std::string_view text, std::uint64_t& result) {
    if (text.empty()) {
        return false;
    }

    const auto [ptr, ec] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        result
    );

    return ec == std::errc{} && ptr == text.data() + text.size();
}

bool parse_size(std::string_view text, std::size_t& result) {
    if (text.empty()) {
        return false;
    }

    const auto [ptr, ec] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        result
    );

    return ec == std::errc{} && ptr == text.data() + text.size();
}

bool get_int64(
    const json& object,
    const char* key,
    std::int64_t& result
) {
    if (!object.contains(key) || !object[key].is_number_integer()) {
        return false;
    }

    try {
        const auto& value = object[key];

        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();

            if (number > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return false;
            }

            result = static_cast<std::int64_t>(number);
        } else {
            result = value.get<std::int64_t>();
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool get_version(
    const json& object,
    std::optional<std::uint64_t>& version
) {
    if (!object.contains("version")) {
        version = std::nullopt;
        return true;
    }

    const auto& value = object["version"];

    if (!value.is_number_integer()) {
        return false;
    }

    try {
        if (value.is_number_unsigned()) {
            version = value.get<std::uint64_t>();
        } else {
            const auto signed_value = value.get<std::int64_t>();

            if (signed_value < 0) {
                return false;
            }

            version = static_cast<std::uint64_t>(signed_value);
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool parse_json_body(const Request& request, json& body) {
    try {
        body = json::parse(request.body);
        return body.is_object();
    } catch (const json::exception&) {
        return false;
    }
}

// --------------------------------------------------
// QUERY PARAMETERS
// --------------------------------------------------

bool parse_pagination(
    std::string_view query,
    std::size_t& offset,
    std::size_t& limit
) {
    offset = 0;
    limit = 100;

    bool offset_seen = false;
    bool limit_seen = false;

    while (!query.empty()) {
        const auto amp = query.find('&');

        const std::string_view part = query.substr(0, amp);
        const auto equal = part.find('=');

        if (equal == std::string_view::npos) {
            return false;
        }

        const std::string_view key = part.substr(0, equal);
        const std::string_view value = part.substr(equal + 1);

        if (key == "offset") {
            if (offset_seen || !parse_size(value, offset)) {
                return false;
            }
            offset_seen = true;
        } else if (key == "limit") {
            if (limit_seen || !parse_size(value, limit)) {
                return false;
            }
            limit_seen = true;
        } else {
            return false;
        }

        if (amp == std::string_view::npos) {
            break;
        }

        query.remove_prefix(amp + 1);
    }

    return limit > 0;
}

// --------------------------------------------------
// PATH PARSING
// --------------------------------------------------

enum class ItemRouteType {
    Invalid,
    Item,
    Increment
};

struct ItemRoute {
    ItemRouteType type = ItemRouteType::Invalid;
    std::uint64_t id = 0;
};

ItemRoute parse_item_route(std::string_view path) {
    constexpr std::string_view prefix = "/items/";

    if (!path.starts_with(prefix)) {
        return {};
    }

    path.remove_prefix(prefix.size());

    const auto slash = path.find('/');

    const std::string_view id_text = path.substr(0, slash);

    std::uint64_t id = 0;

    if (!parse_uint64(id_text, id) || id == 0) {
        return {};
    }

    if (slash == std::string_view::npos) {
        return {ItemRouteType::Item, id};
    }

    const std::string_view suffix = path.substr(slash + 1);

    if (suffix == "increment") {
        return {ItemRouteType::Increment, id};
    }

    return {};
}

// --------------------------------------------------
// STORAGE RESULT MAPPING
// --------------------------------------------------

Response storage_error(const StorageResult& result) {
    switch (result.status) {
        case StorageStatus::NotFound:
            return error_response(404, "item not found");

        case StorageStatus::VersionConflict:
            return error_response(409, "version conflict");

        case StorageStatus::ValueOverflow:
            return error_response(400, "value overflow");

        case StorageStatus::Ok:
            break;
    }

    return error_response(500, "internal server error");
}

// --------------------------------------------------
// GET /health
// --------------------------------------------------

Response handle_health(const Request& request) {
    if (request.method != Method::Get) {
        Response response = error_response(405, "method not allowed");
        response.extra_headers.emplace_back("Allow", "GET");
        return response;
    }

    return json_response(200, R"({"status":"ok"})");
}

// --------------------------------------------------
// GET /items
// --------------------------------------------------

Response handle_items_collection(
    const Request& request,
    Storage& storage
) {
    if (request.method == Method::Get) {
        std::size_t offset = 0;
        std::size_t limit = 100;

        if (!parse_pagination(request.query, offset, limit)) {
            return error_response(400, "invalid pagination parameters");
        }

        const auto items = storage.get_all(offset, limit);

        json result = json::array();

        for (const auto& item : items) {
            result.push_back(item_to_json(item));
        }

        return json_response(200, result.dump());
    }

    if (request.method == Method::Post) {
        json body;

        if (!parse_json_body(request, body)) {
            return error_response(400, "invalid JSON object");
        }

        if (!body.contains("name") || !body["name"].is_string()) {
            return error_response(400, "name must be a string");
        }

        std::int64_t value = 0;

        if (!get_int64(body, "value", value)) {
            return error_response(400, "value must be an integer");
        }

        const std::string name = body["name"].get<std::string>();

        const Item created = storage.create(name, value);

        return item_response(201, created);
    }

    Response response = error_response(405, "method not allowed");
    response.extra_headers.emplace_back("Allow", "GET, POST");

    return response;
}

// --------------------------------------------------
// GET /items/{id}
// --------------------------------------------------

Response handle_get_item(
    std::uint64_t id,
    Storage& storage
) {
    const auto item = storage.get(id);

    if (!item.has_value()) {
        return error_response(404, "item not found");
    }

    return item_response(200, *item);
}

// --------------------------------------------------
// PUT /items/{id}
// --------------------------------------------------

Response handle_put_item(
    const Request& request,
    std::uint64_t id,
    Storage& storage
) {
    json body;

    if (!parse_json_body(request, body)) {
        return error_response(400, "invalid JSON object");
    }

    if (!body.contains("name") || !body["name"].is_string()) {
        return error_response(400, "name must be a string");
    }

    std::int64_t value = 0;

    if (!get_int64(body, "value", value)) {
        return error_response(400, "value must be an integer");
    }

    std::optional<std::uint64_t> version;

    if (!get_version(body, version)) {
        return error_response(400, "invalid version");
    }

    const auto result = storage.update(
        id,
        body["name"].get<std::string>(),
        value,
        version
    );

    if (result.status != StorageStatus::Ok) {
        return storage_error(result);
    }

    return item_response(200, *result.item);
}

// --------------------------------------------------
// PATCH /items/{id}
// --------------------------------------------------

Response handle_patch_item(
    const Request& request,
    std::uint64_t id,
    Storage& storage
) {
    json body;

    if (!parse_json_body(request, body)) {
        return error_response(400, "invalid JSON object");
    }

    std::optional<std::string> name;
    std::optional<std::int64_t> value;
    std::optional<std::uint64_t> version;

    if (body.contains("name")) {
        if (!body["name"].is_string()) {
            return error_response(400, "name must be a string");
        }

        name = body["name"].get<std::string>();
    }

    if (body.contains("value")) {
        std::int64_t parsed_value = 0;

        if (!get_int64(body, "value", parsed_value)) {
            return error_response(400, "value must be an integer");
        }

        value = parsed_value;
    }

    if (!get_version(body, version)) {
        return error_response(400, "invalid version");
    }

    const auto result = storage.patch(
        id,
        name,
        value,
        version
    );

    if (result.status != StorageStatus::Ok) {
        return storage_error(result);
    }

    return item_response(200, *result.item);
}

// --------------------------------------------------
// DELETE /items/{id}
// --------------------------------------------------

Response handle_delete_item(
    std::uint64_t id,
    Storage& storage
) {
    const StorageStatus status = storage.remove(id);

    if (status == StorageStatus::NotFound) {
        return error_response(404, "item not found");
    }

    return json_response(204, "");
}

// --------------------------------------------------
// POST /items/{id}/increment
// --------------------------------------------------

Response handle_increment(
    const Request& request,
    std::uint64_t id,
    Storage& storage
) {
    json body;

    if (!parse_json_body(request, body)) {
        return error_response(400, "invalid JSON object");
    }

    std::int64_t delta = 0;

    if (!get_int64(body, "delta", delta)) {
        return error_response(400, "delta must be an integer");
    }

    const auto result = storage.increment(id, delta);

    if (result.status != StorageStatus::Ok) {
        return storage_error(result);
    }

    return item_response(200, *result.item);
}

} // namespace

// --------------------------------------------------
// MAIN ROUTER
// --------------------------------------------------

Response handle_request(
    const Request& request,
    Storage& storage
) {
    try {
        if (request.path == "/health") {
            return handle_health(request);
        }

        if (request.path == "/items") {
            return handle_items_collection(request, storage);
        }

        const ItemRoute route = parse_item_route(request.path);

        if (route.type == ItemRouteType::Invalid) {
            return error_response(404, "not found");
        }

        if (route.type == ItemRouteType::Increment) {
            if (request.method != Method::Post) {
                Response response =
                    error_response(405, "method not allowed");

                response.extra_headers.emplace_back("Allow", "POST");
                return response;
            }

            return handle_increment(request, route.id, storage);
        }

        switch (request.method) {
            case Method::Get:
                return handle_get_item(route.id, storage);

            case Method::Put:
                return handle_put_item(request, route.id, storage);

            case Method::Patch:
                return handle_patch_item(request, route.id, storage);

            case Method::Delete:
                return handle_delete_item(route.id, storage);

            default: {
                Response response =
                    error_response(405, "method not allowed");

                response.extra_headers.emplace_back(
                    "Allow",
                    "GET, PUT, PATCH, DELETE"
                );

                return response;
            }
        }
    } catch (const std::exception&) {
        return error_response(500, "internal server error");
    }
}

} // namespace http