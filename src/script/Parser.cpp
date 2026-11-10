#include "Ast.h"
#include <algorithm>

namespace nls {

namespace {

struct ParseAbort {};

class Parser {
public:
    Parser(const std::vector<Token>& t, std::vector<CompileError>& e) : tokens(t), errors(e) {}

    void parseProgram(std::vector<Decl>& out) {
        while (peek().kind != Tok::End) {
            try { out.push_back(parseDecl()); }
            catch (ParseAbort&) { synchronize(); }
        }
    }

private:
    const std::vector<Token>& tokens;
    std::vector<CompileError>& errors;
    size_t pos = 0;

    const Token& peek(int ahead = 0) const { return tokens[std::min(pos + ahead, tokens.size() - 1)]; }
    const Token& advance() { const Token& t = tokens[pos]; if (pos + 1 < tokens.size()) pos++; return t; }
    bool check(Tok k) const { return peek().kind == k; }
    bool match(Tok k) { if (check(k)) { advance(); return true; } return false; }

    [[noreturn]] void fail(const Token& at, const std::string& message) {
        errors.push_back({at.line, at.column, message});
        throw ParseAbort{};
    }
    const Token& expect(Tok k, const char* what) {
        if (!check(k)) fail(peek(), std::string("expected ") + what + (peek().kind == Tok::End ? " but the file ended" : ", found '" + peek().text + "'"));
        return advance();
    }

    // После ошибки пропускаем до следующего объявления верхнего уровня
    void synchronize() {
        int depth = 0;
        while (peek().kind != Tok::End) {
            Tok k = peek().kind;
            if (depth == 0 && (k == Tok::Var || k == Tok::Func || k == Tok::On) && peek().column == 1) return;
            if (k == Tok::LBrace) depth++;
            if (k == Tok::RBrace) depth--;
            advance();
        }
    }

    Type parseType() {
        const Token& t = expect(Tok::Ident, "a type name");
        if (t.text == "float") return Type::Float;
        if (t.text == "int") return Type::Int;
        if (t.text == "bool") return Type::Bool;
        if (t.text == "vec3") return Type::Vec3;
        if (t.text == "string") return Type::String;
        if (t.text == "node") return Type::Node;
        fail(t, "unknown type '" + t.text + "' (float, int, bool, vec3, string, node)");
    }

    Decl parseDecl() {
        Decl d;
        const Token& start = peek();
        d.line = start.line; d.column = start.column;
        if (match(Tok::Var)) {
            d.kind = Decl::Var;
            d.name = expect(Tok::Ident, "a variable name").text;
            if (match(Tok::Colon)) { d.type = parseType(); d.hasType = true; }
            if (match(Tok::Assign)) d.init = parseExpr();
            if (!d.hasType && !d.init) fail(start, "variable '" + d.name + "' needs a type or an initial value");
            match(Tok::Semicolon);
            return d;
        }
        if (check(Tok::Func) || check(Tok::On)) {
            d.kind = advance().kind == Tok::Func ? Decl::Func : Decl::Event;
            d.name = expect(Tok::Ident, d.kind == Decl::Func ? "a function name" : "an event name").text;
            expect(Tok::LParen, "'('");
            if (!check(Tok::RParen)) {
                do {
                    Param p;
                    p.name = expect(Tok::Ident, "a parameter name").text;
                    expect(Tok::Colon, "':' and a parameter type");
                    p.type = parseType();
                    d.params.push_back(p);
                } while (match(Tok::Comma));
            }
            expect(Tok::RParen, "')'");
            if (match(Tok::Arrow)) { d.type = parseType(); d.hasType = true; }
            d.body = parseBlock();
            return d;
        }
        fail(start, "expected 'var', 'func' or 'on' at the top level, found '" + start.text + "'");
    }

    std::vector<StmtPtr> parseBlock() {
        expect(Tok::LBrace, "'{'");
        std::vector<StmtPtr> body;
        while (!check(Tok::RBrace)) {
            if (check(Tok::End)) fail(peek(), "missing '}'");
            body.push_back(parseStmt());
        }
        advance();
        return body;
    }

    StmtPtr make(Stmt::Kind kind, const Token& at) {
        auto s = std::make_unique<Stmt>();
        s->kind = kind; s->line = at.line; s->column = at.column;
        return s;
    }

    StmtPtr parseStmt() {
        const Token& start = peek();
        if (match(Tok::Let)) {
            auto s = make(Stmt::Let, start);
            s->name = expect(Tok::Ident, "a variable name").text;
            if (match(Tok::Colon)) { s->type = parseType(); s->hasType = true; }
            if (match(Tok::Assign)) s->value = parseExpr();
            if (!s->hasType && !s->value) fail(start, "'let " + s->name + "' needs a type or a value");
            match(Tok::Semicolon);
            return s;
        }
        if (match(Tok::If)) {
            auto s = make(Stmt::If, start);
            s->value = parseExpr();
            s->body = parseBlock();
            if (match(Tok::Else)) {
                if (check(Tok::If)) s->elseBody.push_back(parseStmt());
                else s->elseBody = parseBlock();
            }
            return s;
        }
        if (match(Tok::While)) {
            auto s = make(Stmt::While, start);
            s->value = parseExpr();
            s->body = parseBlock();
            return s;
        }
        if (match(Tok::For)) {
            auto s = make(Stmt::For, start);
            s->name = expect(Tok::Ident, "a loop variable").text;
            expect(Tok::In, "'in'");
            s->value = parseExpr();
            expect(Tok::DotDot, "'..' (for i in 0..10)");
            s->extra = parseExpr();
            s->body = parseBlock();
            return s;
        }
        if (match(Tok::Return)) {
            auto s = make(Stmt::Return, start);
            if (!check(Tok::RBrace) && !check(Tok::Semicolon) && !peek().newlineBefore) s->value = parseExpr();
            match(Tok::Semicolon);
            return s;
        }
        if (match(Tok::Break)) { match(Tok::Semicolon); return make(Stmt::Break, start); }
        if (match(Tok::Continue)) { match(Tok::Semicolon); return make(Stmt::Continue, start); }
        if (check(Tok::LBrace)) {
            auto s = make(Stmt::Block, start);
            s->body = parseBlock();
            return s;
        }

        ExprPtr e = parseExpr();
        Tok k = peek().kind;
        if (k == Tok::Assign || k == Tok::PlusAssign || k == Tok::MinusAssign || k == Tok::StarAssign || k == Tok::SlashAssign) {
            auto s = make(Stmt::Assign, start);
            s->name = advance().text;
            s->target = std::move(e);
            s->value = parseExpr();
            match(Tok::Semicolon);
            return s;
        }
        auto s = make(Stmt::ExprStmt, start);
        s->value = std::move(e);
        match(Tok::Semicolon);
        return s;
    }

    ExprPtr node(Expr::Kind kind, const Token& at) {
        auto e = std::make_unique<Expr>();
        e->kind = kind; e->line = at.line; e->column = at.column;
        return e;
    }

    ExprPtr binary(ExprPtr left, const Token& op, ExprPtr right) {
        auto e = node(Expr::Binary, op);
        e->name = op.kind == Tok::And ? "and" : op.kind == Tok::Or ? "or" : op.text;
        e->args.push_back(std::move(left));
        e->args.push_back(std::move(right));
        return e;
    }

    ExprPtr parseExpr() { return parseOr(); }

    ExprPtr parseOr() {
        ExprPtr e = parseAnd();
        while (check(Tok::Or)) { const Token& op = advance(); e = binary(std::move(e), op, parseAnd()); }
        return e;
    }
    ExprPtr parseAnd() {
        ExprPtr e = parseEquality();
        while (check(Tok::And)) { const Token& op = advance(); e = binary(std::move(e), op, parseEquality()); }
        return e;
    }
    ExprPtr parseEquality() {
        ExprPtr e = parseComparison();
        while (check(Tok::Eq) || check(Tok::Ne)) { const Token& op = advance(); e = binary(std::move(e), op, parseComparison()); }
        return e;
    }
    ExprPtr parseComparison() {
        ExprPtr e = parseTerm();
        while (check(Tok::Lt) || check(Tok::Le) || check(Tok::Gt) || check(Tok::Ge)) { const Token& op = advance(); e = binary(std::move(e), op, parseTerm()); }
        return e;
    }
    ExprPtr parseTerm() {
        ExprPtr e = parseFactor();
        // Минус в начале новой строки — это новый оператор, а не продолжение выражения
        while (check(Tok::Plus) || (check(Tok::Minus) && !peek().newlineBefore)) { const Token& op = advance(); e = binary(std::move(e), op, parseFactor()); }
        return e;
    }
    ExprPtr parseFactor() {
        ExprPtr e = parseUnary();
        while (check(Tok::Star) || check(Tok::Slash) || check(Tok::Percent)) { const Token& op = advance(); e = binary(std::move(e), op, parseUnary()); }
        return e;
    }
    ExprPtr parseUnary() {
        if (check(Tok::Minus) || check(Tok::Not)) {
            const Token& op = advance();
            auto e = node(Expr::Unary, op);
            e->name = op.kind == Tok::Not ? "not" : "-";
            e->args.push_back(parseUnary());
            return e;
        }
        return parsePostfix();
    }

    void parseArgs(Expr& call) {
        if (!check(Tok::RParen)) {
            do { call.args.push_back(parseExpr()); } while (match(Tok::Comma));
        }
        expect(Tok::RParen, "')'");
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        for (;;) {
            if (check(Tok::Dot)) {
                advance();
                const Token& name = expect(Tok::Ident, "a field or method name after '.'");
                if (check(Tok::LParen) && !peek().newlineBefore) {
                    advance();
                    auto m = node(Expr::Method, name);
                    m->name = name.text;
                    m->args.push_back(std::move(e));
                    parseArgs(*m);
                    e = std::move(m);
                } else {
                    auto m = node(Expr::Member, name);
                    m->name = name.text;
                    m->args.push_back(std::move(e));
                    e = std::move(m);
                }
                continue;
            }
            break;
        }
        return e;
    }

    ExprPtr parsePrimary() {
        const Token& t = peek();
        if (match(Tok::Number)) {
            auto e = node(t.isInt ? Expr::IntLit : Expr::Number, t);
            e->number = t.number;
            return e;
        }
        if (match(Tok::String)) { auto e = node(Expr::StringLit, t); e->name = t.text; return e; }
        if (match(Tok::True)) { auto e = node(Expr::BoolLit, t); e->number = 1; return e; }
        if (match(Tok::False)) { auto e = node(Expr::BoolLit, t); e->number = 0; return e; }
        if (match(Tok::Null)) return node(Expr::NullLit, t);
        if (match(Tok::Ident)) {
            if (check(Tok::LParen) && !peek().newlineBefore) {
                advance();
                auto e = node(Expr::Call, t);
                e->name = t.text;
                parseArgs(*e);
                return e;
            }
            auto e = node(Expr::Name, t);
            e->name = t.text;
            return e;
        }
        if (match(Tok::LParen)) {
            ExprPtr e = parseExpr();
            expect(Tok::RParen, "')'");
            return e;
        }
        fail(t, t.kind == Tok::End ? "unexpected end of file" : "unexpected '" + t.text + "'");
    }
};

}

bool parse(const std::vector<Token>& tokens, std::vector<Decl>& out, std::vector<CompileError>& errors) {
    size_t before = errors.size();
    Parser(tokens, errors).parseProgram(out);
    return errors.size() == before;
}

}
