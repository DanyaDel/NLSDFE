#pragma once
#include "Script.h"
#include <memory>

namespace nls {

enum class Tok {
    End, Ident, Number, String,
    // ключевые слова
    Var, Let, Func, On, If, Else, While, For, In, Return, Break, Continue, True, False, Null, And, Or, Not,
    // знаки
    LParen, RParen, LBrace, RBrace, Comma, Colon, Semicolon, Dot, DotDot, Arrow,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Eq, Ne, Lt, Le, Gt, Ge,
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    double number = 0.0;
    bool isInt = false;
    bool newlineBefore = false;   // Перенос строки перед токеном: заменяет точку с запятой
    int line = 1, column = 1;
};

bool tokenize(const std::string& source, std::vector<Token>& out, std::vector<CompileError>& errors);

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum Kind { Number, IntLit, BoolLit, StringLit, NullLit, Name, Unary, Binary, Call, Member, Method } kind;
    int line = 0, column = 0;
    std::string name;      // Имя, оператор, поле или функция
    double number = 0.0;
    std::vector<ExprPtr> args;   // Операнды / аргументы; у Member и Method args[0] — объект
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    enum Kind { Let, Assign, ExprStmt, If, While, For, Return, Break, Continue, Block } kind;
    int line = 0, column = 0;
    std::string name;      // Имя переменной (Let, For) или оператор присваивания
    bool hasType = false;
    Type type = Type::Void;
    ExprPtr target, value, extra;       // For: value..extra — границы диапазона
    std::vector<StmtPtr> body, elseBody;
};

struct Param { std::string name; Type type; };

struct Decl {
    enum Kind { Var, Func, Event } kind;
    int line = 0, column = 0;
    std::string name;
    bool hasType = false;
    Type type = Type::Void;      // Тип переменной или возвращаемое значение
    ExprPtr init;
    std::vector<Param> params;
    std::vector<StmtPtr> body;
};

bool parse(const std::vector<Token>& tokens, std::vector<Decl>& out, std::vector<CompileError>& errors);

// Байткод
enum Op : uint8_t {
    OP_HALT, OP_CONST, OP_INT, OP_STR, OP_NULL, OP_SELF, OP_POP,
    OP_LOAD_LOCAL, OP_STORE_LOCAL, OP_LOAD_GLOBAL, OP_STORE_GLOBAL,
    OP_ADD_F, OP_SUB_F, OP_MUL_F, OP_DIV_F, OP_MOD_F, OP_NEG_F,
    OP_ADD_I, OP_SUB_I, OP_MUL_I, OP_DIV_I, OP_MOD_I, OP_NEG_I,
    OP_ADD_V, OP_SUB_V, OP_MUL_V, OP_MUL_VF, OP_MUL_FV, OP_DIV_VF, OP_NEG_V,
    OP_CONCAT, OP_I2F, OP_I2F_UNDER, OP_F2I,
    OP_TOSTR,        // аргумент: младшие 4 бита — тип значения, бит 4 — преобразовать второй сверху слот
    OP_EQ_F, OP_LT_F, OP_LE_F, OP_GT_F, OP_GE_F, OP_EQ_I, OP_LT_I, OP_LE_I, OP_GT_I, OP_GE_I, OP_EQ_V, OP_EQ_S, OP_NOT,
    OP_MAKE_VEC, OP_GET_COMP, OP_SET_COMP,
    OP_JMP, OP_JMP_FALSE, OP_JMP_FALSE_KEEP, OP_JMP_TRUE_KEEP,
    OP_CALL, OP_RET, OP_RET_VOID, OP_NATIVE, OP_GET_PROP, OP_SET_PROP,
};

inline uint32_t encode(Op op, int32_t arg = 0) { return (uint32_t)op | ((uint32_t)arg << 8); }
inline Op opOf(uint32_t ins) { return (Op)(ins & 0xFF); }
inline int32_t argOf(uint32_t ins) { return (int32_t)(ins >> 8); }

}
