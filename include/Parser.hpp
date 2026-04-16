#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <optional>
#include "Storage.hpp"

class CommandParser {
private:
    // Рекурсивный парсер значений (понимает 1, "str", [1,2], {"a":1})
    static Object parseValue(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace(s[pos])) pos++;
        if (pos >= s.size()) return Object();

        // Парсинг Списка [v1, v2]
        if (s[pos] == '[') {
            pos++; auto l = std::make_unique<ObjectList>();
            while (pos < s.size() && s[pos] != ']') {
                l->push_back(parseValue(s, pos));
                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ',')) pos++;
            }
            if (pos < s.size()) pos++;
            return Object(std::move(l));
        }

        // Парсинг Мапы {"k": v}
        if (s[pos] == '{') {
            pos++; auto m = std::make_unique<ObjectMap>();
            while (pos < s.size() && s[pos] != '}') {
                Object k_obj = parseValue(s, pos);
                std::string k = std::visit([](auto&& a) -> std::string {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, std::string>) return a;
                    else if constexpr (std::is_same_v<T, long long>) return std::to_string(a);
                    else return "key";
                }, k_obj.data);

                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ':')) pos++;
                (*m)[k] = parseValue(s, pos);
                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ',')) pos++;
            }
            if (pos < s.size()) pos++;
            return Object(std::move(m));
        }

        // Парсинг Строки в кавычках "text"
        if (s[pos] == '"') {
            pos++; size_t start = pos;
            while (pos < s.size() && s[pos] != '"') pos++;
            std::string v = s.substr(start, pos - start);
            if (pos < s.size()) pos++;
            return Object(v);
        }

        // Парсинг Числа или простого слова
        size_t start = pos;
        while (pos < s.size() && !std::isspace(s[pos]) && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != ':') pos++;
        std::string part = s.substr(start, pos - start);
        if (part.empty()) return Object();

        if (std::isdigit(part[0]) || (part.size() > 1 && part[0] == '-')) {
            try {
                if (part.find('.') != std::string::npos) return Object(std::stod(part));
                return Object(std::stoll(part));
            } catch(...) {}
        }
        return Object(part);
    }

    static std::string sanitize(std::string s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
        return s;
    }

public:
    static std::string execute(Storage& store, const std::string& input) {
        std::vector<std::string> tokens;
        std::string cur;
        bool in_quotes = false;
        int brace_level = 0;

        // Умное разбиение на токены (не режет внутри "" и [])
        for (char c : input) {
            if (c == '"') in_quotes = !in_quotes;
            if (!in_quotes) {
                if (c == '[' || c == '{') brace_level++;
                if (c == ']' || c == '}') brace_level--;
            }
            if (std::isspace(c) && !in_quotes && brace_level == 0) {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) tokens.push_back(cur);
        if (tokens.empty()) return "ERR: Empty command";

        std::string cmd = tokens[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

        try {
            // SET key value [EX seconds]
            if (cmd == "SET" && tokens.size() >= 3) {
                std::optional<int> ttl;
                if (tokens.size() >= 5 && (tokens[3] == "EX" || tokens[3] == "ex")) ttl = std::stoi(tokens[4]);
                size_t p = 0;
                store.set(tokens[1], parseValue(tokens[2], p), ttl);
                return "OK";
            }

            // GET key
            if (cmd == "GET" && tokens.size() >= 2) return store.get_as_string(tokens[1]);

            // LPUSH key value
            if (cmd == "LPUSH" && tokens.size() >= 3) {
                size_t p = 0;
                store.lpush(tokens[1], parseValue(tokens[2], p));
                return "OK";
            }

            // HSET key field value
            if (cmd == "HSET" && tokens.size() >= 4) {
                size_t p = 0;
                store.hset(tokens[1], tokens[2], parseValue(tokens[3], p));
                return "OK";
            }

            // BOND key formula
            if (cmd == "BOND" && tokens.size() >= 3) {
                store.bond(tokens[1], sanitize(tokens[2]));
                return "OK";
            }

            if (cmd == "DEL" && tokens.size() >= 2) { store.del(tokens[1]); return "OK"; }

            if (cmd == "SAVE") return store.save_to_file(tokens.size() > 1 ? sanitize(tokens[1]) : "data/dump.pico") ? "OK" : "ERR: Save error";

            if (cmd == "LOAD") return store.load_from_file(tokens.size() > 1 ? sanitize(tokens[1]) : "data/dump.pico") ? "OK" : "ERR: Load error";

            if (cmd == "KEYS") {
                auto ks = store.get_keys();
                std::string res = "[";
                for(size_t i=0; i<ks.size(); ++i) res += "\"" + ks[i] + "\"" + (i < ks.size()-1 ? "," : "");
                return res + "]";
            }

            if (cmd == "PING") return "PONG";

        } catch (const std::exception& e) {
            return std::string("ERR: ") + e.what();
        }

        return "ERR: Unknown command '" + cmd + "'";
    }
};