#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "Types.hpp"

class Serializer {
public:
    static void write_string(std::ostream& os, const std::string& s) {
        size_t size = s.size();
        os.write(reinterpret_cast<const char*>(&size), sizeof(size));
        os.write(s.data(), size);
    }

    static std::string read_string(std::istream& is) {
        size_t size;
        is.read(reinterpret_cast<char*>(&size), sizeof(size));
        std::string s(size, ' ');
        is.read(s.data(), size);
        return s;
    }

    static void write_object(std::ostream& os, const Object& obj) {
        int type = obj.data.index();
        os.write(reinterpret_cast<char*>(&type), sizeof(type));

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, long long> || std::is_same_v<T, double>) {
                os.write(reinterpret_cast<const char*>(&arg), sizeof(arg));
            } else if constexpr (std::is_same_v<T, std::string>) {
                write_string(os, arg);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectList>>) {
                size_t s = arg->size();
                os.write(reinterpret_cast<char*>(&s), sizeof(s));
                for (const auto& item : *arg) write_object(os, item);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectMap>>) {
                size_t s = arg->size();
                os.write(reinterpret_cast<char*>(&s), sizeof(s));
                for (const auto& [k, v] : *arg) {
                    write_string(os, k);
                    write_object(os, v);
                }
            }
        }, obj.data);
    }

    static Object read_object(std::istream& is) {
        int type;
        is.read(reinterpret_cast<char*>(&type), sizeof(type));
        if (type == 1) { long long v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return Object(v); }
        if (type == 2) { double v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return Object(v); }
        if (type == 3) { return Object(read_string(is)); }
        if (type == 4) {
            size_t s; is.read(reinterpret_cast<char*>(&s), sizeof(s));
            auto l = std::make_unique<ObjectList>();
            for(size_t i=0; i<s; ++i) l->push_back(read_object(is));
            return Object(std::move(l));
        }
        if (type == 5) {
            size_t s; is.read(reinterpret_cast<char*>(&s), sizeof(s));
            auto m = std::make_unique<ObjectMap>();
            for(size_t i=0; i<s; ++i) {
                std::string k = read_string(is);
                m->emplace(std::move(k), read_object(is));
            }
            return Object(std::move(m));
        }
        return Object();
    }
};