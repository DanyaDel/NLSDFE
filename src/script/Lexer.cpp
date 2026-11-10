#include "Ast.h"
#include <cctype>
#include <cstdlib>

namespace nls {

namespace {
    const std::unordered_map<std::string, Tok> keywords = {
        {"var", Tok::Var}, {"let", Tok::Let}, {"func", Tok::Func}, {"on", Tok::On}, {"if", Tok::If}, {"else", Tok::Else},
        {"while", Tok::While}, {"for", Tok::For}, {"in", Tok::In}, {"return", Tok::Return}, {"break", Tok::Break},
        {"continue", Tok::Continue}, {"true", Tok::True}, {"false", Tok::False}, {"null", Tok::Null},
        {"and", Tok::And}, {"or", Tok::Or}, {"not", Tok::Not},
    };
}

bool tokenize(const std::string& src, std::vector<Token>& out, std::vector<CompileError>& errors) {
    size_t i = 0;
    int line = 1, col = 1;
    bool newline = false;
    auto error = [&](const std::string& msg) { errors.push_back({line, col, msg}); };
    auto push = [&](Tok kind, std::string text, int startCol) {
        Token t; t.kind = kind; t.text = std::move(text); t.line = line; t.column = startCol; t.newlineBefore = newline;
        newline = false;
        out.push_back(std::move(t));
    };

    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') { line++; col = 1; i++; newline = true; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; col++; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') { while (i < src.size() && src[i] != '\n') i++; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            size_t end = src.find("*/", i + 2);
            if (end == std::string::npos) { error("unterminated comment"); return false; }
            for (size_t k = i; k < end + 2; k++) { if (src[k] == '\n') { line++; col = 1; newline = true; } else col++; }
            i = end + 2;
            continue;
        }

        int startCol = col;
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t start = i;
            while (i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
            std::string word = src.substr(start, i - start);
            col += (int)(i - start);
            auto kw = keywords.find(word);
            push(kw != keywords.end() ? kw->second : Tok::Ident, word, startCol);
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            size_t start = i;
            bool isInt = true;
            while (i < src.size() && std::isdigit((unsigned char)src[i])) i++;
            // "1..5" — это диапазон, а не число с точкой
            if (i + 1 < src.size() && src[i] == '.' && std::isdigit((unsigned char)src[i + 1])) {
                isInt = false; i++;
                while (i < src.size() && std::isdigit((unsigned char)src[i])) i++;
            }
            std::string text = src.substr(start, i - start);
            col += (int)(i - start);
            push(Tok::Number, text, startCol);
            out.back().number = std::strtod(text.c_str(), nullptr);
            out.back().isInt = isInt;
            continue;
        }
        if (c == '"') {
            std::string text;
            i++; col++;
            bool closed = false;
            while (i < src.size() && src[i] != '\n') {
                char ch = src[i];
                if (ch == '"') { closed = true; i++; col++; break; }
                if (ch == '\\' && i + 1 < src.size()) {
                    char e = src[i + 1];
                    text += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
                    i += 2; col += 2;
                    continue;
                }
                text += ch; i++; col++;
            }
            if (!closed) { error("unterminated string"); return false; }
            push(Tok::String, text, startCol);
            continue;
        }

        auto two = [&](char next) { return i + 1 < src.size() && src[i + 1] == next; };
        Tok kind = Tok::End;
        int len = 1;
        switch (c) {
            case '(': kind = Tok::LParen; break;
            case ')': kind = Tok::RParen; break;
            case '{': kind = Tok::LBrace; break;
            case '}': kind = Tok::RBrace; break;
            case ',': kind = Tok::Comma; break;
            case ':': kind = Tok::Colon; break;
            case ';': kind = Tok::Semicolon; break;
            case '%': kind = Tok::Percent; break;
            case '.': if (two('.')) { kind = Tok::DotDot; len = 2; } else kind = Tok::Dot; break;
            case '+': if (two('=')) { kind = Tok::PlusAssign; len = 2; } else kind = Tok::Plus; break;
            case '-': if (two('=')) { kind = Tok::MinusAssign; len = 2; } else if (two('>')) { kind = Tok::Arrow; len = 2; } else kind = Tok::Minus; break;
            case '*': if (two('=')) { kind = Tok::StarAssign; len = 2; } else kind = Tok::Star; break;
            case '/': if (two('=')) { kind = Tok::SlashAssign; len = 2; } else kind = Tok::Slash; break;
            case '=': if (two('=')) { kind = Tok::Eq; len = 2; } else kind = Tok::Assign; break;
            case '!': if (two('=')) { kind = Tok::Ne; len = 2; } else kind = Tok::Not; break;
            case '<': if (two('=')) { kind = Tok::Le; len = 2; } else kind = Tok::Lt; break;
            case '>': if (two('=')) { kind = Tok::Ge; len = 2; } else kind = Tok::Gt; break;
            case '&': if (two('&')) { kind = Tok::And; len = 2; } break;
            case '|': if (two('|')) { kind = Tok::Or; len = 2; } break;
        }
        if (kind == Tok::End) { error(std::string("unexpected character '") + c + "'"); return false; }
        push(kind, src.substr(i, len), startCol);
        i += len; col += len;
    }
    Token end; end.kind = Tok::End; end.line = line; end.column = col; end.newlineBefore = true;
    out.push_back(end);
    return true;
}

}
