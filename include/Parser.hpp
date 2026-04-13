#pragma once
#include "Storage.hpp"

class CommandParser {
private:
    static Object parseValue(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace(s[pos])) pos++;
        if (pos >= s.size()) return Object();
        if (s[pos] == '[') {
            pos++; auto l = std::make_unique<ObjectList>();
            while (pos < s.size() && s[pos] != ']') {
                l->push_back(parseValue(s, pos));
                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ',')) pos++;
            }
            if (pos < s.size()) pos++; return Object(std::move(l));
        }
        if (s[pos] == '{') {
            pos++; auto m = std::make_unique<ObjectMap>();
            while (pos < s.size() && s[pos] != '}') {
                Object k_obj = parseValue(s, pos);
                std::string k = std::visit([](auto&& a) -> std::string {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, std::string>) {
                        return a;
                    } else {
                        return "key";
                    }
                }, k_obj.data);
                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ':')) pos++;
                (*m)[k] = parseValue(s, pos);
                while (pos < s.size() && (std::isspace(s[pos]) || s[pos] == ',')) pos++;
            }
            if (pos < s.size()) pos++; return Object(std::move(m));
        }
        if (s[pos] == '"') {
            pos++; size_t start = pos;
            while (pos < s.size() && s[pos] != '"') pos++;
            std::string v = s.substr(start, pos - start);
            if (pos < s.size()) pos++; return Object(v);
        }
        size_t start = pos;
        while (pos < s.size() && !std::isspace(s[pos]) && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != ':') pos++;
        std::string part = s.substr(start, pos - start);
        if (part.empty()) return Object();
        if (std::isdigit(part[0]) || (part.size() > 1 && part[0] == '-')) {
            try { return (part.find('.') != std::string::npos) ? Object(std::stod(part)) : Object(std::stoll(part)); } catch(...) {}
        }
        return Object(part);
    }

public:
    static std::string execute(Storage& store, const std::string& input) {
        std::vector<std::string> tokens;
        std::string cur; bool q = false; int br = 0;
        for (char c : input) {
            if (c == '"') q = !q;
            if (!q) { if (c == '[' || c == '{') br++; if (c == ']' || c == '}') br--; }
            if (std::isspace(c) && !q && br == 0) { if (!cur.empty()) { tokens.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) tokens.push_back(cur);
        if (tokens.empty()) return "ERR: Empty";

        std::string cmd = tokens[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

        if (cmd == "SET" && tokens.size() >= 3) {
            std::optional<int> ttl;
            if (tokens.size() >= 5 && tokens[3] == "EX") ttl = std::stoi(tokens[4]);
            size_t p = 0; store.set(tokens[1], parseValue(tokens[2], p), ttl);
            return "OK";
        }
        if (cmd == "GET" && tokens.size() >= 2) return store.get_as_string(tokens[1]);
        if (cmd == "BOND" && tokens.size() >= 3) {
            std::string f = tokens[2];
            if (f.front() == '"') f = f.substr(1, f.size()-2);
            store.bond(tokens[1], f); return "OK";
        }
        if (cmd == "KEYS") {
            auto ks = store.get_keys(); std::string r = "[";
            for(size_t i=0; i<ks.size(); ++i) r += "\"" + ks[i] + "\"" + (i < ks.size()-1 ? "," : "");
            return r + "]";
        }
        if (cmd == "DEL") { store.del(tokens[1]); return "OK"; }
        return "ERR: Unknown";
    }
};