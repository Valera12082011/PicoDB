#pragma once
#include <shared_mutex>
#include <regex>
#include <zstd.h>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <algorithm>
#include <mutex>
#include "Types.hpp"
#include "MathEngine.hpp"
#include "Serializer.hpp"

class Storage {
private:
    std::unordered_map<std::string, Entry> db;
    std::unordered_map<std::string, std::string> formulas;
    std::unordered_map<std::string, std::vector<std::string>> subscribers;
    mutable std::shared_mutex mtx;

    double evaluate(const std::string& formula) {
        std::string proc = formula;
        std::regex key_rx("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");
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
        if (depth > 16) return;

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
    void lpush(const std::string& key, Object val) {
        std::unique_lock lock(mtx);
        auto& entry = db[key];
        if (entry.value.data.index() == 0) { // monostate
            entry.value.data = std::make_unique<ObjectList>();
        }
        if (auto* list_ptr = std::get_if<std::unique_ptr<ObjectList>>(&entry.value.data)) {
            (*list_ptr)->insert((*list_ptr)->begin(), std::move(val));
        }
    }

    void hset(const std::string& key, const std::string& field, Object val) {
        std::unique_lock lock(mtx);
        auto& entry = db[key];
        if (entry.value.data.index() == 0) {
            entry.value.data = std::make_unique<ObjectMap>();
        }
        if (auto* map_ptr = std::get_if<std::unique_ptr<ObjectMap>>(&entry.value.data)) {
            (**map_ptr)[field] = std::move(val);
        }
    }

    void bond(const std::string& target, const std::string& formula) {
        std::unique_lock lock(mtx);
        formulas[target] = formula;
        std::regex key_rx("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");
        for (std::sregex_iterator i(formula.begin(), formula.end(), key_rx); i != std::sregex_iterator(); ++i) {
            std::string dep = i->str();
            if (std::find(subscribers[dep].begin(), subscribers[dep].end(), target) == subscribers[dep].end())
                subscribers[dep].push_back(target);
        }
        set_internal(target, Object(evaluate(formula)), std::nullopt, 0);
    }

    std::string serialize_to_string(const Object& obj) const {
        return std::visit([this](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "null";
            else if constexpr (std::is_same_v<T, long long> || std::is_same_v<T, double>) return std::to_string(arg);
            else if constexpr (std::is_same_v<T, std::string>) return "\"" + arg + "\"";
            else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectList>>) {
                std::string res = "[";
                for (size_t i = 0; i < arg->size(); ++i) res += serialize_to_string((*arg)[i]) + (i < arg->size()-1 ? "," : "");
                return res + "]";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectMap>>) {
                std::string res = "{"; size_t i = 0;
                for (const auto& [k, v] : *arg) res += "\"" + k + "\":" + serialize_to_string(v) + (++i < arg->size() ? "," : "");
                return res + "}";
            }
            return "unknown";
        }, obj.data);
    }

    std::string get_as_string(const std::string& key) const {
        std::shared_lock lock(mtx);
        if (!db.contains(key)) return "(nil)";
        auto& e = db.at(key);
        if (e.expires_at && e.expires_at < std::chrono::system_clock::now()) return "(nil)";
        return serialize_to_string(e.value);
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

    bool save_to_file(const std::string& path) {
        std::shared_lock lock(mtx);
        std::stringstream ss(std::ios::binary | std::ios::out);
        size_t dbs = db.size();
        ss.write(reinterpret_cast<char*>(&dbs), sizeof(dbs));
        for (const auto& [k, e] : db) {
            Serializer::write_string(ss, k);
            Serializer::write_object(ss, e.value);
            bool has_exp = e.expires_at.has_value();
            ss.write(reinterpret_cast<char*>(&has_exp), sizeof(has_exp));
            if (has_exp) {
                auto tp = e.expires_at->time_since_epoch().count();
                ss.write(reinterpret_cast<char*>(&tp), sizeof(tp));
            }
        }
        size_t fs = formulas.size();
        ss.write(reinterpret_cast<char*>(&fs), sizeof(fs));
        for (const auto& [k, f] : formulas) {
            Serializer::write_string(ss, k);
            Serializer::write_string(ss, f);
        }
        size_t subs = subscribers.size();
        ss.write(reinterpret_cast<char*>(&subs), sizeof(subs));
        for (const auto& [k, vlist] : subscribers) {
            Serializer::write_string(ss, k);
            size_t vs = vlist.size();
            ss.write(reinterpret_cast<char*>(&vs), sizeof(vs));
            for (const auto& s : vlist) Serializer::write_string(ss, s);
        }
        std::string raw = ss.str();
        size_t const max_c = ZSTD_compressBound(raw.size());
        std::vector<char> comp(max_c);
        size_t const c_size = ZSTD_compress(comp.data(), max_c, raw.data(), raw.size(), 3);
        if (ZSTD_isError(c_size)) return false;
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs.write(comp.data(), c_size);
        return true;
    }

    bool load_from_file(const std::string& path) {
        std::unique_lock lock(mtx);
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs) return false;
        size_t sz = ifs.tellg();
        ifs.seekg(0);
        std::vector<char> comp(sz);
        ifs.read(comp.data(), sz);
        unsigned long long const r_sz = ZSTD_getFrameContentSize(comp.data(), sz);
        if (r_sz == ZSTD_CONTENTSIZE_ERROR) return false;
        std::string raw(r_sz, ' ');
        ZSTD_decompress(raw.data(), r_sz, comp.data(), sz);
        std::stringstream ss(raw, std::ios::binary | std::ios::in);
        db.clear(); formulas.clear(); subscribers.clear();
        size_t dbs; ss.read(reinterpret_cast<char*>(&dbs), sizeof(dbs));
        for(size_t i = 0; i < dbs; ++i) {
            std::string k = Serializer::read_string(ss);
            Object obj = Serializer::read_object(ss);
            bool has_exp; ss.read(reinterpret_cast<char*>(&has_exp), sizeof(has_exp));
            std::optional<std::chrono::system_clock::time_point> exp;
            if (has_exp) {
                long long tp; ss.read(reinterpret_cast<char*>(&tp), sizeof(tp));
                exp = std::chrono::system_clock::time_point(std::chrono::system_clock::duration(tp));
            }
            db[k] = Entry{std::move(obj), exp};
        }
        size_t fs; ss.read(reinterpret_cast<char*>(&fs), sizeof(fs));
        for(size_t i = 0; i < fs; ++i) {
            formulas[Serializer::read_string(ss)] = Serializer::read_string(ss);
        }
        size_t subs; ss.read(reinterpret_cast<char*>(&subs), sizeof(subs));
        for(size_t i = 0; i < subs; ++i) {
            std::string k = Serializer::read_string(ss);
            size_t vs; ss.read(reinterpret_cast<char*>(&vs), sizeof(vs));
            for(size_t j = 0; j < vs; ++j) subscribers[k].push_back(Serializer::read_string(ss));
        }
        return true;
    }
};