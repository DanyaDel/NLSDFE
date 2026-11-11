#pragma once
#include <string>
#include <vector>
#include <utility>

// Минимальный JSON: ровно столько, сколько нужно для обмена с внешними инструментами (Blender)
struct JsonValue {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> fields;

    const JsonValue* get(const std::string& key) const;
    double num(const std::string& key, double fallback = 0.0) const;
    std::string str(const std::string& key, const std::string& fallback = "") const;
};

bool parseJson(const std::string& text, JsonValue& out, std::string* error = nullptr);
std::string jsonString(const std::string& s);   // В кавычках, с экранированием
