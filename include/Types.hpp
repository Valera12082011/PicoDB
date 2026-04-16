#pragma once
#include <variant>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>


struct Object;

using ObjectList = std::vector<Object>;
using ObjectMap = std::unordered_map<std::string, Object>;

struct Object {
    using ObjectVariant = std::variant<
        std::monostate,              // 0
        long long,                   // 1
        double,                      // 2
        std::string,                 // 3
        std::unique_ptr<ObjectList>, // 4
        std::unique_ptr<ObjectMap>   // 5
    >;

    ObjectVariant data;

    Object() : data(std::monostate{}) {}
    Object(long long v) : data(v) {}
    Object(double v) : data(v) {}
    Object(std::string v) : data(std::move(v)) {}
    Object(std::unique_ptr<ObjectList> v) : data(std::move(v)) {}
    Object(std::unique_ptr<ObjectMap> v) : data(std::move(v)) {}

    Object(Object&&) noexcept = default;
    Object& operator=(Object&&) noexcept = default;
    Object(const Object&) = delete;
};

struct Entry {
    Object value;
    std::optional<std::chrono::system_clock::time_point> expires_at;
};