#include "mdtc/compiler.hpp"

#include <algorithm>
#include <array>
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
    KwDouble,
    KwNumber,
    KwString,
    KwMessage,
    KwBuilding,
    KwDisplay,
    KwMemory,
    KwArr,
    KwArr2d,
    KwColor,
    KwPackedColor,
    KwStruct,
    KwSizeof,
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
    LeftBracket,
    RightBracket,
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
    Dot,
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
            {"double", TokenKind::KwDouble},
            {"number", TokenKind::KwNumber}, {"string", TokenKind::KwString},
            {"message", TokenKind::KwMessage}, {"extern", TokenKind::KwExtern},
            {"building", TokenKind::KwBuilding},
            {"display", TokenKind::KwDisplay},
            {"memory", TokenKind::KwMemory},
            {"arr", TokenKind::KwArr},
            {"arr2d", TokenKind::KwArr2d},
            {"color", TokenKind::KwColor},
            {"packed_color", TokenKind::KwPackedColor},
            {"struct", TokenKind::KwStruct}, {"sizeof", TokenKind::KwSizeof},
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
            case '[': return {TokenKind::LeftBracket, "[", start};
            case ']': return {TokenKind::RightBracket, "]", start};
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
            case '.': return {TokenKind::Dot, ".", start};
        }
        throw CompileError(start.line, start.column,
                           std::string("无法识别的字符: ") + character);
    }

    std::string_view source_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

enum class TypeKind {
    Void, Bool, Int, Float, Number, String, Message, Building, Display, Memory,
    PackedColor, Arr, Arr2d
};

struct Type {
    TypeKind kind = TypeKind::Void;
    std::string structName;
    std::shared_ptr<Type> elementType;

    Type() = default;
    Type(TypeKind kindValue) : kind(kindValue) {}
    explicit Type(std::string name) : kind(TypeKind::Void), structName(std::move(name)) {}
    Type(TypeKind kindValue, Type element)
        : kind(kindValue), elementType(std::make_shared<Type>(std::move(element))) {}

    [[nodiscard]] bool isStruct() const { return !structName.empty(); }
    [[nodiscard]] bool isArray() const { return kind == TypeKind::Arr || kind == TypeKind::Arr2d; }
    [[nodiscard]] bool isRuntimeAggregate() const { return isStruct() || isArray(); }
    bool operator==(const Type& other) const {
        if (kind != other.kind || structName != other.structName) return false;
        if (elementType == nullptr || other.elementType == nullptr) return elementType == other.elementType;
        return *elementType == *other.elementType;
    }
    friend bool operator==(const Type& type, TypeKind kind) { return !type.isStruct() && type.kind == kind; }
    friend bool operator==(TypeKind kind, const Type& type) { return type == kind; }
};

std::string typeName(const Type& type) {
    if (type.isStruct()) return type.structName;
    switch (type.kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Message: return "message";
        case TypeKind::Building: return "building";
        case TypeKind::Display: return "display";
        case TypeKind::Memory: return "memory";
        case TypeKind::PackedColor: return "packed_color";
        case TypeKind::Arr: return "arr<" + typeName(*type.elementType) + ">";
        case TypeKind::Arr2d: return "arr2d<" + typeName(*type.elementType) + ">";
    }
    return "<unknown>";
}

bool isNumeric(const Type& type) {
    return type == TypeKind::Int || type == TypeKind::Float || type == TypeKind::Number;
}

struct OpFunction {
    std::string_view operation;
    std::size_t arity;
};

std::optional<OpFunction> builtinOpFunction(std::string_view name) {
    static const std::unordered_map<std::string_view, OpFunction> functions = {
        {"idiv", {"idiv", 2}}, {"mod", {"mod", 2}}, {"emod", {"emod", 2}}, {"pow", {"pow", 2}},
        {"strict_equal", {"strictEqual", 2}},
        {"shl", {"shl", 2}}, {"shr", {"shr", 2}}, {"ushr", {"ushr", 2}},
        {"bit_or", {"or", 2}}, {"bit_and", {"and", 2}}, {"bit_xor", {"xor", 2}},
        {"bit_not", {"not", 1}},
        {"max", {"max", 2}}, {"min", {"min", 2}},
        {"angle", {"angle", 2}}, {"angle_diff", {"angleDiff", 2}},
        {"len", {"len", 2}}, {"noise", {"noise", 2}},
        {"abs", {"abs", 1}}, {"sign", {"sign", 1}},
        {"log", {"log", 1}}, {"logn", {"logn", 2}}, {"log10", {"log10", 1}},
        {"floor", {"floor", 1}}, {"ceil", {"ceil", 1}}, {"round", {"round", 1}},
        {"sqrt", {"sqrt", 1}}, {"rand", {"rand", 1}},
        {"sin", {"sin", 1}}, {"cos", {"cos", 1}}, {"tan", {"tan", 1}},
        {"asin", {"asin", 1}}, {"acos", {"acos", 1}}, {"atan", {"atan", 1}},
    };
    const auto iterator = functions.find(name);
    if (iterator == functions.end()) return std::nullopt;
    return iterator->second;
}

std::optional<Type> implicitLinkType(std::string_view name) {
    std::size_t suffix = name.size();
    while (suffix > 0 && name[suffix - 1] >= '0' && name[suffix - 1] <= '9') --suffix;
    if (suffix == 0 || suffix == name.size() || name[suffix] == '0') return std::nullopt;

    static const std::unordered_set<std::string_view> prefixes = {
        "accelerator", "acropolis", "afflict", "air", "arc", "assembler", "bank",
        "basalt", "bastion", "battery", "beryllium", "blocks", "bluemat", "bore",
        "boulder", "breach", "bridge", "bush", "canvas", "cell", "centrifuge",
        "chamber", "char", "citadel", "cliff", "cluster", "compressor", "concentrator",
        "condenser", "conduit", "constructor", "container", "conveyor", "crater",
        "craters", "crucible", "crusher", "crux", "cryofluid", "cultivator", "cyclone",
        "dacite", "damaged", "darksand", "dead", "deconstructor", "diffuse", "diode",
        "dirt", "disassembler", "disperse", "display", "distributor", "dome", "door",
        "drill", "driver", "duct", "duo", "electrolyzer", "empty", "extractor",
        "fabricator", "factory", "floor", "foreshadow", "foundation", "furnace", "fuse",
        "gate", "generator", "gigantic", "graphite", "grass", "hail", "heater",
        "hotrock", "huge", "ice", "illuminator", "incinerator", "junction", "kiln",
        "lancer", "link", "loader", "lustre", "magmarock", "malign", "meltdown",
        "melter", "mender", "message", "metal", "mine", "missile", "mixer", "module",
        "moss", "mud", "node", "nucleus", "orbs", "ore", "overlay", "pad", "panel",
        "parallax", "pebbles", "phase", "pine", "plates", "point", "press", "processor",
        "projector", "pulverizer", "pump", "radar", "reactor", "reconstructor",
        "redirector", "redmat", "redweed", "refabricator", "regolith", "rhyolite",
        "ripple", "router", "salt", "salvo", "scathe", "scatter", "scorch", "segment",
        "separator", "shale", "shard", "shrubs", "slag", "smelter", "smite", "snow",
        "sorter", "source", "space", "spawn", "spectre", "split", "stone", "sublimate",
        "surge", "swarmer", "switch", "synthesizer", "tank", "tar", "tendrils",
        "thorium", "thruster", "tiles", "titan", "tower", "tree", "tsunami", "tungsten",
        "turret", "unloader", "vault", "vent", "void", "wall", "water", "wave",
        "weaver", "white", "yellowcoral", "zone",
    };

    const std::string_view prefix = name.substr(0, suffix);
    if (!prefixes.contains(prefix)) return std::nullopt;
    if (prefix == "message") return TypeKind::Message;
    if (prefix == "display") return TypeKind::Display;
    if (prefix == "cell" || prefix == "bank") return TypeKind::Memory;
    return TypeKind::Building;
}

bool isBuiltinFunction(std::string_view name) {
    return builtinOpFunction(name).has_value() ||
           name == "print" || name == "printchar" || name == "putchar" ||
           name == "format" || name == "printf" || name == "printflush" || name == "drawflush" ||
           name == "wait" ||
           name == "rgb" || name == "rgba" || name == "pack_color" || name == "unpack_color" ||
           name == "draw_clear" || name == "draw_color" || name == "draw_col" || name == "set_color" ||
           name == "set_packed_color" ||
           name == "draw_stroke" || name == "set_stroke" ||
           name == "draw_line" || name == "draw_rect" || name == "draw_line_rect" ||
           name == "draw_poly" || name == "draw_line_poly" || name == "draw_triangle" ||
           name == "draw_image" || name == "draw_print" || name == "draw_translate" ||
           name == "draw_scale" || name == "draw_rotate" || name == "draw_reset" ||
           name == "dot" || name == "cross" || name == "getlink";
}

struct Expr {
    enum class Kind { Number, String, Boolean, Variable, Unary, Binary, Assign, Call, Prefix, Postfix,
                      Member, Index, InitializerList, TypedInitializer, Sizeof };

    Kind kind = Kind::Number;
    SourceLocation location;
    std::string text;
    bool boolean = false;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> receiver;
    std::vector<std::unique_ptr<Expr>> arguments;
    Type declaredType;
};

struct Stmt {
    enum class Kind { Empty, Block, Variable, Expression, If, While, For, Break, Continue, Return };

    Kind kind = Kind::Block;
    SourceLocation location;
    Type type;
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
    Type type;
    std::string name;
    SourceLocation location;
};

struct FunctionDecl {
    Type returnType;
    std::string name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Stmt> body;
    SourceLocation location;
};

struct GlobalDecl {
    Type type;
    std::string name;
    bool external = false;
    std::unique_ptr<Expr> initializer;
    SourceLocation location;
};

struct StructField {
    Type type;
    std::string name;
    SourceLocation location;
};

struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
    SourceLocation location;
};

struct Program {
    std::vector<StructDecl> structs;
    std::vector<GlobalDecl> globals;
    std::vector<FunctionDecl> functions;
};

std::vector<StructDecl> builtinStructDeclarations() {
    const SourceLocation location{1, 1};
    StructDecl point{"point", {{TypeKind::Number, "x", location},
                                {TypeKind::Number, "y", location}}, location};
    StructDecl vector{"vec", {{TypeKind::Number, "x", location},
                               {TypeKind::Number, "y", location}}, location};
    StructDecl rectangle{"rect", {{Type("point"), "min", location},
                                   {Type("point"), "max", location}}, location};
    StructDecl color{"color", {{TypeKind::Int, "r", location},
                                {TypeKind::Int, "g", location},
                                {TypeKind::Int, "b", location},
                                {TypeKind::Int, "a", location}}, location};
    return {std::move(point), std::move(vector), std::move(rectangle), std::move(color)};
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
        structTypes_.insert("point");
        structTypes_.insert("vec");
        structTypes_.insert("rect");
    }

    Program parse() {
        Program program;
        program.structs = builtinStructDeclarations();
        while (!check(TokenKind::End)) {
            if (match(TokenKind::KwStruct)) {
                program.structs.push_back(parseStruct(previous()));
                continue;
            }
            const bool external = match(TokenKind::KwExtern);
            const Type type = parseType();
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
    bool checkNext(TokenKind kind) const {
        return position_ + 1 < tokens_.size() && tokens_[position_ + 1].kind == kind;
    }

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
               kind == TokenKind::KwDouble ||
               kind == TokenKind::KwNumber || kind == TokenKind::KwString ||
               kind == TokenKind::KwMessage || kind == TokenKind::KwBuilding ||
               kind == TokenKind::KwDisplay || kind == TokenKind::KwMemory ||
               kind == TokenKind::KwArr || kind == TokenKind::KwArr2d ||
               kind == TokenKind::KwColor ||
               kind == TokenKind::KwPackedColor ||
               (kind == TokenKind::Identifier && structTypes_.contains(current().text));
    }

    bool isTypedInitializerStart() const {
        if (current().kind == TokenKind::KwArr || current().kind == TokenKind::KwArr2d) {
            std::size_t cursor = position_ + 1;
            if (cursor >= tokens_.size() || tokens_[cursor].kind != TokenKind::Less) return false;
            int depth = 0;
            for (; cursor < tokens_.size(); ++cursor) {
                if (tokens_[cursor].kind == TokenKind::Less) ++depth;
                else if (tokens_[cursor].kind == TokenKind::Greater && --depth == 0) {
                    return cursor + 1 < tokens_.size() && tokens_[cursor + 1].kind == TokenKind::LeftBrace;
                }
            }
            return false;
        }
        return isTypeToken(current().kind) && checkNext(TokenKind::LeftBrace);
    }

    Type parseType() {
        const Token token = current();
        ++position_;
        switch (token.kind) {
            case TokenKind::KwVoid: return TypeKind::Void;
            case TokenKind::KwBool: return TypeKind::Bool;
            case TokenKind::KwInt: return TypeKind::Int;
            case TokenKind::KwFloat: return TypeKind::Float;
            case TokenKind::KwDouble: return TypeKind::Number;
            case TokenKind::KwNumber: return TypeKind::Number;
            case TokenKind::KwString: return TypeKind::String;
            case TokenKind::KwMessage: return TypeKind::Message;
            case TokenKind::KwBuilding: return TypeKind::Building;
            case TokenKind::KwDisplay: return TypeKind::Display;
            case TokenKind::KwMemory: return TypeKind::Memory;
            case TokenKind::KwArr:
            case TokenKind::KwArr2d: {
                consume(TokenKind::Less, token.text + " 后需要 <元素类型>");
                const Type element = parseType();
                consume(TokenKind::Greater, token.text + " 的元素类型后需要 >");
                if (element == TypeKind::Void) fail(token, token.text + " 的元素不能是 void");
                return Type(token.kind == TokenKind::KwArr ? TypeKind::Arr : TypeKind::Arr2d, element);
            }
            case TokenKind::KwColor: return Type("color");
            case TokenKind::KwPackedColor: return TypeKind::PackedColor;
            case TokenKind::Identifier:
                if (structTypes_.contains(token.text)) return Type(token.text);
                fail(token, "未知的数据类型: " + token.text);
            default: fail(token, "需要数据类型");
        }
    }

    StructDecl parseStruct(const Token& keyword) {
        const Token name = consume(TokenKind::Identifier, "struct 后需要类型名称");
        if (!structTypes_.insert(name.text).second) fail(name, "重复的结构体名称: " + name.text);
        StructDecl declaration;
        declaration.name = name.text;
        declaration.location = keyword.location;
        consume(TokenKind::LeftBrace, "结构体定义需要左大括号");
        std::unordered_set<std::string> fieldNames;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
            const Type fieldType = parseType();
            const Token fieldName = consume(TokenKind::Identifier, "字段类型后需要名称");
            if (fieldType == TypeKind::Void) fail(fieldName, "字段不能是 void 类型");
            if (!fieldNames.insert(fieldName.text).second) fail(fieldName, "重复的字段名称: " + fieldName.text);
            consume(TokenKind::Semicolon, "字段声明后需要分号");
            declaration.fields.push_back({fieldType, fieldName.text, fieldName.location});
        }
        consume(TokenKind::RightBrace, "结构体定义缺少右大括号");
        consume(TokenKind::Semicolon, "结构体定义后需要分号");
        return declaration;
    }

    FunctionDecl parseFunction(Type returnType, const Token& name) {
        FunctionDecl function;
        function.returnType = returnType;
        function.name = name.text;
        function.location = name.location;
        if (!check(TokenKind::RightParen)) {
            do {
                const Type parameterType = parseType();
                if (parameterType == TypeKind::Void) fail(previous(), "参数不能是 void 类型");
                const Token parameterName = consume(TokenKind::Identifier, "参数类型后需要名称");
                function.parameters.push_back({parameterType, parameterName.text, parameterName.location});
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightParen, "参数列表缺少右括号");
        function.body = parseBlock();
        return function;
    }

    GlobalDecl parseGlobal(Type type, const Token& name, bool external) {
        if (type == TypeKind::Void) fail(name, "变量不能是 void 类型");
        GlobalDecl global;
        global.type = type;
        global.name = name.text;
        global.external = external;
        global.location = name.location;
        if (match(TokenKind::Equal)) global.initializer = parseExpression();
        else if (check(TokenKind::LeftBrace)) global.initializer = parseInitializerList();
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
        if (isTypeToken(current().kind) && !isTypedInitializerStart()) return parseVariable(true);

        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Expression;
        statement->location = current().location;
        statement->expression = parseExpression();
        consume(TokenKind::Semicolon, "表达式后需要分号");
        return statement;
    }

    std::unique_ptr<Stmt> parseVariable(bool consumeSemicolon) {
        const Type type = parseType();
        const Token name = consume(TokenKind::Identifier, "变量类型后需要名称");
        if (type == TypeKind::Void) fail(name, "变量不能是 void 类型");
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Variable;
        statement->location = name.location;
        statement->type = type;
        statement->name = name.text;
        if (match(TokenKind::Equal)) statement->expression = parseExpression();
        else if (check(TokenKind::LeftBrace)) statement->expression = parseInitializerList();
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
        } else if (isTypeToken(current().kind) && !isTypedInitializerStart()) {
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
        if (match(TokenKind::KwSizeof)) {
            const Token keyword = previous();
            consume(TokenKind::LeftParen, "sizeof 后需要左括号");
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Sizeof;
            expression->location = keyword.location;
            if (isTypeToken(current().kind) && !isTypedInitializerStart()) expression->declaredType = parseType();
            else expression->left = parseExpression();
            consume(TokenKind::RightParen, "sizeof 参数后需要右括号");
            return expression;
        }
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
                if (expression->kind != Expr::Kind::Variable && expression->kind != Expr::Kind::Member) {
                    fail(previous(), "只能调用具名函数或内置成员函数");
                }
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->location = expression->location;
                call->text = expression->text;
                if (expression->kind == Expr::Kind::Member) {
                    call->receiver = std::move(expression->left);
                }
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
            } else if (match(TokenKind::Dot)) {
                const Token memberName = consume(TokenKind::Identifier, "点号后需要字段名称");
                auto member = std::make_unique<Expr>();
                member->kind = Expr::Kind::Member;
                member->location = memberName.location;
                member->text = memberName.text;
                member->left = std::move(expression);
                expression = std::move(member);
            } else if (match(TokenKind::LeftBracket)) {
                const Token bracket = previous();
                auto index = std::make_unique<Expr>();
                index->kind = Expr::Kind::Index;
                index->location = bracket.location;
                index->left = std::move(expression);
                index->right = parseExpression();
                consume(TokenKind::RightBracket, "索引表达式缺少右方括号");
                expression = std::move(index);
            } else {
                break;
            }
        }
        return expression;
    }

    std::unique_ptr<Expr> parsePrimary() {
        const Token token = current();
        if (isTypedInitializerStart()) {
            const Type type = parseType();
            if (type == TypeKind::Void) fail(token, "不能构造 void 值");
            auto expression = parseInitializerList();
            expression->kind = Expr::Kind::TypedInitializer;
            expression->location = token.location;
            expression->declaredType = type;
            return expression;
        }
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
        if (check(TokenKind::LeftBrace)) return parseInitializerList();
        fail(token, "需要表达式");
    }

    std::unique_ptr<Expr> parseInitializerList() {
        const Token brace = consume(TokenKind::LeftBrace, "初始化列表需要左大括号");
        auto expression = std::make_unique<Expr>();
        expression->kind = Expr::Kind::InitializerList;
        expression->location = brace.location;
        if (!check(TokenKind::RightBrace)) {
            do {
                expression->arguments.push_back(parseExpression());
            } while (match(TokenKind::Comma) && !check(TokenKind::RightBrace));
        }
        consume(TokenKind::RightBrace, "初始化列表缺少右大括号");
        return expression;
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
    std::unordered_set<std::string> structTypes_;
};

struct IrInstruction {
    enum class Kind { Operation, Label };
    enum class OperandRole { Definition, Value, Label, Metadata };

    Kind kind = Kind::Operation;
    std::string opcode;
    std::string label;
    std::vector<std::string> operands;
    std::vector<OperandRole> operandRoles;

    bool operator==(const IrInstruction&) const = default;

    [[nodiscard]] bool isTerminator() const {
        return kind == Kind::Operation &&
               (opcode == "jump" ||
                (opcode == "set" && !operands.empty() && operands.front() == "@counter"));
    }

    [[nodiscard]] bool hasSideEffects() const {
        if (kind == Kind::Label) return false;
        if (isTerminator()) return true;
        return opcode != "set" && opcode != "op" && opcode != "read" &&
               opcode != "getlink" && opcode != "packcolor" && opcode != "unpackcolor";
    }

    [[nodiscard]] std::vector<std::size_t> definitions() const {
        std::vector<std::size_t> result;
        for (std::size_t index = 0; index < operandRoles.size(); ++index) {
            if (operandRoles[index] == OperandRole::Definition) result.push_back(index);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::size_t> uses() const {
        std::vector<std::size_t> result;
        for (std::size_t index = 0; index < operandRoles.size(); ++index) {
            if (operandRoles[index] == OperandRole::Value) result.push_back(index);
        }
        return result;
    }
};

struct IrBasicBlock {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<std::size_t> successors;
};

class IrBuilder {
public:
    void label(const std::string& name) {
        if (!labels_.emplace(name, instructions_.size()).second) {
            throw std::logic_error("重复的内部标签: " + name);
        }
        instructions_.push_back({IrInstruction::Kind::Label, {}, name, {}, {}});
    }

    void emit(std::string opcode, std::vector<std::string> operands = {}) {
        std::vector<IrInstruction::OperandRole> roles = operandRoles(opcode, operands.size());
        instructions_.push_back({IrInstruction::Kind::Operation, std::move(opcode), {},
                                 std::move(operands), std::move(roles)});
    }

    [[nodiscard]] std::string finish() const {
        std::unordered_map<std::string, std::size_t> instructionLabels;
        std::size_t instructionCount = 0;
        for (const IrInstruction& instruction : instructions_) {
            if (instruction.kind == IrInstruction::Kind::Label) {
                instructionLabels.emplace(instruction.label, instructionCount);
            } else {
                ++instructionCount;
            }
        }
        if (instructionCount > 1000) {
            throw CompileError(1, 1, "生成的程序超过 Mindustry 1000 条指令限制（当前 " +
                                             std::to_string(instructionCount) + " 条）");
        }

        std::ostringstream output;
        for (const IrInstruction& instruction : instructions_) {
            if (instruction.kind == IrInstruction::Kind::Label) continue;
            output << instruction.opcode;
            for (const std::string& operand : instruction.operands) {
                output << ' ';
                if (!operand.empty() && operand.front() == '$') {
                    const std::string labelName = operand.substr(1);
                    const auto iterator = instructionLabels.find(labelName);
                    if (iterator == instructionLabels.end()) {
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

    [[nodiscard]] const std::vector<IrInstruction>& instructions() const { return instructions_; }
    [[nodiscard]] std::size_t operationCount() const {
        return static_cast<std::size_t>(std::count_if(
            instructions_.begin(), instructions_.end(), [](const IrInstruction& instruction) {
                return instruction.kind == IrInstruction::Kind::Operation;
            }));
    }

    void optimizeInlining(const std::vector<std::string>& functionOrder) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const std::string& functionName : functionOrder) {
                const std::string entryLabel = "__function_" + functionName;
                if (!containsLabel(entryLabel)) continue;

                IrBuilder candidate = *this;
                if (!candidate.inlineAllCalls(entryLabel, functionName)) continue;
                candidate.optimizeLocalAssignments();
                if (candidate.operationCount() < operationCount()) {
                    *this = std::move(candidate);
                    changed = true;
                }
            }
        }
    }

    [[nodiscard]] std::vector<IrBasicBlock> basicBlocks() const {
        if (instructions_.empty()) return {};
        std::vector<bool> starts(instructions_.size(), false);
        starts[0] = true;
        std::unordered_map<std::string, std::size_t> labelPositions;
        for (std::size_t index = 0; index < instructions_.size(); ++index) {
            const IrInstruction& instruction = instructions_[index];
            if (instruction.kind == IrInstruction::Kind::Label) {
                starts[index] = true;
                labelPositions[instruction.label] = index;
            } else if (instruction.opcode == "jump" ||
                       (instruction.opcode == "set" && !instruction.operands.empty() &&
                        instruction.operands.front() == "@counter")) {
                if (index + 1 < instructions_.size()) starts[index + 1] = true;
            }
        }

        std::vector<IrBasicBlock> blocks;
        std::vector<std::size_t> blockAt(instructions_.size());
        for (std::size_t index = 0; index < instructions_.size();) {
            if (!starts[index]) {
                ++index;
                continue;
            }
            const std::size_t end = [&] {
                std::size_t cursor = index + 1;
                while (cursor < instructions_.size() && !starts[cursor]) ++cursor;
                return cursor;
            }();
            const std::size_t block = blocks.size();
            blocks.push_back({index, end, {}});
            for (std::size_t cursor = index; cursor < end; ++cursor) blockAt[cursor] = block;
            index = end;
        }

        std::unordered_map<std::string, std::vector<std::size_t>> returnContinuations;
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            const IrInstruction* terminal = nullptr;
            for (std::size_t index = blocks[block].end; index > blocks[block].begin; --index) {
                if (instructions_[index - 1].kind == IrInstruction::Kind::Operation) {
                    terminal = &instructions_[index - 1];
                    break;
                }
            }
            if (terminal != nullptr && terminal->opcode == "jump" &&
                !terminal->operands.empty() &&
                terminal->operands.front().starts_with("$__function_") &&
                block + 1 < blocks.size()) {
                returnContinuations[terminal->operands.front().substr(1) + "_return_address"]
                    .push_back(block + 1);
            }
        }

        for (std::size_t block = 0; block < blocks.size(); ++block) {
            IrBasicBlock& current = blocks[block];
            const IrInstruction* terminal = nullptr;
            for (std::size_t index = current.end; index > current.begin; --index) {
                if (instructions_[index - 1].kind == IrInstruction::Kind::Operation) {
                    terminal = &instructions_[index - 1];
                    break;
                }
            }
            if (terminal != nullptr && terminal->opcode == "jump" && !terminal->operands.empty()) {
                if (!terminal->operands.front().empty() && terminal->operands.front().front() == '$') {
                    const auto target = labelPositions.find(terminal->operands.front().substr(1));
                    if (target != labelPositions.end()) current.successors.push_back(blockAt[target->second]);
                }
                const bool functionCall = terminal->operands.front().starts_with("$__function_");
                if (block + 1 < blocks.size() &&
                    (functionCall ||
                     (terminal->operands.size() >= 2 && terminal->operands[1] != "always"))) {
                    current.successors.push_back(block + 1);
                }
            } else if (terminal != nullptr && terminal->opcode == "set" &&
                       terminal->operands.size() >= 2 && terminal->operands.front() == "@counter") {
                const auto continuations = returnContinuations.find(terminal->operands[1]);
                if (continuations != returnContinuations.end()) {
                    current.successors.insert(current.successors.end(), continuations->second.begin(),
                                              continuations->second.end());
                }
            } else if (block + 1 < blocks.size()) {
                current.successors.push_back(block + 1);
            }
        }
        return blocks;
    }

    void optimizeLocalAssignments() {
        while (true) {
            const std::vector<IrInstruction> previous = instructions_;
            optimizeLocalAssignmentsPass();
            if (instructions_ == previous) return;
        }
    }

    void optimizeLocalAssignmentsPass() {
        const std::vector<IrBasicBlock> initialBlocks = basicBlocks();
        for (const IrBasicBlock& block : initialBlocks) {
            std::unordered_map<std::string, std::string> copies;
            const auto kill = [&](const std::string& name) {
                copies.erase(name);
                for (auto iterator = copies.begin(); iterator != copies.end();) {
                    if (iterator->second == name) iterator = copies.erase(iterator);
                    else ++iterator;
                }
            };
            const auto resolve = [&](const std::string& value) {
                std::string result = value;
                std::unordered_set<std::string> visited;
                while (true) {
                    const auto iterator = copies.find(result);
                    if (iterator == copies.end() || !visited.insert(result).second) return result;
                    result = iterator->second;
                }
            };
            for (std::size_t index = block.begin; index < block.end; ++index) {
                IrInstruction& instruction = instructions_[index];
                if (instruction.kind == IrInstruction::Kind::Label) {
                    copies.clear();
                    continue;
                }
                for (std::size_t operand = 0; operand < instruction.operands.size(); ++operand) {
                    if (instruction.opcode != "draw" && operand < instruction.operandRoles.size() &&
                        instruction.operandRoles[operand] == IrInstruction::OperandRole::Value) {
                        instruction.operands[operand] = resolve(instruction.operands[operand]);
                    }
                }
                const std::vector<std::size_t> definitions = instruction.definitions();
                if (instruction.opcode == "set" && definitions.size() == 1 && instruction.operands.size() >= 2) {
                    const std::string destination = instruction.operands[definitions.front()];
                    const std::string source = resolve(instruction.operands[1]);
                    if (destination == source) {
                        instruction.opcode.clear();
                        instruction.operands.clear();
                        instruction.operandRoles.clear();
                    } else {
                        kill(destination);
                        copies[destination] = source;
                    }
                } else {
                    for (const std::size_t definition : definitions) {
                        if (definition < instruction.operands.size()) kill(instruction.operands[definition]);
                    }
                }
                if (instruction.isTerminator()) copies.clear();
            }
        }

        const std::vector<IrBasicBlock> blocks = basicBlocks();
        std::vector<std::unordered_set<std::string>> uses(blocks.size());
        std::vector<std::unordered_set<std::string>> definitions(blocks.size());
        std::vector<std::unordered_set<std::string>> liveIn(blocks.size());
        std::vector<std::unordered_set<std::string>> liveOut(blocks.size());
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            for (std::size_t index = blocks[block].begin; index < blocks[block].end; ++index) {
                const IrInstruction& instruction = instructions_[index];
                if (instruction.kind == IrInstruction::Kind::Label) continue;
                for (const std::size_t operand : instruction.uses()) {
                    if (operand < instruction.operands.size() && !definitions[block].contains(instruction.operands[operand])) {
                        uses[block].insert(instruction.operands[operand]);
                    }
                }
                for (const std::size_t operand : instruction.definitions()) {
                    if (operand < instruction.operands.size()) definitions[block].insert(instruction.operands[operand]);
                }
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t block = blocks.size(); block-- > 0;) {
                std::unordered_set<std::string> nextOut;
                for (const std::size_t successor : blocks[block].successors) {
                    nextOut.insert(liveIn[successor].begin(), liveIn[successor].end());
                }
                std::unordered_set<std::string> nextIn = uses[block];
                for (const std::string& name : nextOut) {
                    if (!definitions[block].contains(name)) nextIn.insert(name);
                }
                if (nextOut != liveOut[block] || nextIn != liveIn[block]) {
                    liveOut[block] = std::move(nextOut);
                    liveIn[block] = std::move(nextIn);
                    changed = true;
                }
            }
        }

        for (std::size_t block = 0; block < blocks.size(); ++block) {
            std::unordered_set<std::string> live = liveOut[block];
            bool dynamicReturn = false;
            bool functionCall = false;
            for (std::size_t cursor = blocks[block].end; cursor > blocks[block].begin; --cursor) {
                const IrInstruction& terminal = instructions_[cursor - 1];
                if (terminal.kind != IrInstruction::Kind::Operation) continue;
                dynamicReturn = terminal.opcode == "set" && !terminal.operands.empty() &&
                                terminal.operands.front() == "@counter";
                functionCall = terminal.opcode == "jump" && !terminal.operands.empty() &&
                               terminal.operands.front().starts_with("$__function_");
                break;
            }
            if (dynamicReturn || functionCall) {
                for (std::size_t index = blocks[block].begin; index < blocks[block].end; ++index) {
                    for (const std::size_t operand : instructions_[index].definitions()) {
                        if (operand < instructions_[index].operands.size()) {
                            live.insert(instructions_[index].operands[operand]);
                        }
                    }
                }
            }
            for (std::size_t index = blocks[block].end; index-- > blocks[block].begin;) {
                IrInstruction& instruction = instructions_[index];
                if (instruction.kind == IrInstruction::Kind::Label) continue;
                const std::vector<std::size_t> instructionDefinitions = instruction.definitions();
                bool removable = !dynamicReturn && !functionCall && !instruction.hasSideEffects() &&
                                 !instructionDefinitions.empty();
                for (const std::size_t operand : instructionDefinitions) {
                    if (operand >= instruction.operands.size() || live.contains(instruction.operands[operand])) {
                        removable = false;
                    }
                }
                if (removable) {
                    instruction.opcode.clear();
                    instruction.operands.clear();
                    instruction.operandRoles.clear();
                    continue;
                }
                for (const std::size_t operand : instructionDefinitions) {
                    if (operand < instruction.operands.size()) live.erase(instruction.operands[operand]);
                }
                for (const std::size_t operand : instruction.uses()) {
                    if (operand < instruction.operands.size()) live.insert(instruction.operands[operand]);
                }
            }
        }

        instructions_.erase(std::remove_if(instructions_.begin(), instructions_.end(),
                                           [](const IrInstruction& instruction) {
                                               return instruction.kind == IrInstruction::Kind::Operation &&
                                                      instruction.opcode.empty();
                                           }), instructions_.end());
    }

private:
    [[nodiscard]] bool containsLabel(const std::string& labelName) const {
        return std::any_of(instructions_.begin(), instructions_.end(), [&](const IrInstruction& instruction) {
            return instruction.kind == IrInstruction::Kind::Label && instruction.label == labelName;
        });
    }

    bool inlineAllCalls(const std::string& entryLabel, const std::string& functionName) {
        const auto entry = std::find_if(instructions_.begin(), instructions_.end(),
                                        [&](const IrInstruction& instruction) {
                                            return instruction.kind == IrInstruction::Kind::Label &&
                                                   instruction.label == entryLabel;
                                        });
        if (entry == instructions_.end()) return false;
        const std::size_t bodyBegin = static_cast<std::size_t>(entry - instructions_.begin()) + 1;
        std::size_t bodyEnd = bodyBegin;
        while (bodyEnd < instructions_.size()) {
            const IrInstruction& instruction = instructions_[bodyEnd];
            if (instruction.kind == IrInstruction::Kind::Label &&
                (instruction.label.starts_with("__function_") ||
                 instruction.label == "__main_loop_entry")) {
                break;
            }
            ++bodyEnd;
        }
        const std::vector<IrInstruction> body(instructions_.begin() + static_cast<std::ptrdiff_t>(bodyBegin),
                                              instructions_.begin() + static_cast<std::ptrdiff_t>(bodyEnd));
        const std::string returnAddress = "__function_" + functionName + "_return_address";
        const std::string callTarget = "$" + entryLabel;

        std::vector<IrInstruction> rewritten;
        rewritten.reserve(instructions_.size());
        bool foundCall = false;
        std::size_t cloneIndex = 0;
        for (std::size_t index = 0; index < instructions_.size(); ++index) {
            const IrInstruction& instruction = instructions_[index];
            const bool call = instruction.kind == IrInstruction::Kind::Operation &&
                              instruction.opcode == "jump" && !instruction.operands.empty() &&
                              instruction.operands.front() == callTarget;
            if (!call) {
                rewritten.push_back(instruction);
                continue;
            }
            if (rewritten.empty()) throw std::logic_error("函数调用缺少返回地址设置");
            const IrInstruction& returnSetup = rewritten.back();
            if (returnSetup.kind != IrInstruction::Kind::Operation || returnSetup.opcode != "set" ||
                returnSetup.operands.size() < 2 || returnSetup.operands[0] != returnAddress ||
                returnSetup.operands[1].empty() || returnSetup.operands[1].front() != '$') {
                throw std::logic_error("无法识别函数调用返回地址");
            }

            foundCall = true;
            const std::string callReturnLabel = returnSetup.operands[1].substr(1);
            const std::string clonePrefix = "__inline_" + functionName + '_' +
                                            std::to_string(cloneIndex++) + '_';
            std::unordered_map<std::string, std::string> renamedLabels;
            for (const IrInstruction& bodyInstruction : body) {
                if (bodyInstruction.kind == IrInstruction::Kind::Label) {
                    renamedLabels.emplace(bodyInstruction.label, clonePrefix + bodyInstruction.label);
                }
            }
            for (IrInstruction bodyInstruction : body) {
                if (bodyInstruction.kind == IrInstruction::Kind::Label) {
                    bodyInstruction.label = renamedLabels.at(bodyInstruction.label);
                } else {
                    for (std::string& operand : bodyInstruction.operands) {
                        if (operand.empty() || operand.front() != '$') continue;
                        const auto renamed = renamedLabels.find(operand.substr(1));
                        if (renamed != renamedLabels.end()) operand = "$" + renamed->second;
                    }
                    if (bodyInstruction.opcode == "set" && bodyInstruction.operands.size() >= 2 &&
                        bodyInstruction.operands[0] == "@counter" &&
                        bodyInstruction.operands[1] == returnAddress) {
                        bodyInstruction.opcode = "jump";
                        bodyInstruction.operands = {"$" + callReturnLabel, "always", "0", "0"};
                        bodyInstruction.operandRoles = operandRoles(bodyInstruction.opcode,
                                                                    bodyInstruction.operands.size());
                    }
                }
                rewritten.push_back(std::move(bodyInstruction));
            }
        }
        if (!foundCall) return false;

        const auto rewrittenEntry = std::find_if(rewritten.begin(), rewritten.end(),
                                                  [&](const IrInstruction& instruction) {
                                                      return instruction.kind == IrInstruction::Kind::Label &&
                                                             instruction.label == entryLabel;
                                                  });
        if (rewrittenEntry == rewritten.end()) throw std::logic_error("内联后函数入口丢失");
        auto rewrittenEnd = rewrittenEntry + 1;
        while (rewrittenEnd != rewritten.end()) {
            if (rewrittenEnd->kind == IrInstruction::Kind::Label &&
                (rewrittenEnd->label.starts_with("__function_") ||
                 rewrittenEnd->label == "__main_loop_entry")) {
                break;
            }
            ++rewrittenEnd;
        }
        rewritten.erase(rewrittenEntry, rewrittenEnd);
        instructions_ = std::move(rewritten);
        rebuildLabels();
        return true;
    }

    void rebuildLabels() {
        labels_.clear();
        for (std::size_t index = 0; index < instructions_.size(); ++index) {
            const IrInstruction& instruction = instructions_[index];
            if (instruction.kind == IrInstruction::Kind::Label &&
                !labels_.emplace(instruction.label, index).second) {
                throw std::logic_error("重复的内部标签: " + instruction.label);
            }
        }
    }

    static std::vector<IrInstruction::OperandRole> operandRoles(const std::string& opcode,
                                                                 std::size_t count) {
        using Role = IrInstruction::OperandRole;
        std::vector<Role> roles(count, Role::Value);
        if (opcode == "set" || opcode == "read" || opcode == "getlink" || opcode == "packcolor" ||
            opcode == "sensor") {
            if (!roles.empty()) roles[0] = Role::Definition;
        } else if (opcode == "op") {
            if (!roles.empty()) roles[0] = Role::Metadata;
            if (roles.size() > 1) roles[1] = Role::Definition;
        } else if (opcode == "jump") {
            if (!roles.empty()) roles[0] = Role::Label;
            if (roles.size() > 1) roles[1] = Role::Metadata;
        } else if (opcode == "draw") {
            if (!roles.empty()) roles[0] = Role::Metadata;
        } else if (opcode == "unpackcolor") {
            for (std::size_t index = 0; index < std::min<std::size_t>(4, roles.size()); ++index) {
                roles[index] = Role::Definition;
            }
        }
        return roles;
    }

    std::vector<IrInstruction> instructions_;
    std::unordered_map<std::string, std::size_t> labels_;
};

struct Symbol {
    Type type;
    std::string storage;
    bool assignable = true;
    std::vector<std::string> components;
};

struct FunctionInfo {
    const FunctionDecl* declaration = nullptr;
    std::string entryLabel;
    std::string returnAddress;
    std::vector<std::string> resultStorage;
    std::vector<std::vector<std::string>> parameterStorage;
};

struct ExpressionResult {
    Type type;
    std::string operand;
    bool lvalue = false;
    std::vector<std::string> components;
    struct MemoryLocation { std::string handle; std::string address; };
    std::optional<MemoryLocation> memoryLocation;

    ExpressionResult() = default;
    ExpressionResult(Type typeValue, std::string operandValue, bool lvalueValue,
                     std::vector<std::string> componentValues = {},
                     std::optional<MemoryLocation> locationValue = std::nullopt)
        : type(std::move(typeValue)), operand(std::move(operandValue)), lvalue(lvalueValue),
          components(std::move(componentValues)), memoryLocation(std::move(locationValue)) {}
};

class Generator {
public:
    Generator(const Program& program, CompileOptions options)
        : program_(program), options_(options) {
        collectStructDeclarations();
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
            if (function.name != "main_loop" && reachableFunctions_.contains(function.name)) {
                generateFunction(function);
            }
        }
        generateMainLoop();
        emitter_.optimizeLocalAssignments();
        emitter_.optimizeInlining(inlineOrder_);
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

    static std::string defaultValue(const Type& type) {
        switch (type.kind) {
            case TypeKind::Bool:
            case TypeKind::Int:
            case TypeKind::Float:
            case TypeKind::Number: return "0";
            case TypeKind::String: return "\"\"";
            case TypeKind::Message:
            case TypeKind::Building:
            case TypeKind::Display:
            case TypeKind::Memory: return "null";
            case TypeKind::PackedColor: return "0";
            case TypeKind::Arr:
            case TypeKind::Arr2d: return "null";
            case TypeKind::Void: break;
        }
        return "null";
    }

    void collectStructDeclarations() {
        for (const StructDecl& declaration : program_.structs) {
            if (!structs_.emplace(declaration.name, &declaration).second) {
                fail(declaration.location, "重复的结构体名称: " + declaration.name);
            }
        }

        enum class State { Visiting, Complete };
        std::unordered_map<std::string, State> states;
        std::function<void(const Type&, SourceLocation)> validate = [&](const Type& type, SourceLocation location) {
            if (!type.isStruct()) return;
            const auto declaration = structs_.find(type.structName);
            if (declaration == structs_.end()) fail(location, "未知的结构体类型: " + type.structName);
            const auto state = states.find(type.structName);
            if (state != states.end()) {
                if (state->second == State::Visiting) fail(location, "结构体不能直接或间接包含自身: " + type.structName);
                return;
            }
            states[type.structName] = State::Visiting;
            for (const StructField& field : declaration->second->fields) validate(field.type, field.location);
            states[type.structName] = State::Complete;
        };
        for (const StructDecl& declaration : program_.structs) validate(Type(declaration.name), declaration.location);
        std::function<bool(const Type&, std::unordered_set<std::string>&)> storable =
            [&](const Type& type, std::unordered_set<std::string>& visiting) {
                if (type.isArray() || type == TypeKind::Memory || type == TypeKind::String ||
                    type == TypeKind::Message || type == TypeKind::Building || type == TypeKind::Display ||
                    type == TypeKind::Void) return false;
                if (!type.isStruct()) return true;
                if (!visiting.insert(type.structName).second) return true;
                for (const StructField& field : structs_.at(type.structName)->fields) {
                    if (!storable(field.type, visiting)) return false;
                }
                visiting.erase(type.structName);
                return true;
            };
        const auto validateArray = [&](const Type& type, SourceLocation location) {
            if (!type.isArray()) return;
            std::unordered_set<std::string> visiting;
            if (!storable(*type.elementType, visiting)) {
                fail(location, "数组元素类型不能存储在 memory 中: " + typeName(*type.elementType));
            }
        };
        std::function<void(const Expr*)> validateExpression = [&](const Expr* expression) {
            if (expression == nullptr) return;
            if (expression->kind == Expr::Kind::TypedInitializer ||
                (expression->kind == Expr::Kind::Sizeof && !expression->left)) {
                validateArray(expression->declaredType, expression->location);
            }
            validateExpression(expression->left.get());
            validateExpression(expression->right.get());
            for (const auto& argument : expression->arguments) validateExpression(argument.get());
        };
        std::function<void(const Stmt*)> validateStatement = [&](const Stmt* statement) {
            if (statement == nullptr) return;
            if (statement->kind == Stmt::Kind::Variable) validateArray(statement->type, statement->location);
            validateExpression(statement->expression.get());
            validateExpression(statement->condition.get());
            validateExpression(statement->increment.get());
            validateStatement(statement->initializerStatement.get());
            validateStatement(statement->thenBranch.get());
            validateStatement(statement->elseBranch.get());
            for (const auto& child : statement->statements) validateStatement(child.get());
        };
        for (const StructDecl& declaration : program_.structs) {
            for (const StructField& field : declaration.fields) validateArray(field.type, field.location);
        }
        for (const GlobalDecl& global : program_.globals) {
            validateArray(global.type, global.location);
            validateExpression(global.initializer.get());
        }
        for (const FunctionDecl& function : program_.functions) {
            validateArray(function.returnType, function.location);
            for (const Parameter& parameter : function.parameters) validateArray(parameter.type, parameter.location);
            validateStatement(function.body.get());
        }
    }

    [[nodiscard]] std::size_t typeSize(const Type& type) const {
        if (type == TypeKind::Arr) return 2;
        if (type == TypeKind::Arr2d) return 3;
        if (!type.isStruct()) return type == TypeKind::Void ? 0 : 1;
        std::size_t size = 0;
        for (const StructField& field : structs_.at(type.structName)->fields) size += typeSize(field.type);
        return size;
    }

    void appendStorage(const Type& type, const std::string& base, std::vector<std::string>& result) const {
        if (type == TypeKind::Arr) {
            result.push_back(base + "_handle");
            result.push_back(base + "_offset");
            return;
        }
        if (type == TypeKind::Arr2d) {
            result.push_back(base + "_handle");
            result.push_back(base + "_offset");
            result.push_back(base + "_stride");
            return;
        }
        if (!type.isStruct()) {
            result.push_back(base);
            return;
        }
        for (const StructField& field : structs_.at(type.structName)->fields) {
            appendStorage(field.type, base + '_' + field.name, result);
        }
    }

    [[nodiscard]] std::vector<std::string> storageFor(const Type& type, const std::string& base) const {
        std::vector<std::string> result;
        appendStorage(type, base, result);
        return result;
    }

    [[nodiscard]] std::vector<std::string> operandsOf(const ExpressionResult& value) const {
        return value.type.isRuntimeAggregate() ? value.components : std::vector<std::string>{value.operand};
    }

    [[nodiscard]] std::vector<std::string> operandsOf(const Symbol& symbol) const {
        return symbol.type.isRuntimeAggregate() ? symbol.components : std::vector<std::string>{symbol.storage};
    }

    void collectDeclarations() {
        std::unordered_set<std::string> topLevelNames;
        for (const StructDecl& declaration : program_.structs) topLevelNames.insert(declaration.name);
        for (const GlobalDecl& global : program_.globals) {
            if (implicitLinkType(global.name)) {
                fail(global.location, "Mindustry 链接标识符不能被声明: " + global.name);
            }
            if (!topLevelNames.insert(global.name).second) fail(global.location, "重复的全局名称: " + global.name);
        }

        for (const FunctionDecl& function : program_.functions) {
            if (implicitLinkType(function.name)) {
                fail(function.location, "Mindustry 链接标识符不能被声明: " + function.name);
            }
            if (!topLevelNames.insert(function.name).second) fail(function.location, "重复的顶层名称: " + function.name);
            if (isBuiltinFunction(function.name)) {
                fail(function.location, "不能重新定义内置函数 " + function.name);
            }

            FunctionInfo info;
            info.declaration = &function;
            info.entryLabel = "__function_" + function.name;
            info.returnAddress = "__function_" + function.name + "_return_address";
            if (function.returnType != TypeKind::Void) {
                info.resultStorage = storageFor(function.returnType, "__function_" + function.name + "_result");
            }
            std::unordered_set<std::string> parameterNames;
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                const Parameter& parameter = function.parameters[index];
                if (implicitLinkType(parameter.name)) {
                    fail(parameter.location, "Mindustry 链接标识符不能被声明: " + parameter.name);
                }
                if (!parameterNames.insert(parameter.name).second) {
                    fail(parameter.location, "重复的参数名称: " + parameter.name);
                }
                info.parameterStorage.push_back(storageFor(parameter.type,
                    "__function_" + function.name + "_arg" + std::to_string(index)));
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
        if (expression->kind == Expr::Kind::Sizeof) return;
        if (expression->kind == Expr::Kind::Call && !expression->receiver) {
            calls.emplace_back(expression->text, expression->location);
        }
        collectCalls(expression->left.get(), calls);
        collectCalls(expression->right.get(), calls);
        collectCalls(expression->receiver.get(), calls);
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
        std::vector<std::string> initializationRoots;
        for (const FunctionDecl& function : program_.functions) {
            std::vector<std::pair<std::string, SourceLocation>> calls;
            collectCalls(function.body.get(), calls);
            for (const auto& [callee, location] : calls) {
                if (isBuiltinFunction(callee)) continue;
                if (functions_.find(callee) == functions_.end()) fail(location, "未定义的函数: " + callee);
                if (callee == "main_loop") fail(location, "不能显式调用 main_loop");
                graph[function.name].push_back(callee);
            }
        }
        for (const GlobalDecl& global : program_.globals) {
            std::vector<std::pair<std::string, SourceLocation>> calls;
            collectCalls(global.initializer.get(), calls);
            for (const auto& [callee, location] : calls) {
                if (isBuiltinFunction(callee)) continue;
                if (functions_.find(callee) == functions_.end()) fail(location, "未定义的函数: " + callee);
                if (callee == "main_loop") fail(location, "不能显式调用 main_loop");
                initializationRoots.push_back(callee);
            }
        }

        enum class VisitState { Visiting, Complete };
        std::unordered_map<std::string, VisitState> states;
        std::vector<std::string> stack;
        std::function<void(const std::string&)> visit = [&](const std::string& name) {
            reachableFunctions_.insert(name);
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
            if (name != "main_loop") inlineOrder_.push_back(name);
        };

        visit("main_loop");
        for (const std::string& root : initializationRoots) visit(root);
    }

    void declareGlobals() {
        for (const GlobalDecl& global : program_.globals) {
            const std::string storage = global.external ? global.name : "__global_" + global.name;
            if (global.external && global.type.isRuntimeAggregate()) fail(global.location, "extern 结构体或数组变量暂不支持");
            scopes_.front().emplace(global.name, makeSymbol(global.type, storage, !global.external));
        }
    }

    void generateGlobalInitializer(const GlobalDecl& global) {
        if (global.external) return;
        const Symbol& symbol = scopes_.front().at(global.name);
        if (global.initializer) {
            const ExpressionResult value = generateValue(*global.initializer, global.type);
            assignValue(symbol, value);
        } else {
            assignValue(symbol, defaultResult(global.type));
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
                         function.parameters[index].type.isStruct()
                            ? Symbol{function.parameters[index].type, "", true, info.parameterStorage[index]}
                            : Symbol{function.parameters[index].type, info.parameterStorage[index].front(), true, {}},
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
        if (implicitLinkType(name)) {
            fail(location, "Mindustry 链接标识符不能被声明: " + name);
        }
        if (!scopes_.back().emplace(name, std::move(symbol)).second) {
            fail(location, "同一作用域内重复定义变量: " + name);
        }
    }

    Symbol resolve(const std::string& name, SourceLocation location) const {
        for (auto iterator = scopes_.rbegin(); iterator != scopes_.rend(); ++iterator) {
            const auto symbol = iterator->find(name);
            if (symbol != iterator->end()) return symbol->second;
        }
        if (const std::optional<Type> type = implicitLinkType(name)) {
            return {*type, name, false, {}};
        }
        fail(location, "未定义的变量: " + name);
    }

    static bool canAssign(const Type& destination, const Type& source) {
        if (destination == source) return true;
        if (destination == TypeKind::Number && isNumeric(source)) return true;
        if (destination == TypeKind::Float && (source == TypeKind::Int || source == TypeKind::Number)) return true;
        return false;
    }

    static void requireAssignable(const Type& destination, const Type& source, SourceLocation location) {
        if (!canAssign(destination, source)) {
            fail(location, "不能把 " + typeName(source) + " 赋值给 " + typeName(destination));
        }
    }

    [[nodiscard]] Symbol makeSymbol(const Type& type, const std::string& base, bool assignable) const {
        if (type.isRuntimeAggregate()) return {type, "", assignable, storageFor(type, base)};
        return {type, base, assignable, {}};
    }

    [[nodiscard]] ExpressionResult fromSymbol(const Symbol& symbol) const {
        return {symbol.type, symbol.storage, symbol.assignable, symbol.components};
    }

    [[nodiscard]] ExpressionResult defaultResult(const Type& type) const {
        if (!type.isRuntimeAggregate()) return {type, defaultValue(type), false, {}};
        if (type.isArray()) {
            std::vector<std::string> values;
            for (std::size_t index = 0; index < typeSize(type); ++index) values.push_back("null");
            return {type, "", false, std::move(values)};
        }
        std::vector<std::string> components;
        for (const StructField& field : structs_.at(type.structName)->fields) {
            const ExpressionResult fieldDefault = defaultResult(field.type);
            const std::vector<std::string> fieldOperands = operandsOf(fieldDefault);
            components.insert(components.end(), fieldOperands.begin(), fieldOperands.end());
        }
        return {type, "", false, std::move(components)};
    }

    [[nodiscard]] ExpressionResult materialize(const ExpressionResult& value) {
        const std::vector<std::string> source = operandsOf(value);
        std::vector<std::string> saved;
        saved.reserve(source.size());
        for (const std::string& operand : source) {
            const std::string temporaryStorage = temporary();
            emitter_.emit("set", {temporaryStorage, operand});
            saved.push_back(temporaryStorage);
        }
        if (value.type.isRuntimeAggregate()) return {value.type, "", false, std::move(saved)};
        return {value.type, saved.front(), false, {}};
    }

    void assignValue(const Symbol& destination, const ExpressionResult& source) {
        const std::vector<std::string> destinations = operandsOf(destination);
        const std::vector<std::string> sources = operandsOf(source);
        if (destinations.size() != sources.size()) throw std::logic_error("聚合赋值字段数量不一致");
        for (std::size_t index = 0; index < destinations.size(); ++index) {
            emitter_.emit("set", {destinations[index], sources[index]});
        }
    }

    [[nodiscard]] std::string addressAdd(const std::string& base, const std::string& delta) {
        if (delta == "0") return base;
        const std::string result = temporary();
        emitter_.emit("op", {"add", result, base, delta});
        return result;
    }

    [[nodiscard]] std::string indexedAddress(const std::string& base, const std::string& index,
                                              std::size_t elementSize) {
        if (elementSize == 1) return addressAdd(base, index);
        const std::string scaled = temporary();
        emitter_.emit("op", {"mul", scaled, index, std::to_string(elementSize)});
        return addressAdd(base, scaled);
    }

    ExpressionResult generateIndex(const Expr& expression) {
        const ExpressionResult object = generateExpression(*expression.left);
        const ExpressionResult index = generateExpression(*expression.right);
        if (index.type != TypeKind::Int) fail(expression.location, "数组索引必须是 int");
        if (object.type == TypeKind::Memory) {
            const std::string address = index.operand;
            const std::string result = temporary();
            emitter_.emit("read", {result, object.operand, address});
            return {TypeKind::Number, result, true, {}, ExpressionResult::MemoryLocation{object.operand, address}};
        }
        if (object.type != TypeKind::Arr && object.type != TypeKind::Arr2d) {
            fail(expression.location, "索引左侧必须是 memory、arr<T> 或 arr2d<T>");
        }
        const std::string handle = object.components.front();
        std::string address;
        Type elementType;
        if (object.type == TypeKind::Arr) {
            address = indexedAddress(object.components[1], index.operand, typeSize(*object.type.elementType));
            elementType = *object.type.elementType;
        } else {
            const std::string rowOffset = temporary();
            emitter_.emit("op", {"mul", rowOffset, index.operand, object.components[2]});
            address = addressAdd(object.components[1], rowOffset);
            elementType = Type(TypeKind::Arr, *object.type.elementType);
        }
        if (elementType.isArray()) {
            return {elementType, "", false, {handle, address}, std::nullopt};
        }
        const std::size_t size = typeSize(elementType);
        std::vector<std::string> values;
        for (std::size_t offset = 0; offset < size; ++offset) {
            const std::string elementAddress = addressAdd(address, std::to_string(offset));
            const std::string result = temporary();
            emitter_.emit("read", {result, handle, elementAddress});
            values.push_back(result);
        }
        if (elementType.isRuntimeAggregate()) {
            return {elementType, "", true, std::move(values), ExpressionResult::MemoryLocation{handle, address}};
        }
        return {elementType, values.front(), true, {}, ExpressionResult::MemoryLocation{handle, address}};
    }

    void storeMemory(const ExpressionResult& destination, const ExpressionResult& source) {
        if (!destination.memoryLocation) fail(SourceLocation{}, "内部错误：缺少内存位置");
        const auto& location = *destination.memoryLocation;
        const std::vector<std::string> values = operandsOf(source);
        for (std::size_t offset = 0; offset < values.size(); ++offset) {
            emitter_.emit("write", {values[offset], location.handle, addressAdd(location.address, std::to_string(offset))});
        }
    }

    ExpressionResult generateValue(const Expr& expression, const Type& expected) {
        if (expression.kind == Expr::Kind::InitializerList) return generateInitializer(expression, expected);
        ExpressionResult value = generateExpression(expression);
        requireAssignable(expected, value.type, expression.location);
        return value;
    }

    ExpressionResult generateInitializer(const Expr& expression, const Type& expected) {
        if (expected.isArray()) {
            const std::size_t count = expected.kind == TypeKind::Arr ? 2 : 3;
            if (expression.arguments.size() > count) fail(expression.location, typeName(expected) + " 初始化项过多");
            std::vector<std::string> components;
            for (std::size_t index = 0; index < count; ++index) {
                Type componentType = index == 0 ? TypeKind::Memory : TypeKind::Int;
                ExpressionResult value = index < expression.arguments.size()
                    ? generateValue(*expression.arguments[index], componentType)
                    : defaultResult(componentType);
                value = materialize(value);
                components.push_back(value.operand);
            }
            return {expected, "", false, std::move(components)};
        }
        if (!expected.isStruct()) {
            if (expression.arguments.size() > 1) fail(expression.location, "标量初始化列表最多包含一个元素");
            if (expression.arguments.empty()) return defaultResult(expected);
            return materialize(generateValue(*expression.arguments.front(), expected));
        }

        const StructDecl& declaration = *structs_.at(expected.structName);
        if (expression.arguments.size() > declaration.fields.size()) {
            fail(expression.location, "结构体 " + expected.structName + " 的初始化项过多");
        }
        std::vector<std::string> components;
        for (std::size_t index = 0; index < declaration.fields.size(); ++index) {
            ExpressionResult fieldValue = index < expression.arguments.size()
                ? generateValue(*expression.arguments[index], declaration.fields[index].type)
                : defaultResult(declaration.fields[index].type);
            fieldValue = materialize(fieldValue);
            const std::vector<std::string> fieldOperands = operandsOf(fieldValue);
            components.insert(components.end(), fieldOperands.begin(), fieldOperands.end());
        }
        return {expected, "", false, std::move(components)};
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
                declareLocal(statement.name, makeSymbol(statement.type, storage, true), statement.location);
                const Symbol symbol = resolve(statement.name, statement.location);
                if (statement.expression) {
                    const ExpressionResult value = generateValue(*statement.expression, statement.type);
                    assignValue(symbol, value);
                } else {
                    assignValue(symbol, defaultResult(statement.type));
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
            const ExpressionResult value = materialize(generateValue(*statement.expression, currentFunction_->returnType));
            const Symbol result = currentFunction_->returnType.isRuntimeAggregate()
                ? Symbol{currentFunction_->returnType, "", true, info.resultStorage}
                : Symbol{currentFunction_->returnType, info.resultStorage.front(), true, {}};
            assignValue(result, value);
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
                const Symbol symbol = resolve(expression.text, expression.location);
                return fromSymbol(symbol);
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
            case Expr::Kind::Member:
                return generateMember(expression);
            case Expr::Kind::Index:
                return generateIndex(expression);
            case Expr::Kind::InitializerList:
                fail(expression.location, "初始化列表需要明确的目标类型");
            case Expr::Kind::TypedInitializer:
                return generateInitializer(expression, expression.declaredType);
            case Expr::Kind::Sizeof:
                {
                    const Type sizedType = expression.left ? expressionType(*expression.left) : expression.declaredType;
                    if (sizedType == TypeKind::Void) fail(expression.location, "不能对 void 使用 sizeof");
                    return {TypeKind::Int, std::to_string(typeSize(sizedType)), false};
                }
        }
        fail(expression.location, "未知表达式");
    }

    void validateInitializerType(const Expr& expression, const Type& expected) const {
        if (expression.kind != Expr::Kind::InitializerList &&
            expression.kind != Expr::Kind::TypedInitializer) {
            requireAssignable(expected, expressionType(expression), expression.location);
            return;
        }
        if (!expected.isStruct()) {
            if (expected.isArray()) {
                const std::size_t count = expected.kind == TypeKind::Arr ? 2 : 3;
                if (expression.arguments.size() > count) fail(expression.location, typeName(expected) + " 初始化项过多");
                for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                    validateInitializerType(*expression.arguments[index], index == 0 ? Type(TypeKind::Memory) : Type(TypeKind::Int));
                }
                return;
            }
            if (expression.arguments.size() > 1) fail(expression.location, "标量初始化列表最多包含一个元素");
            if (!expression.arguments.empty()) validateInitializerType(*expression.arguments.front(), expected);
            return;
        }
        const StructDecl& declaration = *structs_.at(expected.structName);
        if (expression.arguments.size() > declaration.fields.size()) {
            fail(expression.location, "结构体 " + expected.structName + " 的初始化项过多");
        }
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            validateInitializerType(*expression.arguments[index], declaration.fields[index].type);
        }
    }

    Type builtinOpType(const Expr& expression) const {
        const std::optional<OpFunction> function = builtinOpFunction(expression.text);
        if (!function) fail(expression.location, "未知的 op 函数: " + expression.text);

        const bool coordinateOverload = expression.arguments.size() == 1 &&
            (expression.text == "angle" || expression.text == "len" || expression.text == "noise");
        if (coordinateOverload) {
            const Type argument = expressionType(*expression.arguments[0]);
            const bool valid = argument == Type("vec") ||
                               (expression.text == "noise" && argument == Type("point"));
            if (!valid) fail(expression.location, expression.text + " 单参数形式需要 vec" +
                                                  (expression.text == "noise" ? " 或 point" : ""));
            return TypeKind::Number;
        }
        if (expression.arguments.size() != function->arity) {
            fail(expression.location, expression.text + " 需要 " + std::to_string(function->arity) + " 个参数");
        }

        std::vector<Type> argumentTypes;
        argumentTypes.reserve(expression.arguments.size());
        for (const auto& argument : expression.arguments) argumentTypes.push_back(expressionType(*argument));

        if (expression.text == "strict_equal") {
            const Type& left = argumentTypes[0];
            const Type& right = argumentTypes[1];
            if (left.isStruct() || right.isStruct() ||
                (left != right && !(isNumeric(left) && isNumeric(right)))) {
                fail(expression.location, "strict_equal 参数类型不兼容");
            }
            return TypeKind::Bool;
        }

        const bool bitwise = expression.text == "shl" || expression.text == "shr" ||
                             expression.text == "ushr" || expression.text == "bit_or" ||
                             expression.text == "bit_and" || expression.text == "bit_xor" ||
                             expression.text == "bit_not";
        for (const Type& type : argumentTypes) {
            if (bitwise ? type != TypeKind::Int : !isNumeric(type)) {
                fail(expression.location, expression.text + (bitwise ? " 参数必须是 int" : " 参数必须是数值类型"));
            }
        }
        if (bitwise || expression.text == "idiv" || expression.text == "floor" ||
            expression.text == "ceil" || expression.text == "round") {
            return TypeKind::Int;
        }
        if (expression.text == "mod" || expression.text == "emod" ||
            expression.text == "max" || expression.text == "min") {
            return commonNumericType(argumentTypes[0], argumentTypes[1], expression.text);
        }
        if (expression.text == "abs") return argumentTypes[0];
        return TypeKind::Number;
    }

    Type expressionType(const Expr& expression) const {
        switch (expression.kind) {
            case Expr::Kind::Number:
                return expression.text.find_first_of(".eE") == std::string::npos ? TypeKind::Int : TypeKind::Number;
            case Expr::Kind::String: return TypeKind::String;
            case Expr::Kind::Boolean: return TypeKind::Bool;
            case Expr::Kind::Variable: return resolve(expression.text, expression.location).type;
            case Expr::Kind::Unary: {
                const Type operand = expressionType(*expression.right);
                if (operand == Type("vec") && (expression.text == "+" || expression.text == "-")) return operand;
                if (expression.text == "!") {
                    if (operand != TypeKind::Bool && !isNumeric(operand)) fail(expression.location, "条件需要 bool 或数值类型");
                    return TypeKind::Bool;
                }
                if (!isNumeric(operand)) fail(expression.location, "一元运算符需要数值操作数");
                return operand;
            }
            case Expr::Kind::Binary: {
                const Type left = expressionType(*expression.left);
                const Type right = expressionType(*expression.right);
                if (left.isStruct() || right.isStruct()) {
                    const Type point("point"), vector("vec");
                    if (expression.text == "+") {
                        if (left == vector && right == vector) return vector;
                        if ((left == point && right == vector) || (left == vector && right == point)) return point;
                    } else if (expression.text == "-") {
                        if (left == vector && right == vector) return vector;
                        if (left == point && right == vector) return point;
                        if (left == point && right == point) return vector;
                    } else if (expression.text == "*" &&
                               ((left == vector && isNumeric(right)) ||
                                (isNumeric(left) && right == vector))) {
                        return vector;
                    }
                    fail(expression.location, "该运算符不支持这些聚合类型");
                }
                if (expression.text == "&&" || expression.text == "||") {
                    if ((left != TypeKind::Bool && !isNumeric(left)) || (right != TypeKind::Bool && !isNumeric(right))) {
                        fail(expression.location, "逻辑运算需要 bool 或数值类型");
                    }
                    return TypeKind::Bool;
                }
                if (expression.text == "==" || expression.text == "!=") {
                    if (left.isStruct() || right.isStruct()) fail(expression.location, "结构体暂不支持比较运算");
                    if (left != right && !(isNumeric(left) && isNumeric(right))) {
                        fail(expression.location, "不能比较 " + typeName(left) + " 和 " + typeName(right));
                    }
                    return TypeKind::Bool;
                }
                if (expression.text == "<" || expression.text == "<=" || expression.text == ">" || expression.text == ">=") {
                    if (!isNumeric(left) || !isNumeric(right)) fail(expression.location, "顺序比较需要数值操作数");
                    return TypeKind::Bool;
                }
                if (!isNumeric(left) || !isNumeric(right)) fail(expression.location, "算术运算需要数值操作数");
                if (expression.text == "%" && (left != TypeKind::Int || right != TypeKind::Int)) {
                    fail(expression.location, "% 只接受 int 操作数");
                }
                return commonNumericType(left, right, expression.text);
            }
            case Expr::Kind::Assign: {
                const Type destination = expressionType(*expression.left);
                if (expression.right->kind != Expr::Kind::InitializerList) {
                    const Type source = expressionType(*expression.right);
                    if (expression.text == "=" ) {
                        requireAssignable(destination, source, expression.location);
                    } else if (destination.isStruct()) {
                        const Type point("point"), vector("vec");
                        const bool valid = ((expression.text == "+=" || expression.text == "-=") &&
                                            ((destination == vector && source == vector) ||
                                             (destination == point && source == vector))) ||
                                           (expression.text == "*=" && destination == vector && isNumeric(source));
                        if (!valid) fail(expression.location, "该复合赋值不支持这些聚合类型");
                    } else if (!isNumeric(destination) || !isNumeric(source)) {
                        fail(expression.location, "复合赋值需要数值操作数");
                    }
                }
                return destination;
            }
            case Expr::Kind::Prefix: {
                const Type type = expressionType(*expression.right);
                if (!isNumeric(type)) fail(expression.location, "++/-- 需要数值变量");
                return type;
            }
            case Expr::Kind::Postfix: {
                const Type type = expressionType(*expression.left);
                if (!isNumeric(type)) fail(expression.location, "++/-- 需要数值变量");
                return type;
            }
            case Expr::Kind::Member: {
                const Type objectType = expressionType(*expression.left);
                if (!objectType.isStruct()) fail(expression.location, "点号左侧必须是结构体");
                for (const StructField& field : structs_.at(objectType.structName)->fields) {
                    if (field.name == expression.text) return field.type;
                }
                fail(expression.location, "结构体 " + objectType.structName + " 没有字段 " + expression.text);
            }
            case Expr::Kind::Index: {
                const Type objectType = expressionType(*expression.left);
                const Type indexType = expressionType(*expression.right);
                if (indexType != TypeKind::Int) fail(expression.location, "数组索引必须是 int");
                if (objectType == TypeKind::Memory) return TypeKind::Number;
                if (objectType == TypeKind::Arr) return *objectType.elementType;
                if (objectType == TypeKind::Arr2d) return Type(TypeKind::Arr, *objectType.elementType);
                fail(expression.location, "索引左侧必须是 memory、arr<T> 或 arr2d<T>");
            }
            case Expr::Kind::Call: {
                if (expression.receiver) {
                    const Type receiverType = expressionType(*expression.receiver);
                    if (receiverType != TypeKind::Building) {
                        fail(expression.location, "内置成员函数的接收者必须是 building");
                    }
                    if (expression.text == "enable") {
                        if (expression.arguments.size() != 1) {
                            fail(expression.location, "enable 需要一个参数");
                        }
                        const Type argument = expressionType(*expression.arguments.front());
                        if (argument != TypeKind::Bool && !isNumeric(argument)) {
                            fail(expression.location, "enable 参数必须是 bool 或数值类型");
                        }
                        return TypeKind::Void;
                    }
                    if (expression.text == "get_enabled") {
                        if (!expression.arguments.empty()) {
                            fail(expression.location, "get_enabled 不需要参数");
                        }
                        return TypeKind::Bool;
                    }
                    fail(expression.location, "未知的内置成员函数: " + expression.text);
                }
                if (builtinOpFunction(expression.text)) return builtinOpType(expression);
                if (expression.text == "rgb" || expression.text == "rgba") {
                    const std::size_t expected = expression.text == "rgb" ? 3 : 4;
                    if (expression.arguments.size() != expected) {
                        fail(expression.location, expression.text + " 需要 " + std::to_string(expected) + " 个参数");
                    }
                    for (const auto& argument : expression.arguments) {
                        if (expressionType(*argument) != TypeKind::Int) {
                            fail(argument->location, expression.text + " 参数必须是 int");
                        }
                    }
                    return Type("color");
                }
                if (expression.text == "pack_color") {
                    if (expression.arguments.size() == 1) {
                        if (expressionType(*expression.arguments[0]) != Type("color")) {
                            fail(expression.location, "pack_color 单参数形式需要 color");
                        }
                    } else if (expression.arguments.size() == 3 || expression.arguments.size() == 4) {
                        for (const auto& argument : expression.arguments) {
                            if (expressionType(*argument) != TypeKind::Int) {
                                fail(argument->location, "pack_color 标量参数必须是 int");
                            }
                        }
                    } else {
                        fail(expression.location, "pack_color 需要一个 color、三个 int 或四个 int 参数");
                    }
                    return TypeKind::PackedColor;
                }
                if (expression.text == "unpack_color") {
                    if (expression.arguments.size() != 1 || expressionType(*expression.arguments[0]) != TypeKind::PackedColor) {
                        fail(expression.location, "unpack_color 需要一个 packed_color 参数");
                    }
                    return Type("color");
                }
                if (expression.text == "dot" || expression.text == "cross") {
                    if (expression.arguments.size() != 2) {
                        fail(expression.location, expression.text + " 需要两个参数");
                    }
                    const Type vector("vec");
                    if (expressionType(*expression.arguments[0]) != vector ||
                        expressionType(*expression.arguments[1]) != vector) {
                        fail(expression.location, expression.text + " 参数必须都是 vec");
                    }
                    return TypeKind::Number;
                }
                if (expression.text == "getlink") {
                    if (expression.arguments.size() != 1 || expressionType(*expression.arguments.front()) != TypeKind::Int) {
                        fail(expression.location, "getlink 需要一个 int 参数");
                    }
                    return TypeKind::Building;
                }
                if (isBuiltinFunction(expression.text)) return TypeKind::Void;
                const auto function = functions_.find(expression.text);
                if (function == functions_.end()) fail(expression.location, "未定义的函数: " + expression.text);
                const FunctionDecl& declaration = *function->second.declaration;
                if (expression.arguments.size() != declaration.parameters.size()) {
                    fail(expression.location, "函数 " + declaration.name + " 参数数量错误");
                }
                for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                    if (expression.arguments[index]->kind != Expr::Kind::InitializerList) {
                        requireAssignable(declaration.parameters[index].type,
                                          expressionType(*expression.arguments[index]),
                                          expression.arguments[index]->location);
                    }
                }
                return function->second.declaration->returnType;
            }
            case Expr::Kind::Sizeof: return TypeKind::Int;
            case Expr::Kind::InitializerList:
                fail(expression.location, "无法确定无上下文初始化列表的类型");
            case Expr::Kind::TypedInitializer:
                validateInitializerType(expression, expression.declaredType);
                return expression.declaredType;
        }
        fail(expression.location, "无法确定表达式类型");
    }

    ExpressionResult generateMember(const Expr& expression) {
        const ExpressionResult object = generateExpression(*expression.left);
        if (!object.type.isStruct()) fail(expression.location, "点号左侧必须是结构体");
        const StructDecl& declaration = *structs_.at(object.type.structName);
        std::size_t offset = 0;
        for (const StructField& field : declaration.fields) {
            const std::size_t fieldSize = typeSize(field.type);
            if (field.name == expression.text) {
                const std::vector<std::string> objectOperands = operandsOf(object);
                std::vector<std::string> fieldOperands(objectOperands.begin() + static_cast<std::ptrdiff_t>(offset),
                                                       objectOperands.begin() + static_cast<std::ptrdiff_t>(offset + fieldSize));
                std::optional<ExpressionResult::MemoryLocation> location = object.memoryLocation;
                if (location) location->address = addressAdd(location->address, std::to_string(offset));
                if (field.type.isRuntimeAggregate()) return {field.type, "", object.lvalue, std::move(fieldOperands), location};
                return {field.type, fieldOperands.front(), object.lvalue, {}, location};
            }
            offset += fieldSize;
        }
        fail(expression.location, "结构体 " + object.type.structName + " 没有字段 " + expression.text);
    }

    ExpressionResult generateUnary(const Expr& expression) {
        ExpressionResult operand = generateExpression(*expression.right);
        if (operand.type == Type("vec")) {
            if (expression.text == "+") return {operand.type, "", false, operand.components};
            if (expression.text == "-") {
                std::vector<std::string> components;
                for (const std::string& value : operand.components) {
                    const std::string result = temporary();
                    emitter_.emit("op", {"sub", result, "0", value});
                    components.push_back(result);
                }
                return {operand.type, "", false, std::move(components)};
            }
        }
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

    static Type commonNumericType(const Type& left, const Type& right, const std::string& operation) {
        if (operation == "/") return TypeKind::Number;
        if (left == TypeKind::Number || right == TypeKind::Number) return TypeKind::Number;
        if (left == TypeKind::Float || right == TypeKind::Float) return TypeKind::Float;
        return TypeKind::Int;
    }

    ExpressionResult generateBinary(const Expr& expression) {
        if (expression.text == "&&" || expression.text == "||") return generateLogical(expression);

        const ExpressionResult left = generateExpression(*expression.left);
        const ExpressionResult right = generateExpression(*expression.right);
        if (left.type.isStruct() || right.type.isStruct()) {
            return generateAggregateBinary(expression, left, right);
        }
        const std::string result = temporary();

        static const std::unordered_map<std::string, std::string> comparisonOperations = {
            {"==", "equal"}, {"!=", "notEqual"}, {"<", "lessThan"},
            {"<=", "lessThanEq"}, {">", "greaterThan"}, {">=", "greaterThanEq"},
        };
        if (const auto comparison = comparisonOperations.find(expression.text);
            comparison != comparisonOperations.end()) {
            if (left.type.isStruct() || right.type.isStruct()) {
                fail(expression.location, "结构体暂不支持比较运算");
            }
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

    ExpressionResult generateAggregateBinary(const Expr& expression,
                                               const ExpressionResult& left,
                                               const ExpressionResult& right) {
        const Type point("point");
        const Type vector("vec");
        Type resultType;
        std::string operation;
        bool leftScalar = false;
        bool rightScalar = false;

        if (expression.text == "+") {
            if (left.type == vector && right.type == vector) resultType = vector;
            else if ((left.type == point && right.type == vector) ||
                     (left.type == vector && right.type == point)) resultType = point;
            else fail(expression.location, "加法只支持 vec+vec、point+vec 或 vec+point");
            operation = "add";
        } else if (expression.text == "-") {
            if (left.type == vector && right.type == vector) resultType = vector;
            else if (left.type == point && right.type == vector) resultType = point;
            else if (left.type == point && right.type == point) resultType = vector;
            else fail(expression.location, "减法只支持 vec-vec、point-vec 或 point-point");
            operation = "sub";
        } else if (expression.text == "*") {
            if (left.type == vector && isNumeric(right.type)) {
                resultType = vector;
                rightScalar = true;
            } else if (isNumeric(left.type) && right.type == vector) {
                resultType = vector;
                leftScalar = true;
            } else {
                fail(expression.location, "乘法只支持 vec 与数值标量");
            }
            operation = "mul";
        } else {
            fail(expression.location, "该运算符不支持结构体操作数: " + expression.text);
        }

        const std::vector<std::string> leftOperands = operandsOf(left);
        const std::vector<std::string> rightOperands = operandsOf(right);
        std::vector<std::string> components;
        components.reserve(2);
        for (std::size_t index = 0; index < 2; ++index) {
            const std::string result = temporary();
            emitter_.emit("op", {operation, result,
                                 leftScalar ? left.operand : leftOperands[index],
                                 rightScalar ? right.operand : rightOperands[index]});
            components.push_back(result);
        }
        return {resultType, "", false, std::move(components)};
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

        if (expression.text == "=") {
            ExpressionResult source = generateValue(*expression.right, destination.type);
            if (destination.memoryLocation) {
                source = materialize(source);
                storeMemory(destination, source);
                return {destination.type, destination.operand, false, destination.components};
            }
            if (destination.type.isRuntimeAggregate() || expression.right->kind == Expr::Kind::InitializerList) {
                source = materialize(source);
            }
            const Symbol target{destination.type, destination.operand, true, destination.components};
            assignValue(target, source);
            return {destination.type, destination.operand, false, destination.components};
        }

        const ExpressionResult source = generateExpression(*expression.right);
        if (destination.memoryLocation) {
            if (!isNumeric(destination.type) || !isNumeric(source.type)) {
                fail(expression.location, "内存元素的复合赋值需要数值操作数");
            }
            const std::unordered_map<std::string, std::string> operations = {
                {"+=", "add"}, {"-=", "sub"}, {"*=", "mul"}, {"/=", "div"}, {"%=", "mod"},
            };
            const auto operation = operations.find(expression.text);
            if (operation == operations.end()) fail(expression.location, "未知复合赋值运算符");
            const std::string result = temporary();
            emitter_.emit("op", {operation->second, result, destination.operand, source.operand});
            emitter_.emit("write", {result, destination.memoryLocation->handle, destination.memoryLocation->address});
            return {destination.type, result, false};
        }
        if (destination.type.isStruct()) {
            const Type point("point"), vector("vec");
            std::string operation;
            bool scalarSource = false;
            if ((expression.text == "+=" || expression.text == "-=") &&
                ((destination.type == vector && source.type == vector) ||
                 (destination.type == point && source.type == vector))) {
                operation = expression.text == "+=" ? "add" : "sub";
            } else if (expression.text == "*=" && destination.type == vector && isNumeric(source.type)) {
                operation = "mul";
                scalarSource = true;
            } else {
                fail(expression.location, "该复合赋值不支持这些聚合类型");
            }
            const std::vector<std::string> sourceOperands = operandsOf(source);
            for (std::size_t index = 0; index < destination.components.size(); ++index) {
                emitter_.emit("op", {operation, destination.components[index], destination.components[index],
                                     scalarSource ? source.operand : sourceOperands[index]});
            }
            return {destination.type, "", false, destination.components};
        }

        if (!isNumeric(destination.type) || !isNumeric(source.type)) {
            fail(expression.location, "复合赋值需要数值操作数");
        }
        const std::unordered_map<std::string, std::string> operations = {
            {"+=", "add"}, {"-=", "sub"}, {"*=", "mul"}, {"/=", "div"}, {"%=", "mod"},
        };
        const auto operation = operations.find(expression.text);
        if (operation == operations.end()) fail(expression.location, "未知复合赋值运算符");
        const Type resultType = commonNumericType(destination.type, source.type,
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
        if (target.memoryLocation) {
            emitter_.emit("write", {target.operand, target.memoryLocation->handle, target.memoryLocation->address});
        }
        return {target.type, result, false};
    }

    ExpressionResult generateCall(const Expr& expression) {
        if (expression.receiver) return generateBuiltinMemberCall(expression);
        if (builtinOpFunction(expression.text)) return generateBuiltinOp(expression);
        if (expression.text == "print") return generatePrint(expression);
        if (expression.text == "printchar" || expression.text == "putchar") return generatePrintChar(expression);
        if (expression.text == "format") return generateFormat(expression);
        if (expression.text == "printf") return generatePrintf(expression);
        if (expression.text == "printflush") return generatePrintFlush(expression);
        if (expression.text == "drawflush") return generateDrawFlush(expression);
        if (expression.text == "wait") return generateWait(expression);
        if (expression.text == "rgb" || expression.text == "rgba") return generateColor(expression);
        if (expression.text == "pack_color" || expression.text == "unpack_color") return generateColorConversion(expression);
        if (expression.text == "draw_clear") return generateDrawClear(expression);
        if (expression.text == "draw_color" || expression.text == "set_color") return generateDrawColor(expression);
        if (expression.text == "draw_col" || expression.text == "set_packed_color") return generateDrawCol(expression);
        if (expression.text == "draw_stroke" || expression.text == "set_stroke") return generateDrawStroke(expression);
        if (expression.text == "draw_line") return generateDrawLine(expression);
        if (expression.text == "draw_rect" || expression.text == "draw_line_rect") return generateDrawRect(expression);
        if (expression.text == "draw_poly" || expression.text == "draw_line_poly") return generateDrawPoly(expression);
        if (expression.text == "draw_triangle") return generateDrawTriangle(expression);
        if (expression.text == "draw_image") return generateDrawImage(expression);
        if (expression.text == "draw_print") return generateDrawPrint(expression);
        if (expression.text == "draw_translate" || expression.text == "draw_scale") return generateDrawTransform(expression);
        if (expression.text == "draw_rotate") return generateDrawRotate(expression);
        if (expression.text == "draw_reset") return generateDrawReset(expression);
        if (expression.text == "dot" || expression.text == "cross") return generateVectorProduct(expression);
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
            ExpressionResult value = generateValue(*expression.arguments[index], function.parameters[index].type);
            arguments.push_back(materialize(value));
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const std::vector<std::string> values = operandsOf(arguments[index]);
            for (std::size_t component = 0; component < values.size(); ++component) {
                emitter_.emit("set", {info.parameterStorage[index][component], values[component]});
            }
        }

        const std::string returnLabel = uniqueLabel("return_from_" + function.name);
        emitter_.emit("set", {info.returnAddress, reference(returnLabel)});
        emitter_.emit("jump", {reference(info.entryLabel), "always", "0", "0"});
        emitter_.label(returnLabel);
        if (function.returnType == TypeKind::Void) return {TypeKind::Void, "", false};
        std::vector<std::string> results;
        for (const std::string& resultStorage : info.resultStorage) {
            const std::string result = temporary();
            emitter_.emit("set", {result, resultStorage});
            results.push_back(result);
        }
        if (function.returnType.isRuntimeAggregate()) return {function.returnType, "", false, std::move(results)};
        return {function.returnType, results.front(), false, {}};
    }

    ExpressionResult generateBuiltinMemberCall(const Expr& expression) {
        const ExpressionResult receiver = generateExpression(*expression.receiver);
        if (receiver.type != TypeKind::Building) {
            fail(expression.location, "内置成员函数的接收者必须是 building");
        }
        if (expression.text == "enable") {
            if (expression.arguments.size() != 1) {
                fail(expression.location, "enable 需要一个参数");
            }
            const ExpressionResult value = toBoolean(generateExpression(*expression.arguments.front()),
                                                     expression.arguments.front()->location);
            emitter_.emit("control", {"enabled", receiver.operand, value.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "get_enabled") {
            if (!expression.arguments.empty()) {
                fail(expression.location, "get_enabled 不需要参数");
            }
            const std::string result = temporary();
            emitter_.emit("sensor", {result, receiver.operand, "@enabled"});
            return {TypeKind::Bool, result, false};
        }
        fail(expression.location, "未知的内置成员函数: " + expression.text);
    }

    ExpressionResult generateBuiltinOp(const Expr& expression) {
        const OpFunction function = *builtinOpFunction(expression.text);
        const Type resultType = builtinOpType(expression);
        std::string left;
        std::string right = "0";

        const bool coordinateOverload = expression.arguments.size() == 1 &&
            (expression.text == "angle" || expression.text == "len" || expression.text == "noise");
        if (coordinateOverload) {
            const ExpressionResult value = generateExpression(*expression.arguments[0]);
            left = value.components[0];
            right = value.components[1];
        } else {
            std::vector<ExpressionResult> arguments;
            arguments.reserve(expression.arguments.size());
            for (const auto& argument : expression.arguments) arguments.push_back(generateExpression(*argument));
            left = arguments[0].operand;
            if (arguments.size() == 2) right = arguments[1].operand;
        }

        const std::string result = temporary();
        emitter_.emit("op", {std::string(function.operation), result, left, right});
        return {resultType, result, false};
    }

    ExpressionResult generatePrint(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "print 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments.front());
        if (value.type == TypeKind::Void || value.type.isStruct()) fail(expression.location, "不能直接打印该类型的值");
        emitter_.emit("print", {value.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generatePrintChar(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, expression.text + " 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments.front());
        if (value.type != TypeKind::Int) {
            fail(expression.location, expression.text + " 参数必须是 int，实际为 " + typeName(value.type));
        }
        emitter_.emit("printchar", {value.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateFormat(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "format 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments.front());
        if (value.type == TypeKind::Void || value.type.isStruct()) fail(expression.location, "format 不能接收该类型的值");
        emitter_.emit("format", {value.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generatePrintf(const Expr& expression) {
        if (expression.arguments.empty()) fail(expression.location, "printf 至少需要格式字符串参数");

        std::vector<ExpressionResult> arguments;
        arguments.reserve(expression.arguments.size());
        for (const auto& argument : expression.arguments) {
            const ExpressionResult value = generateExpression(*argument);
            if (value.type == TypeKind::Void || value.type.isStruct()) fail(argument->location, "printf 不能接收该类型的值");
            const std::string saved = temporary();
            emitter_.emit("set", {saved, value.operand});
            arguments.push_back({value.type, saved, false});
        }
        if (arguments.front().type != TypeKind::String) {
            fail(expression.location, "printf 第一个参数必须是 string，实际为 " + typeName(arguments.front().type));
        }

        emitter_.emit("print", {arguments.front().operand});
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            emitter_.emit("format", {arguments[index].operand});
        }
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generatePrintFlush(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "printflush 需要一个参数");
        const Expr& targetExpression = *expression.arguments.front();
        if (targetExpression.kind == Expr::Kind::String) {
            fail(expression.location, "printflush 不接受字符串字面量，请使用 message 标识符或正整数字面量");
        }
        if (targetExpression.kind == Expr::Kind::Number &&
            targetExpression.text.find_first_of(".eE") == std::string::npos) {
            long long index = 0;
            const auto [end, error] = std::from_chars(targetExpression.text.data(),
                                                      targetExpression.text.data() + targetExpression.text.size(),
                                                      index);
            if (error != std::errc{} || end != targetExpression.text.data() + targetExpression.text.size() || index <= 0) {
                fail(expression.location, "printflush 信息板序号必须是正整数字面量");
            }
            emitter_.emit("printflush", {"message" + std::to_string(index)});
            return {TypeKind::Void, "", false};
        }
        const ExpressionResult target = generateExpression(*expression.arguments.front());
        if (target.type != TypeKind::Message) {
            fail(expression.location, "printflush 参数必须是 message 或正整数字面量，实际为 " + typeName(target.type));
        }
        emitter_.emit("printflush", {target.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawFlush(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "drawflush 需要一个参数");
        const Expr& targetExpression = *expression.arguments.front();
        if (targetExpression.kind == Expr::Kind::String) {
            fail(expression.location, "drawflush 不接受字符串字面量，请使用 display 标识符或正整数字面量");
        }
        if (targetExpression.kind == Expr::Kind::Number &&
            targetExpression.text.find_first_of(".eE") == std::string::npos) {
            long long index = 0;
            const auto [end, error] = std::from_chars(targetExpression.text.data(),
                                                      targetExpression.text.data() + targetExpression.text.size(),
                                                      index);
            if (error != std::errc{} || end != targetExpression.text.data() + targetExpression.text.size() || index <= 0) {
                fail(expression.location, "drawflush 显示屏序号必须是正整数字面量");
            }
            emitter_.emit("drawflush", {"display" + std::to_string(index)});
            return {TypeKind::Void, "", false};
        }
        const ExpressionResult target = generateExpression(*expression.arguments.front());
        if (target.type != TypeKind::Display) {
            fail(expression.location, "drawflush 参数必须是 display 或正整数字面量，实际为 " + typeName(target.type));
        }
        emitter_.emit("drawflush", {target.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateWait(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "wait 需要一个参数");
        const ExpressionResult duration = numericArgument(*expression.arguments[0], expression.text);
        emitter_.emit("wait", {duration.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult numericArgument(const Expr& expression, std::string_view function) {
        const ExpressionResult value = generateExpression(expression);
        if (!isNumeric(value.type)) {
            fail(expression.location, std::string(function) + " 参数必须是数值类型，实际为 " + typeName(value.type));
        }
        return value;
    }

    ExpressionResult intArgument(const Expr& expression, std::string_view function) {
        const ExpressionResult value = generateExpression(expression);
        if (value.type != TypeKind::Int) {
            fail(expression.location, std::string(function) + " 参数必须是 int，实际为 " + typeName(value.type));
        }
        return value;
    }

    void emitDraw(std::string type, std::vector<std::string> parameters) {
        if (parameters.size() > 6) throw std::logic_error("draw 参数数量超过六个");
        parameters.resize(6, "0");
        parameters.insert(parameters.begin(), std::move(type));
        emitter_.emit("draw", std::move(parameters));
    }

    ExpressionResult generateColor(const Expr& expression) {
        const std::size_t expected = expression.text == "rgb" ? 3 : 4;
        if (expression.arguments.size() != expected) {
            fail(expression.location, expression.text + " 需要 " + std::to_string(expected) + " 个参数");
        }
        std::vector<std::string> components;
        components.reserve(4);
        for (const auto& argument : expression.arguments) {
            components.push_back(intArgument(*argument, expression.text).operand);
        }
        if (expected == 3) components.push_back("255");
        return {Type("color"), "", false, std::move(components)};
    }

    std::optional<long long> constantInteger(const Expr& expression) const {
        if (expression.kind == Expr::Kind::Number &&
            expression.text.find_first_of(".eE") == std::string::npos) {
            long long value = 0;
            const auto [end, error] = std::from_chars(expression.text.data(),
                                                       expression.text.data() + expression.text.size(), value);
            if (error == std::errc{} && end == expression.text.data() + expression.text.size()) return value;
            return std::nullopt;
        }
        if (expression.kind == Expr::Kind::Unary && expression.right != nullptr &&
            (expression.text == "+" || expression.text == "-")) {
            const std::optional<long long> value = constantInteger(*expression.right);
            if (!value) return std::nullopt;
            if (expression.text == "+") return value;
            if (*value == std::numeric_limits<long long>::min()) return std::nullopt;
            return -*value;
        }
        return std::nullopt;
    }

    std::optional<std::array<int, 4>> constantColor(const Expr& expression) const {
        std::array<int, 4> result = {0, 0, 0, 0};
        const std::vector<std::unique_ptr<Expr>>* components = nullptr;
        if (expression.kind == Expr::Kind::TypedInitializer && expression.declaredType == Type("color")) {
            components = &expression.arguments;
        } else if (expression.kind == Expr::Kind::Call &&
                   (expression.text == "rgb" || expression.text == "rgba")) {
            components = &expression.arguments;
            if (expression.text == "rgb") result[3] = 255;
        } else {
            return std::nullopt;
        }
        if (components->size() > result.size()) return std::nullopt;
        for (std::size_t index = 0; index < components->size(); ++index) {
            const std::optional<long long> value = constantInteger(*components->at(index));
            if (!value) return std::nullopt;
            result[index] = static_cast<int>(std::clamp(*value, 0LL, 255LL));
        }
        return result;
    }

    std::optional<std::string> constantPackedColor(const Expr& expression) const {
        std::array<int, 4> components = {0, 0, 0, 255};
        if (expression.arguments.size() == 1) {
            const std::optional<std::array<int, 4>> color = constantColor(*expression.arguments.front());
            if (!color) return std::nullopt;
            components = *color;
        } else if (expression.arguments.size() == 3 || expression.arguments.size() == 4) {
            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                const std::optional<long long> value = constantInteger(*expression.arguments[index]);
                if (!value) return std::nullopt;
                components[index] = static_cast<int>(std::clamp(*value, 0LL, 255LL));
            }
        } else {
            return std::nullopt;
        }
        std::ostringstream literal;
        literal << '%' << std::hex << std::setfill('0');
        for (const int component : components) literal << std::setw(2) << component;
        return literal.str();
    }

    ExpressionResult generateColorConversion(const Expr& expression) {
        if (expression.text == "pack_color") {
            if (const std::optional<std::string> literal = constantPackedColor(expression)) {
                return {TypeKind::PackedColor, *literal, false};
            }
            std::vector<std::string> components;
            if (expression.arguments.size() == 1) {
                const ExpressionResult value = generateExpression(*expression.arguments[0]);
                if (value.type != Type("color")) fail(expression.location, "pack_color 单参数形式需要 color");
                components = value.components;
            } else if (expression.arguments.size() == 3 || expression.arguments.size() == 4) {
                components.reserve(4);
                for (const auto& argument : expression.arguments) {
                    components.push_back(intArgument(*argument, expression.text).operand);
                }
                if (components.size() == 3) components.push_back("255");
            } else {
                fail(expression.location, "pack_color 需要一个 color、三个 int 或四个 int 参数");
            }
            std::vector<std::string> normalized;
            normalized.reserve(4);
            for (const std::string& operand : components) {
                const std::string component = temporary();
                emitter_.emit("op", {"div", component, operand, "255"});
                normalized.push_back(component);
            }
            const std::string result = temporary();
            emitter_.emit("packcolor", {result, normalized[0], normalized[1], normalized[2], normalized[3]});
            return {TypeKind::PackedColor, result, false};
        }
        if (expression.arguments.size() != 1) fail(expression.location, "unpack_color 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments[0]);
        if (value.type != TypeKind::PackedColor) fail(expression.location, "unpack_color 参数必须是 packed_color");
        std::vector<std::string> components(4);
        for (std::string& component : components) component = temporary();
        emitter_.emit("unpackcolor", {components[0], components[1], components[2], components[3], value.operand});
        for (std::string& component : components) {
            emitter_.emit("op", {"mul", component, component, "255"});
            emitter_.emit("op", {"round", component, component, "0"});
        }
        return {Type("color"), "", false, std::move(components)};
    }

    std::string drawAlphaOperand(std::string alpha) {
        if (alpha == "0") {
            alpha = temporary();
            emitter_.emit("set", {alpha, "0"});
        }
        return alpha;
    }

    ExpressionResult generateDrawClear(const Expr& expression) {
        if (expression.arguments.size() == 1) {
            const ExpressionResult color = generateExpression(*expression.arguments[0]);
            if (color.type != Type("color")) fail(expression.location, "draw_clear 单参数形式需要 color");
            emitDraw("clear", {color.components[0], color.components[1], color.components[2]});
            return {TypeKind::Void, "", false};
        }
        if (expression.arguments.size() != 3) fail(expression.location, "draw_clear 需要一个 color 或三个 int 参数");
        std::vector<std::string> values;
        for (const auto& argument : expression.arguments) values.push_back(intArgument(*argument, expression.text).operand);
        emitDraw("clear", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawColor(const Expr& expression) {
        if (expression.arguments.size() == 1) {
            const ExpressionResult color = generateExpression(*expression.arguments[0]);
            if (color.type != Type("color")) fail(expression.location, expression.text + " 单参数形式需要 color");
            emitDraw("color", {color.components[0], color.components[1], color.components[2],
                                drawAlphaOperand(color.components[3])});
            return {TypeKind::Void, "", false};
        }
        if (expression.arguments.size() != 4) fail(expression.location, expression.text + " 需要一个 color 或四个 int 参数");
        std::vector<ExpressionResult> values;
        values.reserve(4);
        for (const auto& argument : expression.arguments) values.push_back(intArgument(*argument, expression.text));

        const std::string alpha = drawAlphaOperand(values[3].operand);
        emitDraw("color", {values[0].operand, values[1].operand, values[2].operand, alpha});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawCol(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, expression.text + " 需要一个 packed_color 参数");
        const ExpressionResult color = generateExpression(*expression.arguments[0]);
        if (color.type != TypeKind::PackedColor) fail(expression.location, expression.text + " 参数必须是 packed_color");
        emitDraw("col", {color.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawStroke(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, expression.text + " 需要一个参数");
        const ExpressionResult width = generateExpression(*expression.arguments.front());
        if (width.type != TypeKind::Int) {
            fail(expression.location, expression.text + " 参数必须是 int，实际为 " + typeName(width.type));
        }
        emitDraw("stroke", {width.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawLine(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 2) {
            const ExpressionResult start = generateExpression(*expression.arguments[0]);
            const ExpressionResult end = generateExpression(*expression.arguments[1]);
            if (start.type != Type("point") || end.type != Type("point")) {
                fail(expression.location, "draw_line 双参数形式需要两个 point");
            }
            values = {start.components[0], start.components[1], end.components[0], end.components[1]};
        } else if (expression.arguments.size() == 4) {
            for (const auto& argument : expression.arguments) values.push_back(numericArgument(*argument, expression.text).operand);
        } else {
            fail(expression.location, "draw_line 需要两个 point 或四个数值参数");
        }
        emitDraw("line", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawRect(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 1) {
            const ExpressionResult rectangle = generateExpression(*expression.arguments[0]);
            if (rectangle.type != Type("rect")) fail(expression.location, expression.text + " 单参数形式需要 rect");
            const std::string width = temporary();
            const std::string height = temporary();
            emitter_.emit("op", {"sub", width, rectangle.components[2], rectangle.components[0]});
            emitter_.emit("op", {"sub", height, rectangle.components[3], rectangle.components[1]});
            values = {rectangle.components[0], rectangle.components[1], width, height};
        } else if (expression.arguments.size() == 2) {
            const ExpressionResult origin = generateExpression(*expression.arguments[0]);
            const ExpressionResult size = generateExpression(*expression.arguments[1]);
            if (origin.type != Type("point") || size.type != Type("vec")) {
                fail(expression.location, expression.text + " 双参数形式需要 point 和 vec");
            }
            values = {origin.components[0], origin.components[1], size.components[0], size.components[1]};
        } else if (expression.arguments.size() == 4) {
            for (const auto& argument : expression.arguments) values.push_back(numericArgument(*argument, expression.text).operand);
        } else {
            fail(expression.location, expression.text + " 需要 rect、point/vec 或四个数值参数");
        }
        emitDraw(expression.text == "draw_rect" ? "rect" : "lineRect", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawPoly(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 4) {
            const ExpressionResult center = generateExpression(*expression.arguments[0]);
            if (center.type != Type("point")) fail(expression.location, expression.text + " 四参数形式的首参数必须是 point");
            values = {center.components[0], center.components[1]};
            values.push_back(intArgument(*expression.arguments[1], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[2], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[3], expression.text).operand);
        } else if (expression.arguments.size() == 5) {
            values.push_back(numericArgument(*expression.arguments[0], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[1], expression.text).operand);
            values.push_back(intArgument(*expression.arguments[2], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[3], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[4], expression.text).operand);
        } else {
            fail(expression.location, expression.text + " 需要 point 加三个参数，或五个标量参数");
        }
        emitDraw(expression.text == "draw_poly" ? "poly" : "linePoly", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawTriangle(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 3) {
            for (const auto& argument : expression.arguments) {
                const ExpressionResult point = generateExpression(*argument);
                if (point.type != Type("point")) fail(expression.location, "draw_triangle 三参数形式需要三个 point");
                values.insert(values.end(), point.components.begin(), point.components.end());
            }
        } else if (expression.arguments.size() == 6) {
            for (const auto& argument : expression.arguments) values.push_back(numericArgument(*argument, expression.text).operand);
        } else {
            fail(expression.location, "draw_triangle 需要三个 point 或六个数值参数");
        }
        emitDraw("triangle", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawImage(const Expr& expression) {
        std::vector<std::string> values;
        std::size_t sourceIndex = 0;
        if (expression.arguments.size() == 4) {
            const ExpressionResult center = generateExpression(*expression.arguments[0]);
            if (center.type != Type("point")) fail(expression.location, "draw_image 四参数形式的首参数必须是 point");
            values = {center.components[0], center.components[1]};
            sourceIndex = 1;
        } else if (expression.arguments.size() == 5) {
            values.push_back(numericArgument(*expression.arguments[0], expression.text).operand);
            values.push_back(numericArgument(*expression.arguments[1], expression.text).operand);
            sourceIndex = 2;
        } else {
            fail(expression.location, "draw_image 需要 point/display/size/rotation 或五个标量参数");
        }
        const ExpressionResult source = generateExpression(*expression.arguments[sourceIndex]);
        if (source.type != TypeKind::Display) {
            fail(expression.arguments[sourceIndex]->location, "draw_image 当前只支持 display 图像源");
        }
        values.push_back(source.operand);
        values.push_back(numericArgument(*expression.arguments[sourceIndex + 1], expression.text).operand);
        values.push_back(numericArgument(*expression.arguments[sourceIndex + 2], expression.text).operand);
        emitDraw("image", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawPrint(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 2) {
            const ExpressionResult position = generateExpression(*expression.arguments[0]);
            if (position.type != Type("point")) fail(expression.location, "draw_print 双参数形式需要 point 和 int");
            values = {position.components[0], position.components[1], intArgument(*expression.arguments[1], expression.text).operand};
        } else if (expression.arguments.size() == 3) {
            values = {numericArgument(*expression.arguments[0], expression.text).operand,
                      numericArgument(*expression.arguments[1], expression.text).operand,
                      intArgument(*expression.arguments[2], expression.text).operand};
        } else {
            fail(expression.location, "draw_print 需要 point/int 或三个标量参数");
        }
        emitDraw("print", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawTransform(const Expr& expression) {
        std::vector<std::string> values;
        if (expression.arguments.size() == 1) {
            const ExpressionResult vector = generateExpression(*expression.arguments[0]);
            if (vector.type != Type("vec")) fail(expression.location, expression.text + " 单参数形式需要 vec");
            values = vector.components;
        } else if (expression.arguments.size() == 2) {
            values = {numericArgument(*expression.arguments[0], expression.text).operand,
                      numericArgument(*expression.arguments[1], expression.text).operand};
        } else {
            fail(expression.location, expression.text + " 需要一个 vec 或两个数值参数");
        }
        emitDraw(expression.text == "draw_translate" ? "translate" : "scale", std::move(values));
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawRotate(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "draw_rotate 需要一个数值参数");
        const ExpressionResult degrees = numericArgument(*expression.arguments[0], expression.text);
        emitDraw("rotate", {"0", "0", degrees.operand});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateDrawReset(const Expr& expression) {
        if (!expression.arguments.empty()) fail(expression.location, "draw_reset 不接受参数");
        emitDraw("reset", {});
        return {TypeKind::Void, "", false};
    }

    ExpressionResult generateVectorProduct(const Expr& expression) {
        if (expression.arguments.size() != 2) fail(expression.location, expression.text + " 需要两个参数");
        const ExpressionResult left = generateExpression(*expression.arguments[0]);
        const ExpressionResult right = generateExpression(*expression.arguments[1]);
        const Type vector("vec");
        if (left.type != vector || right.type != vector) {
            fail(expression.location, expression.text + " 参数必须都是 vec");
        }

        const std::string first = temporary();
        const std::string second = temporary();
        const std::string result = temporary();
        if (expression.text == "dot") {
            emitter_.emit("op", {"mul", first, left.components[0], right.components[0]});
            emitter_.emit("op", {"mul", second, left.components[1], right.components[1]});
            emitter_.emit("op", {"add", result, first, second});
        } else {
            emitter_.emit("op", {"mul", first, left.components[0], right.components[1]});
            emitter_.emit("op", {"mul", second, left.components[1], right.components[0]});
            emitter_.emit("op", {"sub", result, first, second});
        }
        return {TypeKind::Number, result, false};
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
    IrBuilder emitter_;
    std::unordered_map<std::string, const StructDecl*> structs_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::unordered_set<std::string> reachableFunctions_;
    std::vector<std::string> inlineOrder_;
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
