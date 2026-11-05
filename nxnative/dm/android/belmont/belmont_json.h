// A small JSON reader for the companion's manifest and the module's area table (kalasyan fork). Whole documents only, UTF-8 kept as bytes.
#pragma once
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bjson {
struct Value {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Value> a; std::vector<std::pair<std::string, Value>> o;   // objects keep their order
    const Value *get(std::string_view k) const { for (const auto &[n_, v] : o) if (n_ == k) return &v; return nullptr; }
    double num(std::string_view k, double d = 0) const { const Value *v = get(k); return v && v->kind == Num ? v->n : v && v->kind == Bool ? v->b : d; }
    std::string str(std::string_view k, std::string d = {}) const { const Value *v = get(k); return v && v->kind == Str ? v->s : d; }
    bool flag(std::string_view k, bool d = false) const { const Value *v = get(k); return v && v->kind == Bool ? v->b : v && v->kind == Num ? v->n != 0 : d; }
    std::vector<double> nums(std::string_view k) const { std::vector<double> r; if (const Value *v = get(k); v && v->kind == Arr) for (const auto &e : v->a) r.push_back(e.kind == Num ? e.n : 0); return r; }
};

class Parser {
    const char *p, *e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    static void utf8(std::string &out, unsigned c) {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xc0 | c >> 6); out += char(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += char(0xe0 | c >> 12); out += char(0x80 | (c >> 6 & 63)); out += char(0x80 | (c & 63)); }
        else { out += char(0xf0 | c >> 18); out += char(0x80 | (c >> 12 & 63)); out += char(0x80 | (c >> 6 & 63)); out += char(0x80 | (c & 63)); }
    }
    bool hex4(unsigned &c) {
        if (e - p < 4) return false; c = 0;
        for (int i = 0; i < 4; i++) { char h = *p++; c <<= 4; if (h >= '0' && h <= '9') c |= h - '0'; else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10; else return false; }
        return true;
    }
    bool string(std::string &out) {
        if (p >= e || *p != '"') return false; p++;
        while (p < e && *p != '"') {
            if (*p != '\\') { out += *p++; continue; }
            if (++p >= e) return false;
            char c = *p++;
            switch (c) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': { unsigned u; if (!hex4(u)) return false;
                if (u >= 0xd800 && u < 0xdc00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') { p += 2; unsigned l; if (!hex4(l)) return false; u = 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00); }
                utf8(out, u); break; }
            default: out += c;
            }
        }
        if (p >= e) return false; p++; return true;
    }
    bool value(Value &v, int depth) {
        if (depth > 64) return false;
        ws(); if (p >= e) return false;
        if (*p == '{') {
            v.kind = Value::Obj; p++; ws();
            if (p < e && *p == '}') { p++; return true; }
            for (;;) {
                ws(); std::string k; if (!string(k)) return false; ws();
                if (p >= e || *p++ != ':') return false;
                v.o.emplace_back(std::move(k), Value{}); if (!value(v.o.back().second, depth + 1)) return false;
                ws(); if (p >= e) return false; if (*p == ',') { p++; continue; } if (*p == '}') { p++; return true; } return false;
            }
        }
        if (*p == '[') {
            v.kind = Value::Arr; p++; ws();
            if (p < e && *p == ']') { p++; return true; }
            for (;;) {
                v.a.emplace_back(); if (!value(v.a.back(), depth + 1)) return false;
                ws(); if (p >= e) return false; if (*p == ',') { p++; continue; } if (*p == ']') { p++; return true; } return false;
            }
        }
        if (*p == '"') { v.kind = Value::Str; return string(v.s); }
        if (e - p >= 4 && std::string_view(p, 4) == "true") { v.kind = Value::Bool; v.b = true; p += 4; return true; }
        if (e - p >= 5 && std::string_view(p, 5) == "false") { v.kind = Value::Bool; p += 5; return true; }
        if (e - p >= 4 && std::string_view(p, 4) == "null") { p += 4; return true; }
        char *end = nullptr; std::string tmp(p, std::min<size_t>(64, e - p)); v.n = std::strtod(tmp.c_str(), &end);
        if (end == tmp.c_str()) return false;
        v.kind = Value::Num; p += end - tmp.c_str(); return true;
    }
public:
    static bool parse(std::string_view text, Value &out) { Parser q; q.p = text.data(); q.e = text.data() + text.size(); return q.value(out, 0); }
};
}   // namespace bjson
