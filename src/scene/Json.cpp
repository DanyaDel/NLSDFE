#include "Json.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>

const JsonValue* JsonValue::get(const std::string& key) const {
    for (const auto& f : fields) if (f.first == key) return &f.second;
    return nullptr;
}

double JsonValue::num(const std::string& key, double fallback) const {
    const JsonValue* v = get(key);
    return (v && v->kind == Number) ? v->number : fallback;
}

std::string JsonValue::str(const std::string& key, const std::string& fallback) const {
    const JsonValue* v = get(key);
    return (v && v->kind == String) ? v->text : fallback;
}

namespace {
    struct Parser {
        const std::string& s;
        size_t i = 0;
        std::string error;

        void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
        bool fail(const char* what) { if (error.empty()) error = std::string(what) + " at offset " + std::to_string(i); return false; }

        bool string(std::string& out) {
            if (s[i] != '"') return fail("expected string");
            i++;
            while (i < s.size() && s[i] != '"') {
                char c = s[i++];
                if (c != '\\') { out += c; continue; }
                if (i >= s.size()) return fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("bad unicode escape");
                        unsigned code = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        // UTF-8 (суррогатные пары не нужны для имён объектов)
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
                        else { out += (char)(0xE0 | (code >> 12)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
                        break;
                    }
                    default: out += e; break;
                }
            }
            if (i >= s.size()) return fail("unterminated string");
            i++;
            return true;
        }

        bool value(JsonValue& v, int depth) {
            if (depth > 64) return fail("nesting too deep");
            ws();
            if (i >= s.size()) return fail("unexpected end");
            char c = s[i];
            if (c == '{') {
                v.kind = JsonValue::Object;
                i++; ws();
                if (i < s.size() && s[i] == '}') { i++; return true; }
                while (true) {
                    ws();
                    std::string key;
                    if (i >= s.size() || !string(key)) return fail("expected key");
                    ws();
                    if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                    i++;
                    JsonValue item;
                    if (!value(item, depth + 1)) return false;
                    v.fields.emplace_back(key, std::move(item));
                    ws();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == '}') { i++; return true; }
                    return fail("expected ',' or '}'");
                }
            }
            if (c == '[') {
                v.kind = JsonValue::Array;
                i++; ws();
                if (i < s.size() && s[i] == ']') { i++; return true; }
                while (true) {
                    JsonValue item;
                    if (!value(item, depth + 1)) return false;
                    v.items.push_back(std::move(item));
                    ws();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == ']') { i++; return true; }
                    return fail("expected ',' or ']'");
                }
            }
            if (c == '"') { v.kind = JsonValue::String; return string(v.text); }
            if (s.compare(i, 4, "true") == 0) { v.kind = JsonValue::Bool; v.boolean = true; i += 4; return true; }
            if (s.compare(i, 5, "false") == 0) { v.kind = JsonValue::Bool; v.boolean = false; i += 5; return true; }
            if (s.compare(i, 4, "null") == 0) { v.kind = JsonValue::Null; i += 4; return true; }
            if (c == '-' || std::isdigit((unsigned char)c)) {
                char* end = nullptr;
                v.kind = JsonValue::Number;
                v.number = std::strtod(s.c_str() + i, &end);
                if (end == s.c_str() + i) return fail("bad number");
                i = end - s.c_str();
                return true;
            }
            return fail("unexpected character");
        }
    };
}

bool parseJson(const std::string& text, JsonValue& out, std::string* error) {
    Parser p{text};
    out = JsonValue();
    bool ok = p.value(out, 0);
    if (ok) { p.ws(); if (p.i != text.size()) ok = p.fail("trailing characters"); }
    if (!ok && error) *error = p.error;
    return ok;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += (char)c;
    }
    return out + "\"";
}
