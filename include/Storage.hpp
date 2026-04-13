#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <chrono>
#include <regex>
#include <algorithm>
#include <sstream>

struct Object;
using ObjectMap = std::unordered_map<std::string, Object>;
using ObjectList = std::vector<Object>;

using ObjectVariant = std::variant<
    std::monostate,
    long long,
    double,
    std::string,
    std::unique_ptr<ObjectList>,
    std::unique_ptr<ObjectMap>
>;

struct Object {
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

class Storage {
private:
    std::unordered_map<std::string, Entry> db;
    std::unordered_map<std::string, std::string> formulas;
    std::unordered_map<std::string, std::vector<std::string>> subscribers;
    mutable std::shared_mutex mtx;

    // --- Математический парсер (Рекурсивный спуск) ---
    struct MathEngine {
        std::string expr;
        size_t pos = 0;

        double parseExpression() {
            double x = parseTerm();
            for (;;) {
                if (eat('+')) x += parseTerm();
                else if (eat('-')) x -= parseTerm();
                else return x;
            }
        }

        double parseTerm() {
            double x = parseFactor();
            for (;;) {
                if (eat('*')) x *= parseFactor();
                else if (eat('/')) {
                    double d = parseFactor();
                    x = (d != 0) ? x / d : 0;
                } else return x;
            }
        }

        double parseFactor() {
            if (eat('(')) { double x = parseExpression(); eat(')'); return x; }
            size_t start = pos;
            if ((expr[pos] >= '0' && expr[pos] <= '9') || expr[pos] == '.' || expr[pos] == '-') {
                if (expr[pos] == '-') pos++;
                while (pos < expr.size() && ((expr[pos] >= '0' && expr[pos] <= '9') || expr[pos] == '.')) pos++;
                return std::stod(expr.substr(start, pos - start));
            }
            return 0;
        }

        bool eat(char c) {
            while (pos < expr.size() && std::isspace(expr[pos])) pos++;
            if (pos < expr.size() && expr[pos] == c) { pos++; return true; }
            return false;
        }
    };

    double evaluate(const std::string& formula) {
        std::string proc = formula;
        std::regex key_rx("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");

        // Находим все ключи в формуле и подставляем их значения
        auto words_begin = std::sregex_iterator(formula.begin(), formula.end(), key_rx);
        std::vector<std::pair<std::string, std::string>> reps;

        for (auto i = words_begin; i != std::sregex_iterator(); ++i) {
            std::string key = i->str();
            double val = 0;
            if (db.contains(key)) {
                std::visit([&val](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, long long> || std::is_same_v<T, double>) val = (double)arg;
                }, db[key].value.data);
            }
            reps.push_back({key, std::to_string(val)});
        }

        for (const auto& r : reps) proc = std::regex_replace(proc, std::regex("\\b" + r.first + "\\b"), r.second);

        try { return MathEngine{proc, 0}.parseExpression(); } catch(...) { return 0; }
    }

    void set_internal(const std::string& key, Object val, std::optional<int> ttl, int depth) {
        if (depth > 16) return; // Защита от циклов

        std::optional<std::chrono::system_clock::time_point> exp;
        if (ttl && *ttl > 0) exp = std::chrono::system_clock::now() + std::chrono::seconds(*ttl);

        db[key] = Entry{std::move(val), exp};

        if (subscribers.contains(key)) {
            for (const auto& t : subscribers[key]) {
                set_internal(t, Object(evaluate(formulas[t])), std::nullopt, depth + 1);
            }
        }
    }

public:
    void set(const std::string& key, Object val, std::optional<int> ttl = std::nullopt) {
        std::unique_lock lock(mtx);
        set_internal(key, std::move(val), ttl, 0);
    }

    void bond(const std::string& target, const std::string& formula) {
        std::unique_lock lock(mtx);
        formulas[target] = formula;
        std::regex key_rx("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");
        for (std::sregex_iterator i(formula.begin(), formula.end(), key_rx); i != std::sregex_iterator(); ++i) {
            if (std::find(subscribers[i->str()].begin(), subscribers[i->str()].end(), target) == subscribers[i->str()].end())
                subscribers[i->str()].push_back(target);
        }
        set_internal(target, Object(evaluate(formula)), std::nullopt, 0);
    }

    std::string get_as_string(const std::string& key) const {
        std::shared_lock lock(mtx);
        if (!db.contains(key)) return "(nil)";
        auto& e = db.at(key);
        if (e.expires_at && e.expires_at < std::chrono::system_clock::now()) return "(nil)";
        return serialize(e.value);
    }

    std::string serialize(const Object& obj) const {
        return std::visit([this](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "null";
            else if constexpr (std::is_same_v<T, long long> || std::is_same_v<T, double>) return std::to_string(arg);
            else if constexpr (std::is_same_v<T, std::string>) return "\"" + arg + "\"";
            else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectList>>) {
                std::string res = "[";
                for (size_t i = 0; i < arg->size(); ++i) res += serialize((*arg)[i]) + (i < arg->size()-1 ? ", " : "");
                return res + "]";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectMap>>) {
                std::string res = "{"; size_t i = 0;
                for (const auto& [k, v] : *arg) res += "\"" + k + "\": " + serialize(v) + (++i < arg->size() ? ", " : "");
                return res + "}";
            }
            return "unknown";
        }, obj.data);
    }

    void del(const std::string& k) { std::unique_lock l(mtx); db.erase(k); }
    void cleanup() {
        std::unique_lock l(mtx);
        auto now = std::chrono::system_clock::now();
        for (auto it = db.begin(); it != db.end();) {
            if (it->second.expires_at && it->second.expires_at < now) it = db.erase(it); else ++it;
        }
    }
    std::vector<std::string> get_keys() const {
        std::shared_lock l(mtx);
        std::vector<std::string> ks;
        for (auto const& [k, e] : db) ks.push_back(k);
        return ks;
    }
};