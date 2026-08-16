#include "mdtc/compiler.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace mdtc {

CompileError::CompileError(std::size_t line, std::size_t column, const std::string& message)
    : std::runtime_error(message), line_(line), column_(column) {}

std::size_t CompileError::line() const noexcept { return line_; }
std::size_t CompileError::column() const noexcept { return column_; }

namespace {

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

enum class TokenKind {
    End,
    Identifier,
    NumberLiteral,
    StringLiteral,
    KwVoid,
    KwBool,
    KwInt,
    KwFloat,
    KwNumber,
    KwString,
    KwMessage,
    KwBuilding,
    KwExtern,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwBreak,
    KwContinue,
    KwReturn,
    KwTrue,
    KwFalse,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Equal,
    Less,
    Greater,
    PlusPlus,
    MinusMinus,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    EqualEqual,
    BangEqual,
    LessEqual,
    GreaterEqual,
    AndAnd,
    OrOr,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourceLocation location;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!atEnd()) {
            skipWhitespaceAndComments();
            if (atEnd()) break;
            tokens.push_back(scanToken());
        }
        tokens.push_back({TokenKind::End, "", location()});
        return tokens;
    }

private:
    bool atEnd() const { return position_ >= source_.size(); }

    char peek(std::size_t offset = 0) const {
        const std::size_t index = position_ + offset;
        return index < source_.size() ? source_[index] : '\0';
    }

    char advance() {
        const char character = source_[position_++];
        if (character == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return character;
    }

    SourceLocation location() const { return {line_, column_}; }

    bool match(char expected) {
        if (peek() != expected) return false;
        advance();
        return true;
    }

    void skipWhitespaceAndComments() {
        for (;;) {
            while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') advance();
            if (peek() == '/' && peek(1) == '/') {
                while (!atEnd() && peek() != '\n') advance();
                continue;
            }
            if (peek() == '/' && peek(1) == '*') {
                const SourceLocation start = location();
                advance();
                advance();
                while (!atEnd() && !(peek() == '*' && peek(1) == '/')) advance();
                if (atEnd()) throw CompileError(start.line, start.column, "未结束的块注释");
                advance();
                advance();
                continue;
            }
            break;
        }
    }

    static bool isIdentifierStart(char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') || character == '_';
    }

    static bool isIdentifierPart(char character) {
        return isIdentifierStart(character) || (character >= '0' && character <= '9');
    }

    Token scanIdentifier(SourceLocation start) {
        const std::size_t begin = position_ - 1;
        while (isIdentifierPart(peek())) advance();
        std::string text(source_.substr(begin, position_ - begin));
        static const std::unordered_map<std::string, TokenKind> keywords = {
            {"void", TokenKind::KwVoid}, {"bool", TokenKind::KwBool},
            {"int", TokenKind::KwInt}, {"float", TokenKind::KwFloat},
            {"number", TokenKind::KwNumber}, {"string", TokenKind::KwString},
            {"message", TokenKind::KwMessage}, {"extern", TokenKind::KwExtern},
            {"building", TokenKind::KwBuilding},
            {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
            {"while", TokenKind::KwWhile}, {"for", TokenKind::KwFor},
            {"break", TokenKind::KwBreak}, {"continue", TokenKind::KwContinue},
            {"return", TokenKind::KwReturn}, {"true", TokenKind::KwTrue},
            {"false", TokenKind::KwFalse},
        };
        const auto iterator = keywords.find(text);
        return {iterator == keywords.end() ? TokenKind::Identifier : iterator->second,
                std::move(text), start};
    }

    Token scanNumber(SourceLocation start) {
        const std::size_t begin = position_ - 1;
        while (peek() >= '0' && peek() <= '9') advance();
        if (peek() == '.' && peek(1) >= '0' && peek(1) <= '9') {
            advance();
            while (peek() >= '0' && peek() <= '9') advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!(peek() >= '0' && peek() <= '9')) {
                throw CompileError(start.line, start.column, "指数后缺少数字");
            }
            while (peek() >= '0' && peek() <= '9') advance();
        }
        return {TokenKind::NumberLiteral,
                std::string(source_.substr(begin, position_ - begin)), start};
    }

    Token scanString(SourceLocation start) {
        std::string value;
        while (!atEnd() && peek() != '"') {
            char character = advance();
            if (character == '\\') {
                if (atEnd()) break;
                const char escaped = advance();
                switch (escaped) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    default:
                        throw CompileError(start.line, start.column, "不支持的字符串转义");
                }
            } else {
                value.push_back(character);
            }
        }
        if (atEnd()) throw CompileError(start.line, start.column, "未结束的字符串");
        advance();
        return {TokenKind::StringLiteral, std::move(value), start};
    }

    Token scanToken() {
        const SourceLocation start = location();
        const char character = advance();
        if (isIdentifierStart(character)) return scanIdentifier(start);
        if (character >= '0' && character <= '9') return scanNumber(start);

        switch (character) {
            case '"': return scanString(start);
            case '(': return {TokenKind::LeftParen, "(", start};
            case ')': return {TokenKind::RightParen, ")", start};
            case '{': return {TokenKind::LeftBrace, "{", start};
            case '}': return {TokenKind::RightBrace, "}", start};
            case ',': return {TokenKind::Comma, ",", start};
            case ';': return {TokenKind::Semicolon, ";", start};
            case '+':
                if (match('+')) return {TokenKind::PlusPlus, "++", start};
                if (match('=')) return {TokenKind::PlusEqual, "+=", start};
                return {TokenKind::Plus, "+", start};
            case '-':
                if (match('-')) return {TokenKind::MinusMinus, "--", start};
                if (match('=')) return {TokenKind::MinusEqual, "-=", start};
                return {TokenKind::Minus, "-", start};
            case '*':
                if (match('=')) return {TokenKind::StarEqual, "*=", start};
                return {TokenKind::Star, "*", start};
            case '/':
                if (match('=')) return {TokenKind::SlashEqual, "/=", start};
                return {TokenKind::Slash, "/", start};
            case '%':
                if (match('=')) return {TokenKind::PercentEqual, "%=", start};
                return {TokenKind::Percent, "%", start};
            case '!':
                if (match('=')) return {TokenKind::BangEqual, "!=", start};
                return {TokenKind::Bang, "!", start};
            case '=':
                if (match('=')) return {TokenKind::EqualEqual, "==", start};
                return {TokenKind::Equal, "=", start};
            case '<':
                if (match('=')) return {TokenKind::LessEqual, "<=", start};
                return {TokenKind::Less, "<", start};
            case '>':
                if (match('=')) return {TokenKind::GreaterEqual, ">=", start};
                return {TokenKind::Greater, ">", start};
            case '&':
                if (match('&')) return {TokenKind::AndAnd, "&&", start};
                break;
            case '|':
                if (match('|')) return {TokenKind::OrOr, "||", start};
                break;
        }
        throw CompileError(start.line, start.column,
                           std::string("无法识别的字符: ") + character);
    }

    std::string_view source_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

enum class TypeKind { Void, Bool, Int, Float, Number, String, Message, Building };

std::string typeName(TypeKind type) {
    switch (type) {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Message: return "message";
        case TypeKind::Building: return "building";
    }
    return "<unknown>";
}

bool isNumeric(TypeKind type) {
    return type == TypeKind::Int || type == TypeKind::Float || type == TypeKind::Number;
}

struct Expr {
    enum class Kind { Number, String, Boolean, Variable, Unary, Binary, Assign, Call, Prefix, Postfix };

    Kind kind = Kind::Number;
    SourceLocation location;
    std::string text;
    bool boolean = false;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct Stmt {
    enum class Kind { Empty, Block, Variable, Expression, If, While, For, Break, Continue, Return };

    Kind kind = Kind::Block;
    SourceLocation location;
    TypeKind type = TypeKind::Void;
    std::string name;
    std::vector<std::unique_ptr<Stmt>> statements;
    std::unique_ptr<Stmt> initializerStatement;
    std::unique_ptr<Expr> expression;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> increment;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct Parameter {
    TypeKind type = TypeKind::Void;
    std::string name;
    SourceLocation location;
};

struct FunctionDecl {
    TypeKind returnType = TypeKind::Void;
    std::string name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Stmt> body;
    SourceLocation location;
};

struct GlobalDecl {
    TypeKind type = TypeKind::Void;
    std::string name;
    bool external = false;
    std::unique_ptr<Expr> initializer;
    SourceLocation location;
};

struct Program {
    std::vector<GlobalDecl> globals;
    std::vector<FunctionDecl> functions;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program parse() {
        Program program;
        while (!check(TokenKind::End)) {
            const bool external = match(TokenKind::KwExtern);
            const TypeKind type = parseType();
            const Token name = consume(TokenKind::Identifier, "类型后需要名称");
            if (match(TokenKind::LeftParen)) {
                if (external) fail(name, "暂不支持 extern 函数");
                program.functions.push_back(parseFunction(type, name));
            } else {
                program.globals.push_back(parseGlobal(type, name, external));
            }
        }
        return program;
    }

private:
    const Token& current() const { return tokens_[position_]; }
    const Token& previous() const { return tokens_[position_ - 1]; }
    bool check(TokenKind kind) const { return current().kind == kind; }

    bool match(TokenKind kind) {
        if (!check(kind)) return false;
        ++position_;
        return true;
    }

    Token consume(TokenKind kind, const std::string& message) {
        if (!check(kind)) fail(current(), message);
        return tokens_[position_++];
    }

    [[noreturn]] static void fail(const Token& token, const std::string& message) {
        throw CompileError(token.location.line, token.location.column, message);
    }

    bool isTypeToken(TokenKind kind) const {
        return kind == TokenKind::KwVoid || kind == TokenKind::KwBool ||
               kind == TokenKind::KwInt || kind == TokenKind::KwFloat ||
               kind == TokenKind::KwNumber || kind == TokenKind::KwString ||
               kind == TokenKind::KwMessage || kind == TokenKind::KwBuilding;
    }

    TypeKind parseType() {
        const Token token = current();
        ++position_;
        switch (token.kind) {
            case TokenKind::KwVoid: return TypeKind::Void;
            case TokenKind::KwBool: return TypeKind::Bool;
            case TokenKind::KwInt: return TypeKind::Int;
            case TokenKind::KwFloat: return TypeKind::Float;
            case TokenKind::KwNumber: return TypeKind::Number;
            case TokenKind::KwString: return TypeKind::String;
            case TokenKind::KwMessage: return TypeKind::Message;
            case TokenKind::KwBuilding: return TypeKind::Building;
            default: fail(token, "需要数据类型");
        }
    }

    FunctionDecl parseFunction(TypeKind returnType, const Token& name) {
        FunctionDecl function;
        function.returnType = returnType;
        function.name = name.text;
        function.location = name.location;
        if (!check(TokenKind::RightParen)) {
            do {
                const TypeKind parameterType = parseType();
                if (parameterType == TypeKind::Void) fail(previous(), "参数不能是 void 类型");
                const Token parameterName = consume(TokenKind::Identifier, "参数类型后需要名称");
                function.parameters.push_back({parameterType, parameterName.text, parameterName.location});
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightParen, "参数列表缺少右括号");
        function.body = parseBlock();
        return function;
    }

    GlobalDecl parseGlobal(TypeKind type, const Token& name, bool external) {
        if (type == TypeKind::Void) fail(name, "变量不能是 void 类型");
        GlobalDecl global;
        global.type = type;
        global.name = name.text;
        global.external = external;
        global.location = name.location;
        if (match(TokenKind::Equal)) global.initializer = parseExpression();
        if (external && global.initializer) fail(name, "extern 变量不能带初始化器");
        consume(TokenKind::Semicolon, "全局变量声明后需要分号");
        return global;
    }

    std::unique_ptr<Stmt> parseBlock() {
        const Token brace = consume(TokenKind::LeftBrace, "函数或语句块需要左大括号");
        auto block = std::make_unique<Stmt>();
        block->kind = Stmt::Kind::Block;
        block->location = brace.location;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
            block->statements.push_back(parseStatement());
        }
        consume(TokenKind::RightBrace, "语句块缺少右大括号");
        return block;
    }

    std::unique_ptr<Stmt> parseStatement() {
        if (match(TokenKind::Semicolon)) {
            auto statement = std::make_unique<Stmt>();
            statement->kind = Stmt::Kind::Empty;
            statement->location = previous().location;
            return statement;
        }
        if (check(TokenKind::LeftBrace)) return parseBlock();
        if (match(TokenKind::KwIf)) return parseIf(previous());
        if (match(TokenKind::KwWhile)) return parseWhile(previous());
        if (match(TokenKind::KwFor)) return parseFor(previous());
        if (match(TokenKind::KwBreak)) return parseSimple(Stmt::Kind::Break, previous());
        if (match(TokenKind::KwContinue)) return parseSimple(Stmt::Kind::Continue, previous());
        if (match(TokenKind::KwReturn)) return parseReturn(previous());
        if (isTypeToken(current().kind)) return parseVariable(true);

        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Expression;
        statement->location = current().location;
        statement->expression = parseExpression();
        consume(TokenKind::Semicolon, "表达式后需要分号");
        return statement;
    }

    std::unique_ptr<Stmt> parseVariable(bool consumeSemicolon) {
        const TypeKind type = parseType();
        const Token name = consume(TokenKind::Identifier, "变量类型后需要名称");
        if (type == TypeKind::Void) fail(name, "变量不能是 void 类型");
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Variable;
        statement->location = name.location;
        statement->type = type;
        statement->name = name.text;
        if (match(TokenKind::Equal)) statement->expression = parseExpression();
        if (consumeSemicolon) consume(TokenKind::Semicolon, "变量声明后需要分号");
        return statement;
    }

    std::unique_ptr<Stmt> parseIf(const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::If;
        statement->location = keyword.location;
        consume(TokenKind::LeftParen, "if 后需要左括号");
        statement->condition = parseExpression();
        consume(TokenKind::RightParen, "if 条件后需要右括号");
        statement->thenBranch = parseStatement();
        if (match(TokenKind::KwElse)) statement->elseBranch = parseStatement();
        return statement;
    }

    std::unique_ptr<Stmt> parseWhile(const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::While;
        statement->location = keyword.location;
        consume(TokenKind::LeftParen, "while 后需要左括号");
        statement->condition = parseExpression();
        consume(TokenKind::RightParen, "while 条件后需要右括号");
        statement->thenBranch = parseStatement();
        return statement;
    }

    std::unique_ptr<Stmt> parseFor(const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::For;
        statement->location = keyword.location;
        consume(TokenKind::LeftParen, "for 后需要左括号");
        if (match(TokenKind::Semicolon)) {
        } else if (isTypeToken(current().kind)) {
            statement->initializerStatement = parseVariable(false);
            consume(TokenKind::Semicolon, "for 初始化部分后需要分号");
        } else {
            auto initializer = std::make_unique<Stmt>();
            initializer->kind = Stmt::Kind::Expression;
            initializer->location = current().location;
            initializer->expression = parseExpression();
            statement->initializerStatement = std::move(initializer);
            consume(TokenKind::Semicolon, "for 初始化部分后需要分号");
        }
        if (!check(TokenKind::Semicolon)) statement->condition = parseExpression();
        consume(TokenKind::Semicolon, "for 条件后需要分号");
        if (!check(TokenKind::RightParen)) statement->increment = parseExpression();
        consume(TokenKind::RightParen, "for 头部缺少右括号");
        statement->thenBranch = parseStatement();
        return statement;
    }

    std::unique_ptr<Stmt> parseSimple(Stmt::Kind kind, const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = kind;
        statement->location = keyword.location;
        consume(TokenKind::Semicolon, "语句后需要分号");
        return statement;
    }

    std::unique_ptr<Stmt> parseReturn(const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Return;
        statement->location = keyword.location;
        if (!check(TokenKind::Semicolon)) statement->expression = parseExpression();
        consume(TokenKind::Semicolon, "return 后需要分号");
        return statement;
    }

    std::unique_ptr<Expr> parseExpression() { return parseAssignment(); }

    std::unique_ptr<Expr> parseAssignment() {
        auto expression = parseLogicalOr();
        if (match(TokenKind::Equal) || match(TokenKind::PlusEqual) ||
            match(TokenKind::MinusEqual) || match(TokenKind::StarEqual) ||
            match(TokenKind::SlashEqual) || match(TokenKind::PercentEqual)) {
            const Token operation = previous();
            auto assignment = std::make_unique<Expr>();
            assignment->kind = Expr::Kind::Assign;
            assignment->location = operation.location;
            assignment->text = operation.text;
            assignment->left = std::move(expression);
            assignment->right = parseAssignment();
            return assignment;
        }
        return expression;
    }

    std::unique_ptr<Expr> parseLogicalOr() {
        auto expression = parseLogicalAnd();
        while (match(TokenKind::OrOr)) {
            const Token operation = previous();
            auto right = parseLogicalAnd();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseLogicalAnd() {
        auto expression = parseEquality();
        while (match(TokenKind::AndAnd)) {
            const Token operation = previous();
            auto right = parseEquality();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseEquality() {
        auto expression = parseComparison();
        while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
            const Token operation = previous();
            auto right = parseComparison();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseComparison() {
        auto expression = parseTerm();
        while (match(TokenKind::Less) || match(TokenKind::LessEqual) ||
               match(TokenKind::Greater) || match(TokenKind::GreaterEqual)) {
            const Token operation = previous();
            auto right = parseTerm();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseTerm() {
        auto expression = parseFactor();
        while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
            const Token operation = previous();
            auto right = parseFactor();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseFactor() {
        auto expression = parseUnary();
        while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent)) {
            const Token operation = previous();
            auto right = parseUnary();
            expression = makeBinary(std::move(expression), operation, std::move(right));
        }
        return expression;
    }

    std::unique_ptr<Expr> parseUnary() {
        if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Plus) ||
            match(TokenKind::PlusPlus) || match(TokenKind::MinusMinus)) {
            const Token operation = previous();
            auto expression = std::make_unique<Expr>();
            expression->kind = operation.kind == TokenKind::PlusPlus || operation.kind == TokenKind::MinusMinus
                                   ? Expr::Kind::Prefix : Expr::Kind::Unary;
            expression->location = operation.location;
            expression->text = operation.text;
            expression->right = parseUnary();
            return expression;
        }
        return parsePostfix();
    }

    std::unique_ptr<Expr> parsePostfix() {
        auto expression = parsePrimary();
        for (;;) {
            if (match(TokenKind::LeftParen)) {
                if (expression->kind != Expr::Kind::Variable) fail(previous(), "只能调用具名函数");
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->location = expression->location;
                call->text = expression->text;
                if (!check(TokenKind::RightParen)) {
                    do {
                        call->arguments.push_back(parseExpression());
                    } while (match(TokenKind::Comma));
                }
                consume(TokenKind::RightParen, "函数调用缺少右括号");
                expression = std::move(call);
            } else if (match(TokenKind::PlusPlus) || match(TokenKind::MinusMinus)) {
                const Token operation = previous();
                auto postfix = std::make_unique<Expr>();
                postfix->kind = Expr::Kind::Postfix;
                postfix->location = operation.location;
                postfix->text = operation.text;
                postfix->left = std::move(expression);
                expression = std::move(postfix);
            } else {
                break;
            }
        }
        return expression;
    }

    std::unique_ptr<Expr> parsePrimary() {
        const Token token = current();
        if (match(TokenKind::NumberLiteral)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Number;
            expression->location = token.location;
            expression->text = token.text;
            return expression;
        }
        if (match(TokenKind::StringLiteral)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::String;
            expression->location = token.location;
            expression->text = token.text;
            return expression;
        }
        if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Boolean;
            expression->location = token.location;
            expression->boolean = token.kind == TokenKind::KwTrue;
            return expression;
        }
        if (match(TokenKind::Identifier)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Variable;
            expression->location = token.location;
            expression->text = token.text;
            return expression;
        }
        if (match(TokenKind::LeftParen)) {
            auto expression = parseExpression();
            consume(TokenKind::RightParen, "表达式缺少右括号");
            return expression;
        }
        fail(token, "需要表达式");
    }

    static std::unique_ptr<Expr> makeBinary(std::unique_ptr<Expr> left,
                                            const Token& operation,
                                            std::unique_ptr<Expr> right) {
        auto expression = std::make_unique<Expr>();
        expression->kind = Expr::Kind::Binary;
        expression->location = operation.location;
        expression->text = operation.text;
        expression->left = std::move(left);
        expression->right = std::move(right);
        return expression;
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
};

struct Instruction {
    std::string opcode;
    std::vector<std::string> operands;
};

class Emitter {
public:
    void label(const std::string& name) {
        if (!labels_.emplace(name, instructions_.size()).second) {
            throw std::logic_error("重复的内部标签: " + name);
        }
    }

    void emit(std::string opcode, std::vector<std::string> operands = {}) {
        instructions_.push_back({std::move(opcode), std::move(operands)});
    }

    [[nodiscard]] std::string finish() const {
        if (instructions_.size() > 1000) {
            throw CompileError(1, 1, "生成的程序超过 Mindustry 1000 条指令限制（当前 " +
                                             std::to_string(instructions_.size()) + " 条）");
        }

        std::ostringstream output;
        for (const Instruction& instruction : instructions_) {
            output << instruction.opcode;
            for (const std::string& operand : instruction.operands) {
                output << ' ';
                if (!operand.empty() && operand.front() == '$') {
                    const std::string labelName = operand.substr(1);
                    const auto iterator = labels_.find(labelName);
                    if (iterator == labels_.end()) {
                        throw std::logic_error("未定义的内部标签: " + labelName);
                    }
                    output << iterator->second;
                } else {
                    output << operand;
                }
            }
            output << '\n';
        }
        return output.str();
    }

private:
    std::vector<Instruction> instructions_;
    std::unordered_map<std::string, std::size_t> labels_;
};

struct Symbol {
    TypeKind type = TypeKind::Void;
    std::string storage;
    bool assignable = true;
};

struct FunctionInfo {
    const FunctionDecl* declaration = nullptr;
    std::string entryLabel;
    std::string returnAddress;
    std::string resultStorage;
    std::vector<std::string> parameterStorage;
};

struct ExpressionResult {
    TypeKind type = TypeKind::Void;
    std::string operand;
    bool lvalue = false;
};

class Generator {
public:
    Generator(const Program& program, CompileOptions options)
        : program_(program), options_(options) {
        collectDeclarations();
        validateCallGraph();
    }

    std::string generate() {
        scopes_.emplace_back();
        declareGlobals();

        currentContext_ = "init";
        for (const GlobalDecl& global : program_.globals) generateGlobalInitializer(global);
        emitter_.emit("jump", {reference(mainEntryLabel_), "always", "0", "0"});

        for (const FunctionDecl& function : program_.functions) {
            if (function.name != "main_loop") generateFunction(function);
        }
        generateMainLoop();
        return emitter_.finish();
    }

private:
    [[noreturn]] static void fail(SourceLocation location, const std::string& message) {
        throw CompileError(location.line, location.column, message);
    }

    static std::string reference(const std::string& label) { return "$" + label; }

    std::string uniqueLabel(const std::string& prefix) {
        return "__label_" + prefix + '_' + std::to_string(nextLabel_++);
    }

    std::string uniqueStorage(const std::string& name) {
        return "__" + currentContext_ + "_v" + std::to_string(nextStorage_++) + '_' + name;
    }

    std::string temporary() {
        return "__" + currentContext_ + "_tmp" + std::to_string(nextTemporary_++);
    }

    static std::string escapeString(const std::string& value) {
        std::string result = "\"";
        for (char character : value) {
            switch (character) {
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                default: result.push_back(character); break;
            }
        }
        result.push_back('"');
        return result;
    }

    static std::string defaultValue(TypeKind type) {
        switch (type) {
            case TypeKind::Bool:
            case TypeKind::Int:
            case TypeKind::Float:
            case TypeKind::Number: return "0";
            case TypeKind::String: return "\"\"";
            case TypeKind::Message:
            case TypeKind::Building: return "null";
            case TypeKind::Void: break;
        }
        return "null";
    }

    void collectDeclarations() {
        std::unordered_set<std::string> topLevelNames;
        for (const GlobalDecl& global : program_.globals) {
            if (!topLevelNames.insert(global.name).second) fail(global.location, "重复的全局名称: " + global.name);
        }

        for (const FunctionDecl& function : program_.functions) {
            if (!topLevelNames.insert(function.name).second) fail(function.location, "重复的顶层名称: " + function.name);
            if (function.name == "print" || function.name == "printflush" || function.name == "getlink") {
                fail(function.location, "不能重新定义内置函数 " + function.name);
            }

            FunctionInfo info;
            info.declaration = &function;
            info.entryLabel = "__function_" + function.name;
            info.returnAddress = "__function_" + function.name + "_return_address";
            if (function.returnType != TypeKind::Void) {
                info.resultStorage = "__function_" + function.name + "_result";
            }
            std::unordered_set<std::string> parameterNames;
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                const Parameter& parameter = function.parameters[index];
                if (!parameterNames.insert(parameter.name).second) {
                    fail(parameter.location, "重复的参数名称: " + parameter.name);
                }
                info.parameterStorage.push_back("__function_" + function.name + "_arg" + std::to_string(index));
            }
            functions_.emplace(function.name, std::move(info));
        }

        const auto mainIterator = functions_.find("main_loop");
        if (mainIterator == functions_.end()) fail({1, 1}, "程序必须定义 void main_loop()");
        const FunctionDecl& main = *mainIterator->second.declaration;
        if (main.returnType != TypeKind::Void || !main.parameters.empty()) {
            fail(main.location, "入口必须声明为 void main_loop()");
        }
    }

    static void collectCalls(const Expr* expression, std::vector<std::pair<std::string, SourceLocation>>& calls) {
        if (expression == nullptr) return;
        if (expression->kind == Expr::Kind::Call) calls.emplace_back(expression->text, expression->location);
        collectCalls(expression->left.get(), calls);
        collectCalls(expression->right.get(), calls);
        for (const auto& argument : expression->arguments) collectCalls(argument.get(), calls);
    }

    static void collectCalls(const Stmt* statement, std::vector<std::pair<std::string, SourceLocation>>& calls) {
        if (statement == nullptr) return;
        collectCalls(statement->expression.get(), calls);
        collectCalls(statement->condition.get(), calls);
        collectCalls(statement->increment.get(), calls);
        collectCalls(statement->initializerStatement.get(), calls);
        collectCalls(statement->thenBranch.get(), calls);
        collectCalls(statement->elseBranch.get(), calls);
        for (const auto& child : statement->statements) collectCalls(child.get(), calls);
    }

    void validateCallGraph() {
        std::unordered_map<std::string, std::vector<std::string>> graph;
        for (const FunctionDecl& function : program_.functions) {
            std::vector<std::pair<std::string, SourceLocation>> calls;
            collectCalls(function.body.get(), calls);
            for (const auto& [callee, location] : calls) {
                if (callee == "print" || callee == "printflush" || callee == "getlink") continue;
                if (functions_.find(callee) == functions_.end()) fail(location, "未定义的函数: " + callee);
                if (callee == "main_loop") fail(location, "不能显式调用 main_loop");
                graph[function.name].push_back(callee);
            }
        }

        enum class VisitState { Visiting, Complete };
        std::unordered_map<std::string, VisitState> states;
        std::vector<std::string> stack;
        std::function<void(const std::string&)> visit = [&](const std::string& name) {
            const auto state = states.find(name);
            if (state != states.end()) {
                if (state->second == VisitState::Complete) return;
                const auto cycleBegin = std::find(stack.begin(), stack.end(), name);
                std::ostringstream cycle;
                for (auto iterator = cycleBegin; iterator != stack.end(); ++iterator) {
                    if (iterator != cycleBegin) cycle << " -> ";
                    cycle << *iterator;
                }
                cycle << " -> " << name;
                const SourceLocation location = functions_.at(name).declaration->location;
                fail(location, "暂不支持递归调用: " + cycle.str());
            }
            states[name] = VisitState::Visiting;
            stack.push_back(name);
            for (const std::string& callee : graph[name]) visit(callee);
            stack.pop_back();
            states[name] = VisitState::Complete;
        };

        for (const FunctionDecl& function : program_.functions) visit(function.name);
    }

    void declareGlobals() {
        for (const GlobalDecl& global : program_.globals) {
            const std::string storage = global.external ? global.name : "__global_" + global.name;
            scopes_.front().emplace(global.name, Symbol{global.type, storage, !global.external});
        }
    }

    void generateGlobalInitializer(const GlobalDecl& global) {
        if (global.external) return;
        const Symbol& symbol = scopes_.front().at(global.name);
        if (global.initializer) {
            const ExpressionResult value = generateExpression(*global.initializer);
            requireAssignable(global.type, value.type, global.location);
            emitter_.emit("set", {symbol.storage, value.operand});
        } else {
            emitter_.emit("set", {symbol.storage, defaultValue(global.type)});
        }
    }

    void generateFunction(const FunctionDecl& function) {
        const FunctionInfo& info = functions_.at(function.name);
        if (function.returnType != TypeKind::Void && !definitelyReturns(*function.body)) {
            fail(function.location, "非 void 函数并非所有路径都返回值: " + function.name);
        }

        currentFunction_ = &function;
        currentContext_ = "fn_" + function.name;
        emitter_.label(info.entryLabel);
        pushScope();
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            declareLocal(function.parameters[index].name,
                         {function.parameters[index].type, info.parameterStorage[index], true},
                         function.parameters[index].location);
        }
        generateStatement(*function.body);
        if (function.returnType == TypeKind::Void) emitFunctionReturn();
        popScope();
        currentFunction_ = nullptr;
    }

    void generateMainLoop() {
        const FunctionDecl& main = *functions_.at("main_loop").declaration;
        currentFunction_ = &main;
        currentContext_ = "main";
        emitter_.label(mainEntryLabel_);
        pushScope();
        generateStatement(*main.body);
        emitter_.emit("jump", {reference(mainEntryLabel_), "always", "0", "0"});
        popScope();
        currentFunction_ = nullptr;
    }

    static bool definitelyReturns(const Stmt& statement) {
        if (statement.kind == Stmt::Kind::Return) return true;
        if (statement.kind == Stmt::Kind::Block) {
            for (const auto& child : statement.statements) {
                if (definitelyReturns(*child)) return true;
            }
        }
        if (statement.kind == Stmt::Kind::If && statement.elseBranch) {
            return definitelyReturns(*statement.thenBranch) && definitelyReturns(*statement.elseBranch);
        }
        return false;
    }

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    void declareLocal(const std::string& name, Symbol symbol, SourceLocation location) {
        if (!scopes_.back().emplace(name, std::move(symbol)).second) {
            fail(location, "同一作用域内重复定义变量: " + name);
        }
    }

    const Symbol& resolve(const std::string& name, SourceLocation location) const {
        for (auto iterator = scopes_.rbegin(); iterator != scopes_.rend(); ++iterator) {
            const auto symbol = iterator->find(name);
            if (symbol != iterator->end()) return symbol->second;
        }
        fail(location, "未定义的变量: " + name);
    }

    static bool canAssign(TypeKind destination, TypeKind source) {
        if (destination == source) return true;
        if (destination == TypeKind::Number && isNumeric(source)) return true;
        if (destination == TypeKind::Float && (source == TypeKind::Int || source == TypeKind::Number)) return true;
        return false;
    }

    static void requireAssignable(TypeKind destination, TypeKind source, SourceLocation location) {
        if (!canAssign(destination, source)) {
            fail(location, "不能把 " + typeName(source) + " 赋值给 " + typeName(destination));
        }
    }

    ExpressionResult toBoolean(ExpressionResult value, SourceLocation location) {
        if (value.type == TypeKind::Bool) return value;
        if (!isNumeric(value.type)) fail(location, "条件需要 bool 或数值类型");
        const std::string result = temporary();
        emitter_.emit("op", {"notEqual", result, value.operand, "0"});
        return {TypeKind::Bool, result, false};
    }

    void generateStatement(const Stmt& statement) {
        switch (statement.kind) {
            case Stmt::Kind::Empty:
                break;
            case Stmt::Kind::Block: {
                pushScope();
                for (const auto& child : statement.statements) generateStatement(*child);
                popScope();
                break;
            }
            case Stmt::Kind::Variable: {
                const std::string storage = uniqueStorage(statement.name);
                declareLocal(statement.name, {statement.type, storage, true}, statement.location);
                if (statement.expression) {
                    const ExpressionResult value = generateExpression(*statement.expression);
                    requireAssignable(statement.type, value.type, statement.location);
                    emitter_.emit("set", {storage, value.operand});
                } else {
                    emitter_.emit("set", {storage, defaultValue(statement.type)});
                }
                break;
            }
            case Stmt::Kind::Expression:
                generateExpression(*statement.expression);
                break;
            case Stmt::Kind::If:
                generateIf(statement);
                break;
            case Stmt::Kind::While:
                generateWhile(statement);
                break;
            case Stmt::Kind::For:
                generateFor(statement);
                break;
            case Stmt::Kind::Break:
                if (breakLabels_.empty()) fail(statement.location, "break 只能出现在循环中");
                emitter_.emit("jump", {reference(breakLabels_.back()), "always", "0", "0"});
                break;
            case Stmt::Kind::Continue:
                if (continueLabels_.empty()) fail(statement.location, "continue 只能出现在循环中");
                emitter_.emit("jump", {reference(continueLabels_.back()), "always", "0", "0"});
                break;
            case Stmt::Kind::Return:
                generateReturn(statement);
                break;
        }
    }

    void generateIf(const Stmt& statement) {
        const std::string elseLabel = uniqueLabel("else");
        const std::string endLabel = uniqueLabel("if_end");
        const ExpressionResult condition = toBoolean(generateExpression(*statement.condition), statement.location);
        emitter_.emit("jump", {reference(elseLabel), "equal", condition.operand, "false"});
        generateStatement(*statement.thenBranch);
        if (statement.elseBranch) {
            emitter_.emit("jump", {reference(endLabel), "always", "0", "0"});
            emitter_.label(elseLabel);
            generateStatement(*statement.elseBranch);
            emitter_.label(endLabel);
        } else {
            emitter_.label(elseLabel);
        }
    }

    void generateWhile(const Stmt& statement) {
        const std::string conditionLabel = uniqueLabel("while_condition");
        const std::string endLabel = uniqueLabel("while_end");
        emitter_.label(conditionLabel);
        const ExpressionResult condition = toBoolean(generateExpression(*statement.condition), statement.location);
        emitter_.emit("jump", {reference(endLabel), "equal", condition.operand, "false"});
        breakLabels_.push_back(endLabel);
        continueLabels_.push_back(conditionLabel);
        generateStatement(*statement.thenBranch);
        continueLabels_.pop_back();
        breakLabels_.pop_back();
        emitter_.emit("jump", {reference(conditionLabel), "always", "0", "0"});
        emitter_.label(endLabel);
    }

    void generateFor(const Stmt& statement) {
        pushScope();
        if (statement.initializerStatement) generateStatement(*statement.initializerStatement);
        const std::string conditionLabel = uniqueLabel("for_condition");
        const std::string incrementLabel = uniqueLabel("for_increment");
        const std::string endLabel = uniqueLabel("for_end");
        emitter_.label(conditionLabel);
        if (statement.condition) {
            const ExpressionResult condition = toBoolean(generateExpression(*statement.condition), statement.location);
            emitter_.emit("jump", {reference(endLabel), "equal", condition.operand, "false"});
        }
        breakLabels_.push_back(endLabel);
        continueLabels_.push_back(incrementLabel);
        generateStatement(*statement.thenBranch);
        continueLabels_.pop_back();
        breakLabels_.pop_back();
        emitter_.label(incrementLabel);
        if (statement.increment) generateExpression(*statement.increment);
        emitter_.emit("jump", {reference(conditionLabel), "always", "0", "0"});
        emitter_.label(endLabel);
        popScope();
    }

    void generateReturn(const Stmt& statement) {
        if (currentFunction_ == nullptr) fail(statement.location, "return 不在函数内");
        if (currentFunction_->name == "main_loop") {
            if (statement.expression) fail(statement.location, "main_loop 不能返回值");
            emitter_.emit("jump", {reference(mainEntryLabel_), "always", "0", "0"});
            return;
        }

        const FunctionInfo& info = functions_.at(currentFunction_->name);
        if (currentFunction_->returnType == TypeKind::Void) {
            if (statement.expression) fail(statement.location, "void 函数不能返回值");
        } else {
            if (!statement.expression) fail(statement.location, "非 void 函数必须返回值");
            const ExpressionResult value = generateExpression(*statement.expression);
            requireAssignable(currentFunction_->returnType, value.type, statement.location);
            emitter_.emit("set", {info.resultStorage, value.operand});
        }
        emitFunctionReturn();
    }

    void emitFunctionReturn() {
        const FunctionInfo& info = functions_.at(currentFunction_->name);
        emitter_.emit("set", {"@counter", info.returnAddress});
    }

    ExpressionResult generateExpression(const Expr& expression) {
        switch (expression.kind) {
            case Expr::Kind::Number: {
                const bool integral = expression.text.find_first_of(".eE") == std::string::npos;
                return {integral ? TypeKind::Int : TypeKind::Number, expression.text, false};
            }
            case Expr::Kind::String:
                return {TypeKind::String, escapeString(expression.text), false};
            case Expr::Kind::Boolean:
                return {TypeKind::Bool, expression.boolean ? "true" : "false", false};
            case Expr::Kind::Variable: {
                const Symbol& symbol = resolve(expression.text, expression.location);
                return {symbol.type, symbol.storage, symbol.assignable};
            }
            case Expr::Kind::Unary:
                return generateUnary(expression);
            case Expr::Kind::Binary:
                return generateBinary(expression);
            case Expr::Kind::Assign:
                return generateAssignment(expression);
            case Expr::Kind::Call:
                return generateCall(expression);
            case Expr::Kind::Prefix:
                return generateIncrement(expression, true);
            case Expr::Kind::Postfix:
                return generateIncrement(expression, false);
        }
        fail(expression.location, "未知表达式");
    }

    ExpressionResult generateUnary(const Expr& expression) {
        ExpressionResult operand = generateExpression(*expression.right);
        if (expression.text == "!") {
            operand = toBoolean(std::move(operand), expression.location);
            const std::string result = temporary();
            emitter_.emit("op", {"equal", result, operand.operand, "false"});
            return {TypeKind::Bool, result, false};
        }
        if (!isNumeric(operand.type)) fail(expression.location, "一元运算符需要数值操作数");
        if (expression.text == "+") return {operand.type, operand.operand, false};
        const std::string result = temporary();
        emitter_.emit("op", {"sub", result, "0", operand.operand});
        return {operand.type, result, false};
    }

    static TypeKind commonNumericType(TypeKind left, TypeKind right, const std::string& operation) {
        if (operation == "/") return TypeKind::Number;
        if (left == TypeKind::Number || right == TypeKind::Number) return TypeKind::Number;
        if (left == TypeKind::Float || right == TypeKind::Float) return TypeKind::Float;
        return TypeKind::Int;
    }

    ExpressionResult generateBinary(const Expr& expression) {
        if (expression.text == "&&" || expression.text == "||") return generateLogical(expression);

        const ExpressionResult left = generateExpression(*expression.left);
        const ExpressionResult right = generateExpression(*expression.right);
        const std::string result = temporary();

        static const std::unordered_map<std::string, std::string> comparisonOperations = {
            {"==", "equal"}, {"!=", "notEqual"}, {"<", "lessThan"},
            {"<=", "lessThanEq"}, {">", "greaterThan"}, {">=", "greaterThanEq"},
        };
        if (const auto comparison = comparisonOperations.find(expression.text);
            comparison != comparisonOperations.end()) {
            if (expression.text == "==" || expression.text == "!=") {
                const bool compatible = left.type == right.type ||
                                        (isNumeric(left.type) && isNumeric(right.type));
                if (!compatible) fail(expression.location, "不能比较 " + typeName(left.type) + " 和 " + typeName(right.type));
            } else if (!isNumeric(left.type) || !isNumeric(right.type)) {
                fail(expression.location, "顺序比较需要数值操作数");
            }
            emitter_.emit("op", {comparison->second, result, left.operand, right.operand});
            return {TypeKind::Bool, result, false};
        }

        if (!isNumeric(left.type) || !isNumeric(right.type)) {
            fail(expression.location, "算术运算需要数值操作数");
        }
        static const std::unordered_map<std::string, std::string> arithmeticOperations = {
            {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"},
        };
        const auto operation = arithmeticOperations.find(expression.text);
        if (operation == arithmeticOperations.end()) fail(expression.location, "未知二元运算符: " + expression.text);
        if (expression.text == "%" && (left.type != TypeKind::Int || right.type != TypeKind::Int)) {
            fail(expression.location, "% 只接受 int 操作数");
        }
        emitter_.emit("op", {operation->second, result, left.operand, right.operand});
        return {commonNumericType(left.type, right.type, expression.text), result, false};
    }

    ExpressionResult generateLogical(const Expr& expression) {
        const std::string result = temporary();
        const std::string endLabel = uniqueLabel("logical_end");
        ExpressionResult left = toBoolean(generateExpression(*expression.left), expression.location);
        if (expression.text == "&&") {
            emitter_.emit("set", {result, "false"});
            emitter_.emit("jump", {reference(endLabel), "equal", left.operand, "false"});
        } else {
            emitter_.emit("set", {result, "true"});
            emitter_.emit("jump", {reference(endLabel), "notEqual", left.operand, "false"});
        }
        ExpressionResult right = toBoolean(generateExpression(*expression.right), expression.location);
        emitter_.emit("set", {result, right.operand});
        emitter_.label(endLabel);
        return {TypeKind::Bool, result, false};
    }

    ExpressionResult generateAssignment(const Expr& expression) {
        ExpressionResult destination = generateExpression(*expression.left);
        if (!destination.lvalue) fail(expression.location, "赋值左侧必须是可修改变量");
        const ExpressionResult source = generateExpression(*expression.right);

        if (expression.text == "=") {
            requireAssignable(destination.type, source.type, expression.location);
            emitter_.emit("set", {destination.operand, source.operand});
            return {destination.type, destination.operand, false};
        }

        if (!isNumeric(destination.type) || !isNumeric(source.type)) {
            fail(expression.location, "复合赋值需要数值操作数");
        }
        const std::unordered_map<std::string, std::string> operations = {
            {"+=", "add"}, {"-=", "sub"}, {"*=", "mul"}, {"/=", "div"}, {"%=", "mod"},
        };
        const auto operation = operations.find(expression.text);
        if (operation == operations.end()) fail(expression.location, "未知复合赋值运算符");
        const TypeKind resultType = commonNumericType(destination.type, source.type,
                                                      expression.text == "/=" ? "/" : expression.text.substr(0, 1));
        requireAssignable(destination.type, resultType, expression.location);
        if (expression.text == "%=" && (destination.type != TypeKind::Int || source.type != TypeKind::Int)) {
            fail(expression.location, "%= 只接受 int 操作数");
        }
        emitter_.emit("op", {operation->second, destination.operand, destination.operand, source.operand});
        return {destination.type, destination.operand, false};
    }

    ExpressionResult generateIncrement(const Expr& expression, bool prefix) {
        const Expr& targetExpression = prefix ? *expression.right : *expression.left;
        ExpressionResult target = generateExpression(targetExpression);
        if (!target.lvalue || !isNumeric(target.type)) {
            fail(expression.location, "++/-- 需要可修改的数值变量");
        }
        std::string result = target.operand;
        if (!prefix) {
            result = temporary();
            emitter_.emit("set", {result, target.operand});
        }
        emitter_.emit("op", {expression.text == "++" ? "add" : "sub",
                             target.operand, target.operand, "1"});
        return {target.type, result, false};
    }

    ExpressionResult generateCall(const Expr& expression) {
        if (expression.text == "print") return generatePrint(expression);
        if (expression.text == "printflush") return generatePrintFlush(expression);
        if (expression.text == "getlink") return generateGetLink(expression);

        const auto functionIterator = functions_.find(expression.text);
        if (functionIterator == functions_.end()) fail(expression.location, "未定义的函数: " + expression.text);
        const FunctionInfo& info = functionIterator->second;
        const FunctionDecl& function = *info.declaration;
        if (expression.arguments.size() != function.parameters.size()) {
            fail(expression.location, "函数 " + function.name + " 需要 " +
                                      std::to_string(function.parameters.size()) + " 个参数");
        }

        std::vector<ExpressionResult> arguments;
        arguments.reserve(expression.arguments.size());
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            ExpressionResult value = generateExpression(*expression.arguments[index]);
            requireAssignable(function.parameters[index].type, value.type, expression.arguments[index]->location);
            const std::string saved = temporary();
            emitter_.emit("set", {saved, value.operand});
            arguments.push_back({value.type, saved, false});
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            emitter_.emit("set", {info.parameterStorage[index], arguments[index].operand});
        }

        const std::string returnLabel = uniqueLabel("return_from_" + function.name);
        emitter_.emit("set", {info.returnAddress, reference(returnLabel)});
        emitter_.emit("jump", {reference(info.entryLabel), "always", "0", "0"});
        emitter_.label(returnLabel);
        if (function.returnType == TypeKind::Void) return {TypeKind::Void, "", false};
        const std::string result = temporary();
        emitter_.emit("set", {result, info.resultStorage});
        return {function.returnType, result, false};
    }

    ExpressionResult generatePrint(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "print 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments.front());
        if (value.type == TypeKind::Void) fail(expression.location, "不能打印 void 值");
        emitter_.emit("print", {value.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generatePrintFlush(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "printflush 需要一个参数");
        const Expr& targetExpression = *expression.arguments.front();
        if (targetExpression.kind == Expr::Kind::String) {
            const std::string& linkName = targetExpression.text;
            const bool validStart = !linkName.empty() &&
                                    ((linkName.front() >= 'a' && linkName.front() <= 'z') ||
                                     (linkName.front() >= 'A' && linkName.front() <= 'Z') ||
                                     linkName.front() == '_');
            const bool validRest = std::all_of(linkName.begin(), linkName.end(), [](char character) {
                return (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') || character == '_';
            });
            if (!validStart || !validRest) {
                fail(expression.location, "printflush 字符串字面量必须是有效的链接名称");
            }
            emitter_.emit("printflush", {linkName});
            return {TypeKind::Void, "", false};
        }
        const ExpressionResult target = generateExpression(*expression.arguments.front());
        if (target.type != TypeKind::Message) {
            fail(expression.location, "printflush 参数必须是 message 或字符串字面量，实际为 " + typeName(target.type));
        }
        emitter_.emit("printflush", {target.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateGetLink(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "getlink 需要一个参数");
        const ExpressionResult index = generateExpression(*expression.arguments.front());
        if (index.type != TypeKind::Int) {
            fail(expression.location, "getlink 参数必须是 int，实际为 " + typeName(index.type));
        }
        const std::string result = temporary();
        emitter_.emit("getlink", {result, index.operand});
        return {TypeKind::Building, result, false};
    }

    const Program& program_;
    [[maybe_unused]] CompileOptions options_;
    Emitter emitter_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    const FunctionDecl* currentFunction_ = nullptr;
    std::string currentContext_ = "global";
    std::vector<std::string> breakLabels_;
    std::vector<std::string> continueLabels_;
    std::size_t nextLabel_ = 0;
    std::size_t nextStorage_ = 0;
    std::size_t nextTemporary_ = 0;
    const std::string mainEntryLabel_ = "__main_loop_entry";
};

} // namespace

std::string compile(std::string_view source, const CompileOptions& options) {
    Lexer lexer(source);
    Parser parser(lexer.scan());
    Program program = parser.parse();
    return Generator(program, options).generate();
}

} // namespace mdtc
