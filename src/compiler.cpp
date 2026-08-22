#include "mdtc/compiler.hpp"
#include "preprocessor.hpp"

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
    BuiltinConstant,
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
    KwPosc,
    KwItem,
    KwLiquid,
    KwBlock,
    KwUnit,
    KwUnitKind,
    KwTeam,
    KwSensor,
    KwSensorValue,
    KwRadarFilter,
    KwRadarSort,
    KwDisplay,
    KwMemory,
    KwArr,
    KwArr2d,
    KwColor,
    KwPackedColor,
    KwRestrict,
    KwStruct,
    KwSizeof,
    KwExtern,
    KwIf,
    KwElse,
    KwSwitch,
    KwCase,
    KwDefault,
    KwWhile,
    KwFor,
    KwBreak,
    KwContinue,
    KwReturn,
    KwTrue,
    KwFalse,
    KwNull,
    KwThis,
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
    Ampersand,
    OrOr,
    Question,
    Colon,
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
        if (source_.starts_with("\xEF\xBB\xBF")) position_ = 3;
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
    struct CodePoint {
        char32_t value;
        std::size_t bytes;
    };

    bool atEnd() const { return position_ >= source_.size(); }

    char peek(std::size_t offset = 0) const {
        const std::size_t index = position_ + offset;
        return index < source_.size() ? source_[index] : '\0';
    }

    [[noreturn]] void invalidUtf8() const {
        throw CompileError(line_, column_, "非法 UTF-8 编码");
    }

    CodePoint decode(std::size_t position) const {
        if (position >= source_.size()) return {U'\0', 0};
        const auto byte = [&](std::size_t offset) {
            return static_cast<unsigned char>(source_[position + offset]);
        };
        const unsigned char first = byte(0);
        if (first < 0x80) return {first, 1};

        std::size_t length = 0;
        char32_t value = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
            value = first & 0x1F;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
            value = first & 0x0F;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
            value = first & 0x07;
        } else {
            invalidUtf8();
        }
        if (position + length > source_.size()) invalidUtf8();
        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char continuation = byte(offset);
            if ((continuation & 0xC0) != 0x80) invalidUtf8();
            value = (value << 6) | (continuation & 0x3F);
        }
        if ((length == 3 && value < 0x800) || (length == 4 && value < 0x10000) ||
            (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
            invalidUtf8();
        }
        return {value, length};
    }

    char32_t peekCodePoint() const { return decode(position_).value; }

    char32_t advance() {
        const CodePoint codePoint = decode(position_);
        position_ += codePoint.bytes;
        if (codePoint.value == U'\n' || codePoint.value == 0x2028 ||
            codePoint.value == 0x2029) {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return codePoint.value;
    }

    SourceLocation location() const { return {line_, column_}; }

    bool match(char expected) {
        if (peek() != expected) return false;
        advance();
        return true;
    }

    void skipWhitespaceAndComments() {
        for (;;) {
            while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n' ||
                   isUnicodeWhitespace(peekCodePoint())) advance();
            if (peek() == '/' && peek(1) == '/') {
                while (!atEnd() && peekCodePoint() != U'\n' &&
                       peekCodePoint() != 0x2028 && peekCodePoint() != 0x2029) advance();
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

    static bool isUnicodeWhitespace(char32_t character) {
        return character == 0x0085 || character == 0x00A0 || character == 0x1680 ||
               (character >= 0x2000 && character <= 0x200A) ||
               character == 0x2028 || character == 0x2029 || character == 0x202F ||
               character == 0x205F || character == 0x3000 || character == 0xFEFF;
    }

    static bool isUnicodePunctuation(char32_t character) {
        return (character >= 0x2000 && character <= 0x206F) ||
               (character >= 0x2E00 && character <= 0x2E7F) ||
               (character >= 0x3000 && character <= 0x303F) ||
               (character >= 0xFE10 && character <= 0xFE1F) ||
               (character >= 0xFE30 && character <= 0xFE4F) ||
               (character >= 0xFF01 && character <= 0xFF0F) ||
               (character >= 0xFF1A && character <= 0xFF20) ||
               (character >= 0xFF3B && character <= 0xFF40) ||
               (character >= 0xFF5B && character <= 0xFF65);
    }

    static bool isIdentifierStart(char32_t character) {
        if ((character >= U'a' && character <= U'z') ||
            (character >= U'A' && character <= U'Z') || character == U'_') return true;
        return character >= 0x80 && !isUnicodeWhitespace(character) &&
               !isUnicodePunctuation(character);
    }

    static bool isIdentifierPart(char32_t character) {
        return isIdentifierStart(character) || (character >= U'0' && character <= U'9');
    }

    Token scanIdentifier(SourceLocation start, std::size_t begin) {
        while (isIdentifierPart(peekCodePoint())) advance();
        std::string text(source_.substr(begin, position_ - begin));
        static const std::unordered_map<std::string, TokenKind> keywords = {
            {"void", TokenKind::KwVoid}, {"bool", TokenKind::KwBool},
            {"int", TokenKind::KwInt}, {"float", TokenKind::KwFloat},
            {"double", TokenKind::KwDouble},
            {"number", TokenKind::KwNumber}, {"string", TokenKind::KwString},
            {"message", TokenKind::KwMessage}, {"extern", TokenKind::KwExtern},
            {"building", TokenKind::KwBuilding},
            {"posc", TokenKind::KwPosc},
            {"item", TokenKind::KwItem},
            {"liquid", TokenKind::KwLiquid},
            {"block", TokenKind::KwBlock},
            {"unit", TokenKind::KwUnit},
            {"unit_kind", TokenKind::KwUnitKind},
            {"team", TokenKind::KwTeam},
            {"sensor", TokenKind::KwSensor},
            {"sensor_value", TokenKind::KwSensorValue},
            {"radar_filter", TokenKind::KwRadarFilter},
            {"radar_sort", TokenKind::KwRadarSort},
            {"display", TokenKind::KwDisplay},
            {"memory", TokenKind::KwMemory},
            {"arr", TokenKind::KwArr},
            {"arr2d", TokenKind::KwArr2d},
            {"color", TokenKind::KwColor},
            {"packed_color", TokenKind::KwPackedColor},
            {"restrict", TokenKind::KwRestrict},
            {"struct", TokenKind::KwStruct}, {"sizeof", TokenKind::KwSizeof},
            {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
            {"switch", TokenKind::KwSwitch}, {"case", TokenKind::KwCase},
            {"default", TokenKind::KwDefault},
            {"while", TokenKind::KwWhile}, {"for", TokenKind::KwFor},
            {"break", TokenKind::KwBreak}, {"continue", TokenKind::KwContinue},
            {"return", TokenKind::KwReturn}, {"true", TokenKind::KwTrue},
            {"false", TokenKind::KwFalse}, {"null", TokenKind::KwNull},
            {"this", TokenKind::KwThis},
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
            const std::size_t characterStart = position_;
            const char32_t character = advance();
            if (character == U'\\') {
                if (atEnd()) break;
                const char32_t escaped = advance();
                switch (escaped) {
                    case U'n': value.push_back('\n'); break;
                    case U't': value.push_back('\t'); break;
                    case U'r': value.push_back('\r'); break;
                    case U'"': value.push_back('"'); break;
                    case U'\\': value.push_back('\\'); break;
                    default:
                        throw CompileError(start.line, start.column, "不支持的字符串转义");
                }
            } else {
                value.append(source_.substr(characterStart, position_ - characterStart));
            }
        }
        if (atEnd()) throw CompileError(start.line, start.column, "未结束的字符串");
        advance();
        return {TokenKind::StringLiteral, std::move(value), start};
    }

    Token scanToken() {
        const SourceLocation start = location();
        const std::size_t begin = position_;
        const char32_t character = advance();
        if (isIdentifierStart(character)) return scanIdentifier(start, begin);
        if (character >= U'0' && character <= U'9') return scanNumber(start);

        switch (character) {
            case '@': {
                if (!isIdentifierStart(peek())) {
                    throw CompileError(start.line, start.column, "@ 后需要内置常量名称");
                }
                const std::size_t begin = position_ - 1;
                advance();
                while (isIdentifierPart(peek()) || peek() == '-') advance();
                return {TokenKind::BuiltinConstant,
                        std::string(source_.substr(begin, position_ - begin)), start};
            }
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
                return {TokenKind::Ampersand, "&", start};
            case '|':
                if (match('|')) return {TokenKind::OrOr, "||", start};
                break;
            case '?': return {TokenKind::Question, "?", start};
            case ':': return {TokenKind::Colon, ":", start};
            case '.': return {TokenKind::Dot, ".", start};
        }
        throw CompileError(start.line, start.column,
                           "无法识别的字符: " +
                               std::string(source_.substr(begin, position_ - begin)));
    }

    std::string_view source_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

enum class TypeKind {
    Void, Null, Bool, Int, Float, Number, String, Message, Building, Posc, Display, Memory,
    Item, Liquid, Block, Unit, UnitKind, Team, Sensor, SensorValue, RadarFilter, RadarSort,
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
        case TypeKind::Null: return "null";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Message: return "message";
        case TypeKind::Building: return "building";
        case TypeKind::Posc: return "posc";
        case TypeKind::Display: return "display";
        case TypeKind::Memory: return "memory";
        case TypeKind::Item: return "item";
        case TypeKind::Liquid: return "liquid";
        case TypeKind::Block: return "block";
        case TypeKind::Unit: return "unit";
        case TypeKind::UnitKind: return "unit_kind";
        case TypeKind::Team: return "team";
        case TypeKind::Sensor: return "sensor";
        case TypeKind::SensorValue: return "sensor_value";
        case TypeKind::RadarFilter: return "radar_filter";
        case TypeKind::RadarSort: return "radar_sort";
        case TypeKind::PackedColor: return "packed_color";
        case TypeKind::Arr: return "arr<" + typeName(*type.elementType) + ">";
        case TypeKind::Arr2d: return "arr2d<" + typeName(*type.elementType) + ">";
    }
    return "<unknown>";
}

bool isNumeric(const Type& type) {
    return type == TypeKind::Int || type == TypeKind::Float || type == TypeKind::Number;
}

std::optional<Type> builtinContentConstantType(std::string_view name);

struct OpFunction {
    std::string_view operation;
    std::size_t arity;
};

struct LookupFunction {
    std::string_view content;
    TypeKind result;
};

struct RadarConstant {
    TypeKind type;
    std::string_view operand;
};

struct RadarMethod {
    std::string_view sort;
    int order;
};

std::optional<RadarConstant> builtinRadarConstant(std::string_view name) {
    static const std::unordered_map<std::string_view, RadarConstant> constants = {
        {"radar_any", {TypeKind::RadarFilter, "any"}},
        {"radar_enemy", {TypeKind::RadarFilter, "enemy"}},
        {"radar_ally", {TypeKind::RadarFilter, "ally"}},
        {"radar_player", {TypeKind::RadarFilter, "player"}},
        {"radar_attacker", {TypeKind::RadarFilter, "attacker"}},
        {"radar_flying", {TypeKind::RadarFilter, "flying"}},
        {"radar_boss", {TypeKind::RadarFilter, "boss"}},
        {"radar_ground", {TypeKind::RadarFilter, "ground"}},
        {"radar_distance", {TypeKind::RadarSort, "distance"}},
        {"radar_health", {TypeKind::RadarSort, "health"}},
        {"radar_shield", {TypeKind::RadarSort, "shield"}},
        {"radar_armor", {TypeKind::RadarSort, "armor"}},
        {"radar_max_health", {TypeKind::RadarSort, "maxHealth"}},
    };
    const auto iterator = constants.find(name);
    return iterator == constants.end() ? std::nullopt
                                       : std::optional<RadarConstant>(iterator->second);
}

std::optional<RadarMethod> builtinRadarMethod(std::string_view name) {
    static const std::unordered_map<std::string_view, RadarMethod> methods = {
        {"radar_nearest", {"distance", 1}}, {"radar_farthest", {"distance", 0}},
        {"radar_max_health", {"health", 1}}, {"radar_min_health", {"health", 0}},
        {"radar_max_shield", {"shield", 1}}, {"radar_min_shield", {"shield", 0}},
        {"radar_max_armor", {"armor", 1}}, {"radar_min_armor", {"armor", 0}},
        {"radar_max_max_health", {"maxHealth", 1}},
        {"radar_min_max_health", {"maxHealth", 0}},
        {"radar_max_health_limit", {"maxHealth", 1}},
        {"radar_min_health_limit", {"maxHealth", 0}},
    };
    const auto iterator = methods.find(name);
    return iterator == methods.end() ? std::nullopt
                                     : std::optional<RadarMethod>(iterator->second);
}

std::optional<int> builtinBuildRotation(std::string_view name) {
    static const std::unordered_map<std::string_view, int> rotations = {
        {"build_right", 0}, {"build_up", 1}, {"build_left", 2}, {"build_down", 3},
    };
    const auto iterator = rotations.find(name);
    return iterator == rotations.end() ? std::nullopt : std::optional<int>(iterator->second);
}

bool isReservedBuiltinConstant(std::string_view name) {
    return builtinRadarConstant(name).has_value() || builtinBuildRotation(name).has_value();
}

bool isRadarSelector(const Type& type) {
    return type == TypeKind::RadarFilter || type == TypeKind::RadarSort;
}

bool isSenseableReceiver(const Type& type) {
    return type == TypeKind::Building || type == TypeKind::Posc || type == TypeKind::Message ||
           type == TypeKind::Unit ||
           type == TypeKind::Display || type == TypeKind::Memory || type == TypeKind::Item ||
           type == TypeKind::Liquid || type == TypeKind::Block || type == TypeKind::UnitKind ||
           type == TypeKind::Team || type == TypeKind::String || type == TypeKind::SensorValue;
}

std::optional<Type> sensorResultType(std::string_view sensor, const Type& receiver) {
    static const std::unordered_map<std::string_view, Type> results = {
        {"firstItem", TypeKind::Item}, {"currentAmmoType", TypeKind::SensorValue},
        {"payloadType", TypeKind::SensorValue}, {"config", TypeKind::SensorValue},
        {"controller", TypeKind::Posc}, {"selectedBlock", TypeKind::Block},
        {"pingText", TypeKind::String}, {"name", TypeKind::String},
        {"color", TypeKind::PackedColor}, {"enabled", TypeKind::Bool},
        {"solid", TypeKind::Bool}, {"dead", TypeKind::Bool}, {"shooting", TypeKind::Bool},
        {"boosting", TypeKind::Bool}, {"mining", TypeKind::Bool}, {"flying", TypeKind::Bool},
    };
    if (sensor == "type") {
        if (receiver == TypeKind::Unit) return TypeKind::UnitKind;
        if (receiver == TypeKind::Building || receiver == TypeKind::Message ||
            receiver == TypeKind::Display || receiver == TypeKind::Memory) return TypeKind::Block;
        return TypeKind::SensorValue;
    }
    if (sensor == "building") {
        return receiver == TypeKind::Posc || receiver == TypeKind::Unit
            ? Type(TypeKind::Building) : Type(TypeKind::Block);
    }
    if (sensor == "breaking") {
        return receiver == TypeKind::Posc || receiver == TypeKind::Unit
            ? Type(TypeKind::Building) : Type(TypeKind::Bool);
    }
    const auto iterator = results.find(sensor);
    if (iterator != results.end()) return iterator->second;
    return TypeKind::Number;
}

std::optional<std::string_view> sensorAlias(std::string_view name) {
    static const std::unordered_map<std::string_view, std::string_view> aliases = {
        {"get_total_items", "totalItems"}, {"get_first_item", "firstItem"},
        {"get_total_liquids", "totalLiquids"}, {"get_total_power", "totalPower"},
        {"get_item_capacity", "itemCapacity"}, {"get_liquid_capacity", "liquidCapacity"},
        {"get_power_capacity", "powerCapacity"}, {"get_power_net_stored", "powerNetStored"},
        {"get_power_net_capacity", "powerNetCapacity"}, {"get_power_net_in", "powerNetIn"},
        {"get_power_net_out", "powerNetOut"}, {"get_ammo", "ammo"},
        {"get_ammo_capacity", "ammoCapacity"}, {"get_current_ammo_type", "currentAmmoType"},
        {"get_memory_capacity", "memoryCapacity"}, {"get_health", "health"},
        {"get_max_health", "maxHealth"}, {"get_heat", "heat"}, {"get_shield", "shield"},
        {"get_armor", "armor"}, {"get_efficiency", "efficiency"}, {"get_progress", "progress"},
        {"get_timescale", "timescale"}, {"get_rotation", "rotation"}, {"get_x", "x"},
        {"get_y", "y"}, {"get_velocity_x", "velocityX"}, {"get_velocity_y", "velocityY"},
        {"get_shoot_x", "shootX"}, {"get_shoot_y", "shootY"}, {"get_camera_x", "cameraX"},
        {"get_camera_y", "cameraY"}, {"get_camera_width", "cameraWidth"},
        {"get_camera_height", "cameraHeight"}, {"get_display_width", "displayWidth"},
        {"get_display_height", "displayHeight"}, {"get_buffer_size", "bufferSize"},
        {"get_operations", "operations"}, {"get_size", "size"}, {"get_solid", "solid"},
        {"get_dead", "dead"}, {"get_range", "range"}, {"get_shooting", "shooting"},
        {"get_boosting", "boosting"}, {"get_mine_x", "mineX"}, {"get_mine_y", "mineY"},
        {"get_mining", "mining"}, {"get_build_x", "buildX"}, {"get_build_y", "buildY"},
        {"get_ping_x", "pingX"}, {"get_ping_y", "pingY"}, {"get_ping_text", "pingText"},
        {"get_building", "building"}, {"get_breaking", "breaking"}, {"get_speed", "speed"},
        {"get_team", "team"}, {"get_type", "type"}, {"get_flag", "flag"},
        {"get_flying", "flying"}, {"get_controlled", "controlled"},
        {"get_controller", "controller"}, {"get_name", "name"},
        {"get_payload_count", "payloadCount"}, {"get_payload_type", "payloadType"},
        {"get_total_payload", "totalPayload"}, {"get_payload_capacity", "payloadCapacity"},
        {"get_max_units", "maxUnits"}, {"get_id", "id"},
        {"get_selected_block", "selectedBlock"}, {"get_selected_rotation", "selectedRotation"},
        {"get_bullet_lifetime", "bulletLifetime"}, {"get_bullet_time", "bulletTime"},
        {"get_enabled", "enabled"}, {"get_config", "config"}, {"get_color", "color"},
    };
    const auto iterator = aliases.find(name);
    return iterator == aliases.end() ? std::nullopt : std::optional<std::string_view>(iterator->second);
}

bool isSensorName(std::string_view name) {
    static const std::unordered_set<std::string_view> names = {
        "totalItems", "firstItem", "totalLiquids", "totalPower", "itemCapacity", "liquidCapacity",
        "powerCapacity", "powerNetStored", "powerNetCapacity", "powerNetIn", "powerNetOut", "ammo",
        "ammoCapacity", "currentAmmoType", "memoryCapacity", "health", "maxHealth", "heat", "shield",
        "armor", "efficiency", "progress", "timescale", "rotation", "x", "y", "velocityX", "velocityY",
        "shootX", "shootY", "cameraX", "cameraY", "cameraWidth", "cameraHeight", "displayWidth",
        "displayHeight", "bufferSize", "operations", "size", "solid", "dead", "range", "shooting",
        "boosting", "mineX", "mineY", "mining", "buildX", "buildY", "pingX", "pingY", "pingText",
        "building", "breaking", "speed", "team", "type", "flag", "flying", "controlled", "controller",
        "name", "payloadCount", "payloadType", "totalPayload", "payloadCapacity", "maxUnits", "id",
        "selectedBlock", "selectedRotation", "bulletLifetime", "bulletTime", "enabled", "shoot", "shootp",
        "config", "color",
    };
    return names.contains(name);
}

bool isSenseableSensorName(std::string_view name) {
    return isSensorName(name) && name != "shoot" && name != "shootp";
}

std::optional<LookupFunction> builtinLookupFunction(std::string_view name) {
    static const std::unordered_map<std::string_view, LookupFunction> functions = {
        {"lookup_block", {"block", TypeKind::Block}},
        {"lookup_unit", {"unit", TypeKind::UnitKind}},
        {"lookup_item", {"item", TypeKind::Item}},
        {"lookup_liquid", {"liquid", TypeKind::Liquid}},
        {"lookup_team", {"team", TypeKind::Team}},
    };
    const auto iterator = functions.find(name);
    return iterator == functions.end() ? std::nullopt : std::optional<LookupFunction>(iterator->second);
}

std::optional<Type> configValueMemberType(std::string_view name) {
    static const std::unordered_map<std::string_view, TypeKind> methods = {
        {"set_production", TypeKind::UnitKind},
        {"set_output_item", TypeKind::Item},
        {"set_output_liquid", TypeKind::Liquid},
        {"set_sort_item", TypeKind::Item},
        {"set_unload_item", TypeKind::Item},
        {"set_delivery_item", TypeKind::Item},
        {"set_recipe", TypeKind::Block},
    };
    const auto iterator = methods.find(name);
    return iterator == methods.end() ? std::nullopt : std::optional<Type>(iterator->second);
}

bool isPayloadKindMember(std::string_view name) {
    return name == "set_payload_kind" || name == "set_straight_payload";
}

bool isConfigClearMember(std::string_view name) {
    static const std::unordered_set<std::string_view> methods = {
        "clear_unit_command", "clear_output_item", "clear_output_liquid",
        "clear_sort_item", "clear_unload_item", "clear_delivery_item",
        "clear_payload_kind", "clear_straight_payload", "clear_recipe",
    };
    return methods.contains(name);
}

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

std::optional<Type> builtinContentConstantType(std::string_view name) {
    if (name.empty() || name.front() != '@') return std::nullopt;
    name.remove_prefix(1);
    if (isSensorName(name)) return TypeKind::Sensor;

    static const std::unordered_map<std::string, TypeKind> constants = [] {
        std::unordered_map<std::string, TypeKind> result;
        const auto add = [&](TypeKind type, std::string_view names) {
            while (!names.empty()) {
                const std::size_t begin = names.find_first_not_of(' ');
                if (begin == std::string_view::npos) break;
                names.remove_prefix(begin);
                const std::size_t end = names.find(' ');
                const std::string_view entry = names.substr(0, end);
                result.emplace(std::string(entry), type);
                if (end == std::string_view::npos) break;
                names.remove_prefix(end + 1);
            }
        };

        add(TypeKind::Item, R"(copper lead metaglass graphite sand coal titanium thorium scrap silicon plastanium phase-fabric surge-alloy spore-pod blast-compound pyratite beryllium tungsten oxide carbide fissile-matter dormant-cyst)");
        add(TypeKind::Liquid, R"(water slag oil cryofluid neoplasm hydrogen ozone cyanogen gallium nitrogen arkycite)");
        add(TypeKind::Block, R"(additive-reconstructor advanced-launch-pad afflict air air-factory arc arkycite-floor arkyic-boulder arkyic-stone arkyic-vent arkyic-wall armored-conveyor armored-duct atmospheric-concentrator basalt basalt-boulder basalt-vent basic-assembler-module battery battery-large beam-link beam-node beam-tower beryllic-boulder beryllic-stone beryllic-stone-wall beryllium-wall beryllium-wall-large blast-door blast-drill blast-mixer bluemat boulder breach bridge-conduit bridge-conveyor build-tower canvas carbide-crucible carbide-wall carbide-wall-large carbon-boulder carbon-stone carbon-vent carbon-wall char character-overlay character-overlay-white chemical-combustion-chamber cliff cliff-crusher coal-centrifuge colored-floor colored-wall combustion-generator conduit constructor container conveyor copper-wall copper-wall-large core-acropolis core-bastion core-citadel core-foundation core-nucleus core-shard core-zone crater-stone cryofluid-mixer crystal-blocks crystal-cluster crystal-floor crystal-orbs crystalline-boulder crystalline-stone crystalline-stone-wall crystalline-vent cultivator cyanogen-synthesizer cyclone dacite dacite-boulder dacite-wall dark-metal dark-panel-1 dark-panel-2 dark-panel-3 dark-panel-4 dark-panel-5 dark-panel-6 darksand darksand-tainted-water darksand-water deconstructor deep-tainted-water deep-water dense-red-stone differential-generator diffuse diode dirt dirt-wall disassembler disperse distributor door door-large duct duct-bridge duct-router duct-unloader dune-wall duo electric-heater electrolyzer empty eruption-drill exponential-reconstructor ferric-boulder ferric-craters ferric-stone ferric-stone-wall flux-reactor force-projector foreshadow fuse graphite-press graphitic-wall grass ground-factory hail heat-reactor heat-redirector heat-router heat-source hotrock hyper-processor ice ice-snow ice-wall illuminator impact-drill impact-reactor impulse-pump incinerator interplanetary-accelerator inverted-sorter item-source item-void junction kiln lancer landing-pad large-canvas large-cliff-crusher large-constructor large-logic-display large-payload-mass-driver large-plasma-bore large-shield-projector laser-drill launch-pad liquid-container liquid-junction liquid-router liquid-source liquid-tank liquid-void logic-display logic-processor lustre magmarock malign mass-driver mech-assembler mech-fabricator mech-refabricator mechanical-drill mechanical-pump meltdown melter memory-bank memory-cell mend-projector mender message metal-floor metal-floor-2 metal-floor-3 metal-floor-4 metal-floor-5 metal-floor-damaged metal-tiles-1 metal-tiles-10 metal-tiles-11 metal-tiles-12 metal-tiles-13 metal-tiles-2 metal-tiles-3 metal-tiles-4 metal-tiles-5 metal-tiles-6 metal-tiles-7 metal-tiles-8 metal-tiles-9 metal-wall-1 metal-wall-2 metal-wall-3 micro-processor molten-slag moss mud multi-press multiplicative-reconstructor naval-factory neoplasia-reactor oil-extractor ore-crystal-thorium ore-wall-beryllium ore-wall-graphite ore-wall-thorium ore-wall-tungsten overdrive-dome overdrive-projector overflow-duct overflow-gate oxidation-chamber parallax payload-conveyor payload-loader payload-mass-driver payload-router payload-source payload-unloader payload-void pebbles phase-conduit phase-conveyor phase-heater phase-synthesizer phase-wall phase-wall-large phase-weaver pine plasma-bore plastanium-compressor plastanium-conveyor plastanium-wall plastanium-wall-large plated-conduit pneumatic-drill pooled-cryofluid power-node power-node-large power-source power-void prime-refabricator pulse-conduit pulverizer pur-bush pyratite-mixer pyrolysis-generator radar red-diamond-wall red-ice red-ice-boulder red-ice-wall red-stone red-stone-boulder red-stone-vent red-stone-wall redmat redweed regen-projector regolith regolith-wall reinforced-bridge-conduit reinforced-conduit reinforced-container reinforced-liquid-container reinforced-liquid-junction reinforced-liquid-router reinforced-liquid-tank reinforced-message reinforced-payload-conveyor reinforced-payload-router reinforced-pump reinforced-surge-wall reinforced-surge-wall-large reinforced-vault remove-ore remove-wall repair-point repair-turret rhyolite rhyolite-boulder rhyolite-crater rhyolite-vent rhyolite-wall ripple rotary-pump rough-rhyolite router rtg-generator rune-overlay rune-overlay-crux salt salt-wall salvo sand-boulder sand-floor sand-wall sand-water scathe scatter scorch scrap-wall scrap-wall-gigantic scrap-wall-huge scrap-wall-large segment separator shale shale-boulder shale-wall shallow-water shield-projector shielded-wall ship-assembler ship-fabricator ship-refabricator shock-mine shockwave-tower shrubs silicon-arc-furnace silicon-crucible silicon-smelter slag-centrifuge slag-heater slag-incinerator small-deconstructor small-heat-redirector smite snow snow-boulder snow-pine snow-wall solar-panel solar-panel-large sorter space spawn spectre spore-cluster spore-moss spore-pine spore-press spore-wall steam-generator stone stone-vent stone-wall sublimate surge-conveyor surge-crucible surge-router surge-smelter surge-tower surge-wall surge-wall-large swarmer switch tainted-water tank-assembler tank-fabricator tank-refabricator tar tendrils tetrative-reconstructor thermal-generator thorium-reactor thorium-wall thorium-wall-large thruster tile-logic-display titan titanium-conveyor titanium-wall titanium-wall-large tsunami tungsten-wall tungsten-wall-large turbine-condenser underflow-duct underflow-gate unit-cargo-loader unit-cargo-unload-point unit-repair-tower unloader vault vent-condenser vibrant-crystal-cluster water-extractor wave white-tree white-tree-dead world-cell world-message world-processor world-switch yellow-stone yellow-stone-boulder yellow-stone-plates yellow-stone-vent yellow-stone-wall yellowcoral)");
        add(TypeKind::UnitKind, R"(dagger mace fortress scepter reign nova pulsar quasar vela corvus crawler atrax spiroct arkyid toxopid flare horizon zenith antumbra eclipse mono poly mega quad oct risso minke bryde sei omura retusa oxynoe cyerce aegires navanax alpha beta gamma stell locus precept vanquish conquer merui cleroi anthicus tecta collaris elude avert obviate quell disrupt evoke incite emanate)");
        add(TypeKind::Team, R"(derelict sharded crux malis green blue)");
        return result;
    }();

    const auto iterator = constants.find(std::string(name));
    if (iterator == constants.end()) return std::nullopt;
    return Type(iterator->second);
}

bool isBuiltinFunction(std::string_view name) {
    return builtinOpFunction(name).has_value() ||
           name == "print" || name == "printchar" || name == "putchar" ||
           name == "format" || name == "printf" || name == "printflush" || name == "drawflush" ||
           name == "wait" || name == "stop" || name == "exit" ||
           name == "rgb" || name == "rgba" || name == "pack_color" || name == "unpack_color" ||
           name == "draw_clear" || name == "draw_color" || name == "draw_col" || name == "set_color" ||
           name == "set_packed_color" ||
           name == "draw_stroke" || name == "set_stroke" ||
           name == "draw_line" || name == "draw_rect" || name == "draw_line_rect" ||
           name == "draw_poly" || name == "draw_line_poly" || name == "draw_triangle" ||
           name == "draw_image" || name == "draw_print" || name == "draw_translate" ||
           name == "draw_scale" || name == "draw_rotate" || name == "draw_reset" ||
           name == "dot" || name == "cross" || name == "getlink" || name == "unit_bind" ||
           builtinLookupFunction(name).has_value();
}

struct Expr {
    enum class Kind { Number, String, Boolean, Null, Variable, Unary, Binary, Conditional,
                      Assign, Call, Prefix, Postfix,
                      Member, Index, InitializerList, TypedInitializer, Sizeof };

    Kind kind = Kind::Number;
    SourceLocation location;
    std::string text;
    bool boolean = false;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> third;
    std::unique_ptr<Expr> receiver;
    std::vector<std::unique_ptr<Expr>> arguments;
    Type declaredType;
};

struct Stmt {
    enum class Kind { Empty, Block, Variable, Expression, If, Switch, While, For,
                      Break, Continue, Return };

    struct SwitchClause {
        std::optional<long long> value;
        SourceLocation location;
        std::vector<std::unique_ptr<Stmt>> statements;
    };

    Kind kind = Kind::Block;
    SourceLocation location;
    Type type;
    std::string name;
    std::vector<std::unique_ptr<Stmt>> statements;
    std::vector<SwitchClause> switchClauses;
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
    bool reference = false;
    bool restricted = false;
};

struct FunctionDecl {
    Type returnType;
    std::string name;
    std::string memberOf;
    std::string memberName;
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
        for (FunctionDecl& function : memberFunctions_) {
            program.functions.push_back(std::move(function));
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
               kind == TokenKind::KwPosc ||
               kind == TokenKind::KwItem || kind == TokenKind::KwLiquid ||
               kind == TokenKind::KwBlock || kind == TokenKind::KwUnit ||
               kind == TokenKind::KwUnitKind ||
               kind == TokenKind::KwTeam || kind == TokenKind::KwSensor ||
               kind == TokenKind::KwSensorValue || kind == TokenKind::KwRadarFilter ||
               kind == TokenKind::KwRadarSort ||
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
            case TokenKind::KwPosc: return TypeKind::Posc;
            case TokenKind::KwItem: return TypeKind::Item;
            case TokenKind::KwLiquid: return TypeKind::Liquid;
            case TokenKind::KwBlock: return TypeKind::Block;
            case TokenKind::KwUnit: return TypeKind::Unit;
            case TokenKind::KwUnitKind: return TypeKind::UnitKind;
            case TokenKind::KwTeam: return TypeKind::Team;
            case TokenKind::KwSensor: return TypeKind::Sensor;
            case TokenKind::KwSensorValue: return TypeKind::SensorValue;
            case TokenKind::KwRadarFilter: return TypeKind::RadarFilter;
            case TokenKind::KwRadarSort: return TypeKind::RadarSort;
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
        std::unordered_set<std::string> methodNames;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
            const Type memberType = parseType();
            const Token memberName = consume(TokenKind::Identifier, "成员类型后需要名称");
            if (match(TokenKind::LeftParen)) {
                if (fieldNames.contains(memberName.text) || !methodNames.insert(memberName.text).second) {
                    fail(memberName, "重复的成员名称: " + memberName.text);
                }
                FunctionDecl function = parseFunction(memberType, memberName, name.text);
                function.memberOf = name.text;
                function.memberName = memberName.text;
                function.name = "__member_" + name.text + "_" + memberName.text;
                function.parameters.insert(function.parameters.begin(),
                    {Type(name.text), "__this", memberName.location, true, true});
                memberFunctions_.push_back(std::move(function));
            } else {
                if (memberType == TypeKind::Void) fail(memberName, "字段不能是 void 类型");
                if (methodNames.contains(memberName.text) || !fieldNames.insert(memberName.text).second) {
                    fail(memberName, "重复的成员名称: " + memberName.text);
                }
                consume(TokenKind::Semicolon, "字段声明后需要分号");
                declaration.fields.push_back({memberType, memberName.text, memberName.location});
            }
        }
        consume(TokenKind::RightBrace, "结构体定义缺少右大括号");
        consume(TokenKind::Semicolon, "结构体定义后需要分号");
        return declaration;
    }

    FunctionDecl parseFunction(Type returnType, const Token& name,
                               const std::string& memberOf = "") {
        FunctionDecl function;
        function.returnType = returnType;
        function.name = name.text;
        function.location = name.location;
        if (!check(TokenKind::RightParen)) {
            do {
                const bool restricted = match(TokenKind::KwRestrict);
                const Type parameterType = parseType();
                if (parameterType == TypeKind::Void) fail(previous(), "参数不能是 void 类型");
                const bool reference = match(TokenKind::Ampersand);
                if (restricted && !reference) fail(previous(), "restrict 只能修饰引用参数");
                if (reference && parameterType.isArray()) {
                    fail(previous(), "引用参数暂不支持 arr 或 arr2d 类型");
                }
                const Token parameterName = consume(TokenKind::Identifier, "参数类型后需要名称");
                function.parameters.push_back({parameterType, parameterName.text, parameterName.location,
                                               reference, restricted});
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightParen, "参数列表缺少右括号");
        const std::string previousMember = currentMemberStruct_;
        currentMemberStruct_ = memberOf;
        function.body = parseBlock();
        currentMemberStruct_ = previousMember;
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
        if (match(TokenKind::KwSwitch)) return parseSwitch(previous());
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

    long long parseCaseValue() {
        std::string text;
        SourceLocation location = current().location;
        if (match(TokenKind::Minus)) {
            text.push_back('-');
        } else {
            (void)match(TokenKind::Plus);
        }
        const Token literal = consume(TokenKind::NumberLiteral,
                                      "case 后需要整数常量");
        text += literal.text;
        if (literal.text.find_first_of(".eE") != std::string::npos) {
            fail(literal, "case 值必须是整数常量");
        }
        long long value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            throw CompileError(location.line, location.column, "case 整数常量超出范围");
        }
        return value;
    }

    std::unique_ptr<Stmt> parseSwitch(const Token& keyword) {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::Switch;
        statement->location = keyword.location;
        consume(TokenKind::LeftParen, "switch 后需要左括号");
        statement->condition = parseExpression();
        consume(TokenKind::RightParen, "switch 条件后需要右括号");
        consume(TokenKind::LeftBrace, "switch 需要左大括号");

        std::unordered_set<long long> values;
        bool hasDefault = false;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
            Stmt::SwitchClause clause;
            if (match(TokenKind::KwCase)) {
                clause.location = previous().location;
                clause.value = parseCaseValue();
                if (!values.insert(*clause.value).second) {
                    throw CompileError(clause.location.line, clause.location.column,
                                       "重复的 case 值: " + std::to_string(*clause.value));
                }
            } else if (match(TokenKind::KwDefault)) {
                clause.location = previous().location;
                if (hasDefault) fail(previous(), "switch 只能有一个 default");
                hasDefault = true;
            } else {
                fail(current(), "switch 中的语句必须位于 case 或 default 之后");
            }
            consume(TokenKind::Colon, "case 或 default 后需要冒号");
            while (!check(TokenKind::KwCase) && !check(TokenKind::KwDefault) &&
                   !check(TokenKind::RightBrace) && !check(TokenKind::End)) {
                clause.statements.push_back(parseStatement());
            }
            statement->switchClauses.push_back(std::move(clause));
        }
        consume(TokenKind::RightBrace, "switch 缺少右大括号");
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
        auto expression = parseConditional();
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

    std::unique_ptr<Expr> parseConditional() {
        auto expression = parseLogicalOr();
        if (!match(TokenKind::Question)) return expression;
        const Token question = previous();
        auto conditional = std::make_unique<Expr>();
        conditional->kind = Expr::Kind::Conditional;
        conditional->location = question.location;
        conditional->left = std::move(expression);
        conditional->right = parseExpression();
        consume(TokenKind::Colon, "三目运算符缺少冒号");
        conditional->third = parseConditional();
        return conditional;
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
        if (match(TokenKind::KwNull)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Null;
            expression->location = token.location;
            return expression;
        }
        if (match(TokenKind::KwThis)) {
            if (currentMemberStruct_.empty()) fail(token, "this-> 只能用于成员函数");
            if (!match(TokenKind::Minus) || !match(TokenKind::Greater)) {
                fail(token, "this 不能作为独立表达式，只支持 this->成员");
            }
            const Token memberName = consume(TokenKind::Identifier, "this-> 后需要成员名称");
            auto receiver = std::make_unique<Expr>();
            receiver->kind = Expr::Kind::Variable;
            receiver->location = token.location;
            receiver->text = "__this";
            auto member = std::make_unique<Expr>();
            member->kind = Expr::Kind::Member;
            member->location = memberName.location;
            member->text = memberName.text;
            member->left = std::move(receiver);
            return member;
        }
        if (match(TokenKind::Identifier)) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Variable;
            expression->location = token.location;
            expression->text = token.text;
            return expression;
        }
        if (match(TokenKind::BuiltinConstant)) {
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
    std::vector<FunctionDecl> memberFunctions_;
    std::string currentMemberStruct_;
};

struct IrInstruction {
    enum class Kind { Operation, Label };
    enum class OperandRole { Definition, Value, Label, Metadata };

    Kind kind = Kind::Operation;
    std::string opcode;
    std::string label;
    std::vector<std::string> operands;
    std::vector<OperandRole> operandRoles;
    std::vector<std::string> indirectTargets;

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
               opcode != "getlink" && opcode != "lookup" &&
               opcode != "packcolor" && opcode != "unpackcolor";
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
        instructions_.push_back({IrInstruction::Kind::Label, {}, name, {}, {}, {}});
    }

    void emit(std::string opcode, std::vector<std::string> operands = {}) {
        std::vector<IrInstruction::OperandRole> roles = operandRoles(opcode, operands.size());
        if (opcode == "ucontrol" && !operands.empty()) {
            roles[0] = IrInstruction::OperandRole::Metadata;
            if (operands[0] == "getBlock" && roles.size() >= 6) {
                roles[3] = IrInstruction::OperandRole::Definition;
                roles[4] = IrInstruction::OperandRole::Definition;
                roles[5] = IrInstruction::OperandRole::Definition;
            } else if (operands[0] == "within" && roles.size() >= 5) {
                roles[4] = IrInstruction::OperandRole::Definition;
            }
        }
        instructions_.push_back({IrInstruction::Kind::Operation, std::move(opcode), {},
                                 std::move(operands), std::move(roles), {}});
    }

    void emitCounterJump(std::string target, std::vector<std::string> possibleTargets) {
        emit("set", {"@counter", std::move(target)});
        instructions_.back().indirectTargets = std::move(possibleTargets);
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
            output << (instruction.opcode == "ubindunit" ? "ubind" : instruction.opcode);
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
                candidate.optimizeUnitBindings();
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
                if (!terminal->indirectTargets.empty()) {
                    for (const std::string& label : terminal->indirectTargets) {
                        const auto target = labelPositions.find(label);
                        if (target != labelPositions.end()) {
                            current.successors.push_back(blockAt[target->second]);
                        }
                    }
                } else {
                    const auto continuations = returnContinuations.find(terminal->operands[1]);
                    if (continuations != returnContinuations.end()) {
                        current.successors.insert(current.successors.end(), continuations->second.begin(),
                                                  continuations->second.end());
                    }
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

    void optimizeUnitBindings() {
        const std::vector<IrBasicBlock> blocks = basicBlocks();
        for (const IrBasicBlock& block : blocks) {
            std::unordered_set<std::string> aliases;
            for (std::size_t index = block.begin; index < block.end; ++index) {
                IrInstruction& instruction = instructions_[index];
                if (instruction.kind == IrInstruction::Kind::Label) continue;

                if (instruction.opcode == "ubind" || instruction.opcode == "ubindunit") {
                    const bool directUnit = instruction.opcode == "ubindunit";
                    const std::string operand = instruction.operands.front();
                    if (directUnit && aliases.contains(operand)) {
                        instruction.opcode.clear();
                        instruction.operands.clear();
                        instruction.operandRoles.clear();
                        continue;
                    }
                    aliases.clear();
                    aliases.insert("@unit");
                    if (directUnit) aliases.insert(operand);
                    continue;
                }

                if (instruction.opcode == "ucontrol" && !instruction.operands.empty() &&
                    instruction.operands[0] == "unbind") {
                    aliases.clear();
                    continue;
                }

                const std::vector<std::size_t> definitions = instruction.definitions();
                const bool copiedBinding = instruction.opcode == "set" &&
                                           instruction.operands.size() >= 2 &&
                                           aliases.contains(instruction.operands[1]);
                for (const std::size_t definition : definitions) {
                    if (definition < instruction.operands.size()) aliases.erase(instruction.operands[definition]);
                }
                if (copiedBinding) aliases.insert(instruction.operands[0]);
            }
        }
        instructions_.erase(std::remove_if(instructions_.begin(), instructions_.end(),
                                           [](const IrInstruction& instruction) {
                                               return instruction.kind == IrInstruction::Kind::Operation &&
                                                      instruction.opcode.empty();
                                           }), instructions_.end());
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
                    for (std::string& target : bodyInstruction.indirectTargets) {
                        const auto renamed = renamedLabels.find(target);
                        if (renamed != renamedLabels.end()) target = renamed->second;
                    }
                    if (bodyInstruction.opcode == "set" && bodyInstruction.operands.size() >= 2 &&
                        bodyInstruction.operands[0] == "@counter" &&
                        bodyInstruction.operands[1] == returnAddress) {
                        bodyInstruction.opcode = "jump";
                        bodyInstruction.operands = {"$" + callReturnLabel, "always", "0", "0"};
                        bodyInstruction.operandRoles = operandRoles(bodyInstruction.opcode,
                                                                    bodyInstruction.operands.size());
                        bodyInstruction.indirectTargets.clear();
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
        } else if (opcode == "lookup") {
            if (!roles.empty()) roles[0] = Role::Metadata;
            if (roles.size() > 1) roles[1] = Role::Definition;
        } else if (opcode == "op") {
            if (!roles.empty()) roles[0] = Role::Metadata;
            if (roles.size() > 1) roles[1] = Role::Definition;
        } else if (opcode == "jump") {
            if (!roles.empty()) roles[0] = Role::Label;
            if (roles.size() > 1) roles[1] = Role::Metadata;
        } else if (opcode == "draw") {
            if (!roles.empty()) roles[0] = Role::Metadata;
        } else if (opcode == "control") {
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
    struct MemoryLocation {
        std::string handle;
        std::string address;
        std::string identityHandle;
        std::string identityBase;
        std::string identityIndex;
        long long indexScale = 0;
        long long constantOffset = 0;
        bool normalized = false;
    };
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
        if (functions_.contains("main_init")) generateMainInit();
        emitter_.emit("jump", {reference(mainEntryLabel_), "always", "0", "0"});

        for (const FunctionDecl& function : program_.functions) {
            if (function.name != "main_loop" && function.name != "main_init" &&
                reachableFunctions_.contains(function.name)) {
                generateFunction(function);
            }
        }
        generateMainLoop();
        emitter_.optimizeLocalAssignments();
        emitter_.optimizeUnitBindings();
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
            case TypeKind::Posc:
            case TypeKind::Display:
            case TypeKind::Memory:
            case TypeKind::Item:
            case TypeKind::Liquid:
            case TypeKind::Block:
            case TypeKind::Unit:
            case TypeKind::UnitKind:
            case TypeKind::Team:
            case TypeKind::Sensor:
            case TypeKind::SensorValue:
            case TypeKind::RadarFilter:
            case TypeKind::RadarSort: return "null";
            case TypeKind::Null: return "null";
            case TypeKind::PackedColor: return "0";
            case TypeKind::Arr:
            case TypeKind::Arr2d: return "null";
            case TypeKind::Void: break;
        }
        return "null";
    }

    void collectStructDeclarations() {
        for (const StructDecl& declaration : program_.structs) {
            if (isReservedBuiltinConstant(declaration.name)) {
                fail(declaration.location, "内置常量名称不能被声明: " + declaration.name);
            }
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
            for (const StructField& field : declaration->second->fields) {
                if (isReservedBuiltinConstant(field.name)) {
                    fail(field.location, "内置常量名称不能被声明: " + field.name);
                }
                validate(field.type, field.location);
            }
            states[type.structName] = State::Complete;
        };
        for (const StructDecl& declaration : program_.structs) validate(Type(declaration.name), declaration.location);
        std::function<bool(const Type&, std::unordered_set<std::string>&)> storable =
            [&](const Type& type, std::unordered_set<std::string>& visiting) {
                if (type.isArray() || type == TypeKind::Memory || type == TypeKind::String ||
                    type == TypeKind::Message || type == TypeKind::Building || type == TypeKind::Posc ||
                    type == TypeKind::Display || type == TypeKind::Item || type == TypeKind::Liquid ||
                    type == TypeKind::Block || type == TypeKind::Unit ||
                    type == TypeKind::UnitKind || type == TypeKind::Team ||
                    type == TypeKind::Sensor || type == TypeKind::SensorValue ||
                    type == TypeKind::RadarFilter || type == TypeKind::RadarSort ||
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
            validateExpression(expression->third.get());
            for (const auto& argument : expression->arguments) validateExpression(argument.get());
        };
        std::function<void(const Stmt*)> validateStatement = [&](const Stmt* statement) {
            if (statement == nullptr) return;
            if (statement->kind == Stmt::Kind::Variable) {
                if (isRadarSelector(statement->type)) {
                    fail(statement->location, "Radar 选择器类型不能声明变量");
                }
                validateArray(statement->type, statement->location);
            }
            validateExpression(statement->expression.get());
            validateExpression(statement->condition.get());
            validateExpression(statement->increment.get());
            validateStatement(statement->initializerStatement.get());
            validateStatement(statement->thenBranch.get());
            validateStatement(statement->elseBranch.get());
            for (const auto& child : statement->statements) validateStatement(child.get());
            for (const auto& clause : statement->switchClauses) {
                for (const auto& child : clause.statements) validateStatement(child.get());
            }
        };
        for (const StructDecl& declaration : program_.structs) {
            for (const StructField& field : declaration.fields) {
                if (isRadarSelector(field.type)) {
                    fail(field.location, "Radar 选择器类型不能用于结构体字段");
                }
                validateArray(field.type, field.location);
            }
        }
        for (const GlobalDecl& global : program_.globals) {
            if (isReservedBuiltinConstant(global.name)) {
                fail(global.location, "内置常量名称不能被声明: " + global.name);
            }
            if (isRadarSelector(global.type)) {
                fail(global.location, "Radar 选择器类型不能声明变量");
            }
            validateArray(global.type, global.location);
            validateExpression(global.initializer.get());
        }
        for (const FunctionDecl& function : program_.functions) {
            if (isReservedBuiltinConstant(function.name)) {
                fail(function.location, "内置常量名称不能被声明: " + function.name);
            }
            if (isRadarSelector(function.returnType)) {
                fail(function.location, "Radar 选择器类型不能作为函数返回类型");
            }
            validateArray(function.returnType, function.location);
            for (const Parameter& parameter : function.parameters) {
                if (isRadarSelector(parameter.type)) {
                    fail(parameter.location, "Radar 选择器类型不能作为函数参数");
                }
                validateArray(parameter.type, parameter.location);
            }
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
            if (!function.memberOf.empty()) {
                auto& methods = memberFunctions_[function.memberOf];
                if (!methods.emplace(function.memberName, function.name).second) {
                    fail(function.location, "重复的成员函数: " + function.memberOf + "." +
                                            function.memberName);
                }
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
                if (isReservedBuiltinConstant(parameter.name)) {
                    fail(parameter.location, "内置常量名称不能被声明: " + parameter.name);
                }
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

        const auto initIterator = functions_.find("main_init");
        if (initIterator != functions_.end()) {
            const FunctionDecl& init = *initIterator->second.declaration;
            if (init.returnType != TypeKind::Void || !init.parameters.empty()) {
                fail(init.location, "初始化入口必须声明为 void main_init()");
            }
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
        collectCalls(expression->third.get(), calls);
        collectCalls(expression->receiver.get(), calls);
        for (const auto& argument : expression->arguments) collectCalls(argument.get(), calls);
    }

    static void collectMemberCalls(const Expr* expression,
                                   std::vector<std::pair<std::string, SourceLocation>>& calls) {
        if (expression == nullptr) return;
        if (expression->kind == Expr::Kind::Sizeof) return;
        if (expression->kind == Expr::Kind::Call && expression->receiver) {
            calls.emplace_back(expression->text, expression->location);
        }
        collectMemberCalls(expression->left.get(), calls);
        collectMemberCalls(expression->right.get(), calls);
        collectMemberCalls(expression->third.get(), calls);
        collectMemberCalls(expression->receiver.get(), calls);
        for (const auto& argument : expression->arguments) collectMemberCalls(argument.get(), calls);
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
        for (const auto& clause : statement->switchClauses) {
            for (const auto& child : clause.statements) collectCalls(child.get(), calls);
        }
    }

    static void collectMemberCalls(const Stmt* statement,
                                   std::vector<std::pair<std::string, SourceLocation>>& calls) {
        if (statement == nullptr) return;
        collectMemberCalls(statement->expression.get(), calls);
        collectMemberCalls(statement->condition.get(), calls);
        collectMemberCalls(statement->increment.get(), calls);
        collectMemberCalls(statement->initializerStatement.get(), calls);
        collectMemberCalls(statement->thenBranch.get(), calls);
        collectMemberCalls(statement->elseBranch.get(), calls);
        for (const auto& child : statement->statements) collectMemberCalls(child.get(), calls);
        for (const auto& clause : statement->switchClauses) {
            for (const auto& child : clause.statements) collectMemberCalls(child.get(), calls);
        }
    }

    void appendMemberCallEdges(const std::vector<std::pair<std::string, SourceLocation>>& calls,
                               std::vector<std::string>& edges) const {
        for (const auto& [methodName, location] : calls) {
            bool found = false;
            for (const auto& [owner, methods] : memberFunctions_) {
                (void)owner;
                const auto method = methods.find(methodName);
                if (method == methods.end()) continue;
                edges.push_back(method->second);
                found = true;
            }
            (void)location;
            (void)found;
        }
    }

    void validateCallGraph() {
        std::unordered_map<std::string, std::vector<std::string>> graph;
        std::vector<std::string> initializationRoots;
        for (const FunctionDecl& function : program_.functions) {
            std::vector<std::pair<std::string, SourceLocation>> calls;
            collectCalls(function.body.get(), calls);
            for (const auto& [callee, location] : calls) {
                if (isBuiltinFunction(callee)) continue;
                std::string resolved = callee;
                if (!function.memberOf.empty()) {
                    const auto owner = memberFunctions_.find(function.memberOf);
                    if (owner != memberFunctions_.end()) {
                        const auto method = owner->second.find(callee);
                        if (method != owner->second.end()) resolved = method->second;
                    }
                }
                if (functions_.find(resolved) == functions_.end()) fail(location, "未定义的函数: " + callee);
                if (resolved == "main_loop" || resolved == "main_init") {
                    fail(location, "不能显式调用 " + resolved);
                }
                graph[function.name].push_back(resolved);
            }
            std::vector<std::pair<std::string, SourceLocation>> memberCalls;
            collectMemberCalls(function.body.get(), memberCalls);
            appendMemberCallEdges(memberCalls, graph[function.name]);
        }
        for (const GlobalDecl& global : program_.globals) {
            std::vector<std::pair<std::string, SourceLocation>> calls;
            collectCalls(global.initializer.get(), calls);
            for (const auto& [callee, location] : calls) {
                if (isBuiltinFunction(callee)) continue;
                if (functions_.find(callee) == functions_.end()) fail(location, "未定义的函数: " + callee);
                if (callee == "main_loop" || callee == "main_init") {
                    fail(location, "不能显式调用 " + callee);
                }
                initializationRoots.push_back(callee);
            }
            std::vector<std::pair<std::string, SourceLocation>> memberCalls;
            collectMemberCalls(global.initializer.get(), memberCalls);
            std::vector<std::string> edges;
            appendMemberCallEdges(memberCalls, edges);
            initializationRoots.insert(initializationRoots.end(), edges.begin(), edges.end());
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
            if (name != "main_loop" && name != "main_init") inlineOrder_.push_back(name);
        };

        visit("main_loop");
        if (functions_.contains("main_init")) visit("main_init");
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

    void generateMainInit() {
        const FunctionDecl& init = *functions_.at("main_init").declaration;
        currentFunction_ = &init;
        currentContext_ = "main_init";
        pushScope();
        generateStatement(*init.body);
        emitter_.label(mainInitEndLabel_);
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
        if (statement.kind == Stmt::Kind::Switch) return switchDefinitelyReturns(statement);
        return false;
    }

    static bool containsSwitchEscape(const Stmt& statement) {
        if (statement.kind == Stmt::Kind::Break || statement.kind == Stmt::Kind::Continue) {
            return true;
        }
        if (statement.kind == Stmt::Kind::While || statement.kind == Stmt::Kind::For ||
            statement.kind == Stmt::Kind::Switch) {
            return false;
        }
        if (statement.kind == Stmt::Kind::Block) {
            return std::any_of(statement.statements.begin(), statement.statements.end(),
                               [](const auto& child) { return containsSwitchEscape(*child); });
        }
        if (statement.kind == Stmt::Kind::If) {
            return containsSwitchEscape(*statement.thenBranch) ||
                   (statement.elseBranch && containsSwitchEscape(*statement.elseBranch));
        }
        return false;
    }

    static bool switchDefinitelyReturns(const Stmt& statement) {
        const bool hasDefault = std::any_of(
            statement.switchClauses.begin(), statement.switchClauses.end(),
            [](const Stmt::SwitchClause& clause) { return !clause.value.has_value(); });
        if (!hasDefault) return false;

        bool followingReturns = false;
        for (std::size_t index = statement.switchClauses.size(); index-- > 0;) {
            bool currentReturns = followingReturns;
            for (const auto& child : statement.switchClauses[index].statements) {
                if (containsSwitchEscape(*child)) {
                    currentReturns = false;
                    break;
                }
                if (definitelyReturns(*child)) {
                    currentReturns = true;
                    break;
                }
            }
            if (!currentReturns) return false;
            followingReturns = true;
        }
        return !statement.switchClauses.empty();
    }

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    void declareLocal(const std::string& name, Symbol symbol, SourceLocation location) {
        if (isReservedBuiltinConstant(name)) {
            fail(location, "内置常量名称不能被声明: " + name);
        }
        if (implicitLinkType(name)) {
            fail(location, "Mindustry 链接标识符不能被声明: " + name);
        }
        if (!scopes_.back().emplace(name, std::move(symbol)).second) {
            fail(location, "同一作用域内重复定义变量: " + name);
        }
    }

    Symbol resolve(const std::string& name, SourceLocation location) const {
        if (!name.empty() && name.front() == '@') {
            if (const std::optional<Type> type = builtinContentConstantType(name)) {
                return {*type, name, false, {}};
            }
            fail(location, "未知或暂不支持的 Mindustry @ 常量: " + name);
        }
        if (const std::optional<RadarConstant> radar = builtinRadarConstant(name)) {
            return {radar->type, std::string(radar->operand), false, {}};
        }
        if (const std::optional<int> rotation = builtinBuildRotation(name)) {
            return {TypeKind::Int, std::to_string(*rotation), false, {}};
        }
        for (auto iterator = scopes_.rbegin(); iterator != scopes_.rend(); ++iterator) {
            const auto symbol = iterator->find(name);
            if (symbol != iterator->end()) return symbol->second;
        }
        if (currentFunction_ != nullptr && !currentFunction_->memberOf.empty()) {
            const StructDecl& declaration = *structs_.at(currentFunction_->memberOf);
            const Symbol* thisSymbol = nullptr;
            for (auto iterator = scopes_.rbegin(); iterator != scopes_.rend(); ++iterator) {
                const auto found = iterator->find("__this");
                if (found != iterator->end()) {
                    thisSymbol = &found->second;
                    break;
                }
            }
            if (thisSymbol != nullptr) {
                std::size_t offset = 0;
                for (const StructField& field : declaration.fields) {
                    const std::size_t fieldSize = typeSize(field.type);
                    if (field.name == name) {
                        std::vector<std::string> components(
                            thisSymbol->components.begin() + static_cast<std::ptrdiff_t>(offset),
                            thisSymbol->components.begin() + static_cast<std::ptrdiff_t>(offset + fieldSize));
                        if (field.type.isRuntimeAggregate()) {
                            return {field.type, "", true, std::move(components)};
                        }
                        return {field.type, components.front(), true, {}};
                    }
                    offset += fieldSize;
                }
            }
        }
        if (const std::optional<Type> type = implicitLinkType(name)) {
            return {*type, name, false, {}};
        }
        fail(location, "未定义的变量: " + name);
    }

    std::optional<std::string> memberFunctionName(const Type& receiver,
                                                   std::string_view name) const {
        if (!receiver.isStruct()) return std::nullopt;
        const auto owner = memberFunctions_.find(receiver.structName);
        if (owner == memberFunctions_.end()) return std::nullopt;
        const auto method = owner->second.find(std::string(name));
        return method == owner->second.end() ? std::nullopt
                                             : std::optional<std::string>(method->second);
    }

    std::optional<std::string> currentMemberFunctionName(std::string_view name) const {
        if (currentFunction_ == nullptr || currentFunction_->memberOf.empty()) return std::nullopt;
        return memberFunctionName(Type(currentFunction_->memberOf), name);
    }

    static bool canAssign(const Type& destination, const Type& source) {
        if (destination == source) return true;
        if (source == TypeKind::Null) {
            return destination == TypeKind::String || destination == TypeKind::Message ||
                   destination == TypeKind::Building || destination == TypeKind::Posc ||
                   destination == TypeKind::Unit ||
                   destination == TypeKind::Display || destination == TypeKind::Memory ||
                   destination == TypeKind::Item || destination == TypeKind::Liquid ||
                   destination == TypeKind::Block || destination == TypeKind::UnitKind ||
                   destination == TypeKind::Team || destination == TypeKind::SensorValue;
        }
        if (destination == TypeKind::Posc &&
            (source == TypeKind::Building || source == TypeKind::Unit ||
             source == TypeKind::Message ||
             source == TypeKind::Display || source == TypeKind::Memory)) return true;
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

    static std::optional<long long> integerOperand(const std::string& operand) {
        long long value = 0;
        const auto [end, error] = std::from_chars(operand.data(), operand.data() + operand.size(), value);
        if (error != std::errc{} || end != operand.data() + operand.size()) return std::nullopt;
        return value;
    }

    ExpressionResult::MemoryLocation normalizedMemoryLocation(const std::string& handle,
                                                               const std::string& base,
                                                               const std::string& index,
                                                               std::size_t elementSize,
                                                               const std::string& address) const {
        ExpressionResult::MemoryLocation location;
        location.handle = handle;
        location.address = address;
        location.identityHandle = handle;
        location.identityBase = base;
        location.normalized = true;
        if (const std::optional<long long> constant = integerOperand(index)) {
            if (*constant > std::numeric_limits<long long>::max() / static_cast<long long>(elementSize) ||
                *constant < std::numeric_limits<long long>::min() / static_cast<long long>(elementSize)) {
                location.normalized = false;
            } else {
                location.constantOffset = *constant * static_cast<long long>(elementSize);
            }
        } else {
            location.identityIndex = index;
            location.indexScale = static_cast<long long>(elementSize);
        }
        return location;
    }

    ExpressionResult generateIndex(const Expr& expression) {
        const ExpressionResult object = generateExpression(*expression.left);
        const ExpressionResult index = generateExpression(*expression.right);
        if (index.type != TypeKind::Int) fail(expression.location, "数组索引必须是 int");
        if (object.type == TypeKind::Memory) {
            const std::string address = index.operand;
            const std::string result = temporary();
            emitter_.emit("read", {result, object.operand, address});
            return {TypeKind::Number, result, true, {},
                    normalizedMemoryLocation(object.operand, "0", index.operand, 1, address)};
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
            return {elementType, "", true, std::move(values),
                    normalizedMemoryLocation(handle, object.components[1], index.operand, size, address)};
        }
        return {elementType, values.front(), true, {},
                normalizedMemoryLocation(handle, object.components[1], index.operand, size, address)};
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
            case Stmt::Kind::Switch:
                generateSwitch(statement);
                break;
            case Stmt::Kind::While:
                generateWhile(statement);
                break;
            case Stmt::Kind::For:
                generateFor(statement);
                break;
            case Stmt::Kind::Break:
                if (breakLabels_.empty()) fail(statement.location, "break 只能出现在循环或 switch 中");
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
        if (tryGenerateComparisonJump(*statement.condition, elseLabel, false)) {
            generateStatement(*statement.thenBranch);
            if (statement.elseBranch) {
                emitter_.emit("jump", {reference(endLabel), "always", "0", "0"});
                emitter_.label(elseLabel);
                generateStatement(*statement.elseBranch);
                emitter_.label(endLabel);
            } else {
                emitter_.label(elseLabel);
            }
            return;
        }
        if (statement.elseBranch && tryGenerateComparisonJump(*statement.condition, elseLabel, true)) {
            generateStatement(*statement.elseBranch);
            emitter_.emit("jump", {reference(endLabel), "always", "0", "0"});
            emitter_.label(elseLabel);
            generateStatement(*statement.thenBranch);
            emitter_.label(endLabel);
            return;
        }
        generateJumpWhenFalse(*statement.condition, elseLabel, statement.location);
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

    void generateSwitch(const Stmt& statement) {
        const ExpressionResult selector = generateExpression(*statement.condition);
        if (selector.type != TypeKind::Int) {
            fail(statement.condition->location,
                 "switch 条件必须是 int，实际为 " + typeName(selector.type));
        }

        const std::string endLabel = uniqueLabel("switch_end");
        std::vector<std::string> clauseLabels;
        clauseLabels.reserve(statement.switchClauses.size());
        std::optional<std::size_t> defaultClause;
        std::vector<std::pair<long long, std::size_t>> cases;
        for (std::size_t index = 0; index < statement.switchClauses.size(); ++index) {
            clauseLabels.push_back(uniqueLabel("switch_case"));
            if (statement.switchClauses[index].value) {
                cases.emplace_back(*statement.switchClauses[index].value, index);
            } else {
                defaultClause = index;
            }
        }
        const std::string unmatchedLabel = defaultClause ? clauseLabels[*defaultClause] : endLabel;

        bool useJumpTable = false;
        long long minimum = 0;
        long long maximum = 0;
        std::size_t tableSize = 0;
        if (cases.size() >= 4) {
            minimum = cases.front().first;
            maximum = cases.front().first;
            for (const auto& [value, clause] : cases) {
                (void)clause;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
            const long double span = static_cast<long double>(maximum) -
                                     static_cast<long double>(minimum) + 1.0L;
            if (span <= 64.0L && span <= static_cast<long double>(cases.size() * 2)) {
                useJumpTable = true;
                tableSize = static_cast<std::size_t>(span);
            }
        }

        if (useJumpTable) {
            emitter_.emit("jump", {reference(unmatchedLabel), "lessThan", selector.operand,
                                    std::to_string(minimum)});
            emitter_.emit("jump", {reference(unmatchedLabel), "greaterThan", selector.operand,
                                    std::to_string(maximum)});
            std::string tableIndex = selector.operand;
            if (minimum != 0) {
                tableIndex = temporary();
                emitter_.emit("op", {"sub", tableIndex, selector.operand,
                                     std::to_string(minimum)});
            }
            const std::string tableLabel = uniqueLabel("switch_table");
            const std::string target = temporary();
            emitter_.emit("op", {"add", target, tableIndex, reference(tableLabel)});
            std::vector<std::string> possibleTargets = clauseLabels;
            if (std::find(possibleTargets.begin(), possibleTargets.end(), unmatchedLabel) ==
                possibleTargets.end()) {
                possibleTargets.push_back(unmatchedLabel);
            }
            emitter_.emitCounterJump(target, std::move(possibleTargets));
            emitter_.label(tableLabel);

            std::unordered_map<long long, std::size_t> clauseByValue;
            for (const auto& [value, clause] : cases) clauseByValue.emplace(value, clause);
            for (std::size_t offset = 0; offset < tableSize; ++offset) {
                const long long value = minimum + static_cast<long long>(offset);
                const auto clause = clauseByValue.find(value);
                const std::string& targetLabel = clause == clauseByValue.end()
                    ? unmatchedLabel : clauseLabels[clause->second];
                emitter_.emit("jump", {reference(targetLabel), "always", "0", "0"});
            }
        } else {
            for (const auto& [value, clause] : cases) {
                emitter_.emit("jump", {reference(clauseLabels[clause]), "equal",
                                        selector.operand, std::to_string(value)});
            }
            emitter_.emit("jump", {reference(unmatchedLabel), "always", "0", "0"});
        }

        breakLabels_.push_back(endLabel);
        for (std::size_t index = 0; index < statement.switchClauses.size(); ++index) {
            emitter_.label(clauseLabels[index]);
            pushScope();
            for (const auto& child : statement.switchClauses[index].statements) {
                generateStatement(*child);
            }
            popScope();
        }
        breakLabels_.pop_back();
        emitter_.label(endLabel);
    }

    void generateWhile(const Stmt& statement) {
        const std::string conditionLabel = uniqueLabel("while_condition");
        const std::string endLabel = uniqueLabel("while_end");
        emitter_.label(conditionLabel);
        generateJumpWhenFalse(*statement.condition, endLabel, statement.location);
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
            generateJumpWhenFalse(*statement.condition, endLabel, statement.location);
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

    bool tryGenerateComparisonJump(const Expr& expression, const std::string& target,
                                   bool jumpWhenTrue) {
        if (expression.kind != Expr::Kind::Binary) return false;
        const Type leftType = expressionType(*expression.left);
        const Type rightType = expressionType(*expression.right);
        const bool nullComparison = leftType == TypeKind::Null || rightType == TypeKind::Null;
        std::string condition;
        if (nullComparison) {
            if ((jumpWhenTrue && expression.text != "==") ||
                (!jumpWhenTrue && expression.text != "!=")) return false;
            condition = "strictEqual";
        } else if (jumpWhenTrue) {
            static const std::unordered_map<std::string, std::string> directConditions = {
                {"==", "equal"}, {"<", "lessThan"}, {"<=", "lessThanEq"},
                {">", "greaterThan"}, {">=", "greaterThanEq"},
            };
            const auto direct = directConditions.find(expression.text);
            if (direct == directConditions.end()) return false;
            condition = direct->second;
        } else {
            if (expression.text != "!=") return false;
            condition = "equal";
        }

        (void)expressionType(expression);
        const ExpressionResult left = generateExpression(*expression.left);
        const ExpressionResult right = generateExpression(*expression.right);
        emitter_.emit("jump", {reference(target), condition, left.operand, right.operand});
        return true;
    }

    void generateJumpWhenFalse(const Expr& expression, const std::string& target,
                               SourceLocation location) {
        if (tryGenerateComparisonJump(expression, target, false)) return;
        const ExpressionResult condition = generateExpression(expression);
        if (condition.type == TypeKind::Bool) {
            emitter_.emit("jump", {reference(target), "equal", condition.operand, "false"});
            return;
        }
        if (!isNumeric(condition.type)) fail(location, "条件需要 bool 或数值类型");
        emitter_.emit("jump", {reference(target), "equal", condition.operand, "0"});
    }

    void generateReturn(const Stmt& statement) {
        if (currentFunction_ == nullptr) fail(statement.location, "return 不在函数内");
        if (currentFunction_->name == "main_loop") {
            if (statement.expression) fail(statement.location, "main_loop 不能返回值");
            emitter_.emit("jump", {reference(mainEntryLabel_), "always", "0", "0"});
            return;
        }
        if (currentFunction_->name == "main_init") {
            if (statement.expression) fail(statement.location, "main_init 不能返回值");
            emitter_.emit("jump", {reference(mainInitEndLabel_), "always", "0", "0"});
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
            case Expr::Kind::Null:
                return {TypeKind::Null, "null", false};
            case Expr::Kind::Variable: {
                const Symbol symbol = resolve(expression.text, expression.location);
                return fromSymbol(symbol);
            }
            case Expr::Kind::Unary:
                return generateUnary(expression);
            case Expr::Kind::Binary:
                return generateBinary(expression);
            case Expr::Kind::Conditional:
                return generateConditional(expression);
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
                (left != TypeKind::Null && right != TypeKind::Null &&
                 left != right && !(isNumeric(left) && isNumeric(right)))) {
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

    std::size_t unitCoordinateOffset(const Expr& expression, std::size_t scalarCount,
                                     std::size_t pointCount) const {
        if (expression.arguments.size() == pointCount &&
            expressionType(*expression.arguments[0]) == Type("point")) {
            return 1;
        }
        if (expression.arguments.size() == scalarCount) {
            if (!isNumeric(expressionType(*expression.arguments[0])) ||
                !isNumeric(expressionType(*expression.arguments[1]))) {
                fail(expression.location, expression.text + " 的坐标必须是数值或 point");
            }
            return 2;
        }
        fail(expression.location, expression.text + " 参数数量错误");
    }

    Type unitMemberType(const Expr& expression) const {
        const auto requireNone = [&] {
            if (!expression.arguments.empty()) fail(expression.location, expression.text + " 不需要参数");
        };
        const auto requireNumeric = [&](std::size_t index) {
            if (!isNumeric(expressionType(*expression.arguments[index]))) {
                fail(expression.arguments[index]->location, expression.text + " 参数必须是数值类型");
            }
        };
        const auto requireBoolean = [&](std::size_t index) {
            const Type type = expressionType(*expression.arguments[index]);
            if (type != TypeKind::Bool && !isNumeric(type)) {
                fail(expression.arguments[index]->location, expression.text + " 参数必须是 bool 或数值类型");
            }
        };

        if (expression.text == "idle" || expression.text == "stop" ||
            expression.text == "auto_pathfind" || expression.text == "payload_drop" ||
            expression.text == "payload_enter" || expression.text == "unbind") {
            requireNone();
            return TypeKind::Void;
        }
        if (expression.text == "move" || expression.text == "pathfind" ||
            expression.text == "mine" || expression.text == "deconstruct") {
            (void)unitCoordinateOffset(expression, 2, 1);
            return TypeKind::Void;
        }
        if (expression.text == "approach" || expression.text == "within") {
            const std::size_t offset = unitCoordinateOffset(expression, 3, 2);
            requireNumeric(offset);
            return expression.text == "within" ? Type(TypeKind::Bool) : Type(TypeKind::Void);
        }
        if (expression.text == "boost" || expression.text == "payload_take") {
            if (expression.arguments.size() != 1) fail(expression.location, expression.text + " 需要一个参数");
            requireBoolean(0);
            return TypeKind::Void;
        }
        if (expression.text == "target") {
            const std::size_t offset = unitCoordinateOffset(expression, 3, 2);
            requireBoolean(offset);
            return TypeKind::Void;
        }
        if (expression.text == "targetp") {
            if (expression.arguments.size() != 2) fail(expression.location, "targetp 需要目标和开火状态");
            requireAssignable(TypeKind::Posc, expressionType(*expression.arguments[0]),
                              expression.arguments[0]->location);
            requireBoolean(1);
            return TypeKind::Void;
        }
        if (expression.text == "item_drop") {
            if (expression.arguments.size() != 2 ||
                expressionType(*expression.arguments[0]) != TypeKind::Building ||
                expressionType(*expression.arguments[1]) != TypeKind::Int) {
                fail(expression.location, "item_drop 需要 building 和 int 参数");
            }
            return TypeKind::Void;
        }
        if (expression.text == "discard_items") {
            if (expression.arguments.size() != 1 ||
                expressionType(*expression.arguments[0]) != TypeKind::Int) {
                fail(expression.location, "discard_items 需要一个 int 参数");
            }
            return TypeKind::Void;
        }
        if (expression.text == "item_take") {
            if (expression.arguments.size() != 3 ||
                expressionType(*expression.arguments[0]) != TypeKind::Building ||
                expressionType(*expression.arguments[1]) != TypeKind::Item ||
                expressionType(*expression.arguments[2]) != TypeKind::Int) {
                fail(expression.location, "item_take 需要 building、item 和 int 参数");
            }
            return TypeKind::Void;
        }
        if (expression.text == "set_flag") {
            if (expression.arguments.size() != 1) fail(expression.location, "set_flag 需要一个参数");
            requireNumeric(0);
            return TypeKind::Void;
        }
        if (expression.text == "build") {
            const bool pointForm = !expression.arguments.empty() &&
                                   expressionType(*expression.arguments[0]) == Type("point");
            const std::size_t offset = pointForm ? 1 : 2;
            const std::size_t minimum = pointForm ? 3 : 4;
            if (expression.arguments.size() != minimum && expression.arguments.size() != minimum + 1) {
                fail(expression.location, "build 需要坐标、block、旋转和可选配置");
            }
            if (!pointForm) {
                requireNumeric(0);
                requireNumeric(1);
            }
            if (expressionType(*expression.arguments[offset]) != TypeKind::Block) {
                fail(expression.arguments[offset]->location, "build 方块参数必须是 block");
            }
            if (!isNumeric(expressionType(*expression.arguments[offset + 1]))) {
                fail(expression.arguments[offset + 1]->location, "build 旋转参数必须是数值类型");
            }
            if (expression.arguments.size() == minimum + 1) {
                const Type config = expressionType(*expression.arguments[offset + 2]);
                const bool valid = config == TypeKind::Null || config == TypeKind::Bool || isNumeric(config) ||
                                   config == TypeKind::Building || config == TypeKind::Block ||
                                   config == TypeKind::UnitKind || config == TypeKind::Item ||
                                   config == TypeKind::Liquid || config == TypeKind::SensorValue;
                if (!valid) fail(expression.arguments[offset + 2]->location, "build 配置参数类型不受支持");
            }
            return TypeKind::Void;
        }
        if (expression.text == "get_block") {
            const std::size_t offset = unitCoordinateOffset(expression, 5, 4);
            const std::array<Type, 3> outputs = {TypeKind::Block, TypeKind::Building, TypeKind::Block};
            for (std::size_t index = 0; index < outputs.size(); ++index) {
                if (expressionType(*expression.arguments[offset + index]) != outputs[index]) {
                    fail(expression.arguments[offset + index]->location,
                         "get_block 输出参数类型必须依次为 block、building、block");
                }
            }
            return TypeKind::Void;
        }
        if (expression.text == "get_block_type" || expression.text == "get_block_building" ||
            expression.text == "get_block_floor") {
            (void)unitCoordinateOffset(expression, 2, 1);
            return expression.text == "get_block_building" ? Type(TypeKind::Building)
                                                             : Type(TypeKind::Block);
        }
        fail(expression.location, "未知的 unit 成员函数: " + expression.text);
    }

    Type expressionType(const Expr& expression) const {
        switch (expression.kind) {
            case Expr::Kind::Number:
                return expression.text.find_first_of(".eE") == std::string::npos ? TypeKind::Int : TypeKind::Number;
            case Expr::Kind::String: return TypeKind::String;
            case Expr::Kind::Boolean: return TypeKind::Bool;
            case Expr::Kind::Null: return TypeKind::Null;
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
                    const bool nullComparison = left == TypeKind::Null || right == TypeKind::Null;
                    if (!nullComparison && left != right && !(isNumeric(left) && isNumeric(right))) {
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
            case Expr::Kind::Conditional: {
                const Type condition = expressionType(*expression.left);
                if (condition != TypeKind::Bool && !isNumeric(condition)) {
                    fail(expression.left->location, "三目运算符条件需要 bool 或数值类型");
                }
                const Type trueType = expressionType(*expression.right);
                const Type falseType = expressionType(*expression.third);
                if (trueType == TypeKind::Void || falseType == TypeKind::Void) {
                    if (trueType == TypeKind::Void && falseType == TypeKind::Void) {
                        return TypeKind::Void;
                    }
                    fail(expression.location, "三目运算符两支必须同时为 void 或同时产生值");
                }
                if (isRadarSelector(trueType) || isRadarSelector(falseType)) {
                    fail(expression.location, "Radar 编译期选择器不能作为三目运算符结果");
                }
                return conditionalCommonType(trueType, falseType, expression.location);
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
                    if (const std::optional<std::string> member =
                            memberFunctionName(receiverType, expression.text)) {
                        const FunctionDecl& declaration = *functions_.at(*member).declaration;
                        if (expression.arguments.size() + 1 != declaration.parameters.size()) {
                            fail(expression.location, "成员函数 " + receiverType.structName + "." +
                                                      expression.text + " 参数数量错误");
                        }
                        if (!expression.receiver || expression.receiver->kind == Expr::Kind::InitializerList) {
                            fail(expression.location, "成员函数接收者需要可赋值左值");
                        }
                        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                            const Parameter& parameter = declaration.parameters[index + 1];
                            if (parameter.reference) {
                                const Type argumentType = expressionType(*expression.arguments[index]);
                                if (argumentType != parameter.type) {
                                    fail(expression.arguments[index]->location,
                                         "引用参数类型必须精确匹配 " + typeName(parameter.type));
                                }
                            } else if (expression.arguments[index]->kind != Expr::Kind::InitializerList) {
                                requireAssignable(parameter.type,
                                                  expressionType(*expression.arguments[index]),
                                                  expression.arguments[index]->location);
                            }
                        }
                        return declaration.returnType;
                    }
                    if (expression.text == "get") {
                        if (!isSenseableReceiver(receiverType)) {
                            fail(expression.location, "get 的接收者不支持 sensor");
                        }
                        if (expression.arguments.size() != 1) {
                            fail(expression.location, "get 需要一个 sensor 或内容常量参数");
                        }
                        const Expr& selector = *expression.arguments[0];
                        const Type selectorType = expressionType(selector);
                        if (selectorType == TypeKind::Sensor) {
                            if (selector.kind != Expr::Kind::Variable || selector.text.empty() ||
                                selector.text.front() != '@' ||
                                !isSenseableSensorName(std::string_view(selector.text).substr(1))) {
                                fail(selector.location, "get 的 sensor 参数必须是可感知的内置 @ 常量");
                            }
                            return *sensorResultType(std::string_view(selector.text).substr(1), receiverType);
                        }
                        if (selectorType == TypeKind::Item || selectorType == TypeKind::Liquid ||
                            selectorType == TypeKind::Block || selectorType == TypeKind::UnitKind) {
                            return TypeKind::Number;
                        }
                        fail(selector.location, "get 参数必须是 sensor、item、liquid、block 或 unit_kind 常量");
                    }
                    if (const std::optional<std::string_view> sensor = sensorAlias(expression.text)) {
                        if (!isSenseableReceiver(receiverType)) {
                            fail(expression.location, expression.text + " 的接收者不支持 sensor");
                        }
                        if (!expression.arguments.empty()) {
                            fail(expression.location, expression.text + " 不需要参数");
                        }
                        return *sensorResultType(*sensor, receiverType);
                    }
                    if (receiverType == TypeKind::Unit) return unitMemberType(expression);
                    if (expression.text == "radar" || builtinRadarMethod(expression.text)) {
                        if (receiverType != TypeKind::Building) {
                            fail(expression.location, "Radar 的接收者必须是 building");
                        }
                        const std::optional<RadarMethod> method = builtinRadarMethod(expression.text);
                        if (!method && expression.arguments.size() < 2) {
                            fail(expression.location, "radar 需要 radar_sort 和 int order 参数");
                        }
                        const std::size_t filterCount = method
                            ? expression.arguments.size()
                            : expression.arguments.size() >= 2 ? expression.arguments.size() - 2 : 4;
                        if (filterCount > 3) {
                            fail(expression.location, expression.text + " 最多接受三个 Radar 筛选条件");
                        }
                        for (std::size_t index = 0; index < filterCount; ++index) {
                            const Expr& argument = *expression.arguments[index];
                            const std::optional<RadarConstant> selector =
                                argument.kind == Expr::Kind::Variable
                                    ? builtinRadarConstant(argument.text) : std::nullopt;
                            if (!selector || selector->type != TypeKind::RadarFilter) {
                                fail(argument.location, "Radar 筛选条件必须是内置 radar_filter 常量");
                            }
                        }
                        if (!method) {
                            const Expr& sort = *expression.arguments[filterCount];
                            const std::optional<RadarConstant> selector =
                                sort.kind == Expr::Kind::Variable
                                    ? builtinRadarConstant(sort.text) : std::nullopt;
                            if (!selector || selector->type != TypeKind::RadarSort) {
                                fail(sort.location, "Radar 排序必须是内置 radar_sort 常量");
                            }
                            if (expressionType(*expression.arguments.back()) != TypeKind::Int) {
                                fail(expression.arguments.back()->location, "Radar order 必须是 int");
                            }
                        }
                        return TypeKind::Unit;
                    }
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
                    if (expression.text == "shoot") {
                        if (expression.arguments.size() != 2) {
                            fail(expression.location, "shoot 需要 point 和开火状态两个参数");
                        }
                        if (expressionType(*expression.arguments[0]) != Type("point")) {
                            fail(expression.arguments[0]->location, "shoot 的目标必须是 point");
                        }
                        const Type enabled = expressionType(*expression.arguments[1]);
                        if (enabled != TypeKind::Bool && !isNumeric(enabled)) {
                            fail(expression.arguments[1]->location, "shoot 的开火状态必须是 bool 或数值类型");
                        }
                        return TypeKind::Void;
                    }
                    if (expression.text == "shootp") {
                        if (expression.arguments.size() != 2) {
                            fail(expression.location, "shootp 需要 posc 和开火状态两个参数");
                        }
                        requireAssignable(TypeKind::Posc, expressionType(*expression.arguments[0]),
                                          expression.arguments[0]->location);
                        const Type enabled = expressionType(*expression.arguments[1]);
                        if (enabled != TypeKind::Bool && !isNumeric(enabled)) {
                            fail(expression.arguments[1]->location, "shootp 的开火状态必须是 bool 或数值类型");
                        }
                        return TypeKind::Void;
                    }
                    if (expression.text == "set_color") {
                        if (expression.arguments.size() != 1 ||
                            expressionType(*expression.arguments[0]) != TypeKind::PackedColor) {
                            fail(expression.location, "set_color 需要一个 packed_color 参数");
                        }
                        return TypeKind::Void;
                    }
                    if (const std::optional<Type> parameter = configValueMemberType(expression.text)) {
                        if (expression.arguments.size() != 1) {
                            fail(expression.location, expression.text + " 需要一个 " +
                                                      typeName(*parameter) + " 参数");
                        }
                        requireAssignable(*parameter, expressionType(*expression.arguments[0]),
                                          expression.arguments[0]->location);
                        return TypeKind::Void;
                    }
                    if (isPayloadKindMember(expression.text)) {
                        if (expression.arguments.size() != 1) {
                            fail(expression.location, expression.text + " 需要一个 block 或 unit_kind 参数");
                        }
                        const Type argument = expressionType(*expression.arguments[0]);
                        if (argument != TypeKind::Block && argument != TypeKind::UnitKind) {
                            fail(expression.arguments[0]->location,
                                 expression.text + " 参数必须是 block 或 unit_kind");
                        }
                        return TypeKind::Void;
                    }
                    if (isConfigClearMember(expression.text)) {
                        if (!expression.arguments.empty()) {
                            fail(expression.location, expression.text + " 不需要参数");
                        }
                        return TypeKind::Void;
                    }
                    if (expression.text == "copy_configuration_from") {
                        if (expression.arguments.size() != 1 ||
                            expressionType(*expression.arguments[0]) != TypeKind::Building) {
                            fail(expression.location, "copy_configuration_from 需要一个 building 参数");
                        }
                        return TypeKind::Void;
                    }
                    if (expression.text == "set_rotation") {
                        if (expression.arguments.size() != 1 ||
                            expressionType(*expression.arguments[0]) != TypeKind::Int) {
                            fail(expression.location, "set_rotation 需要一个 int 参数");
                        }
                        return TypeKind::Void;
                    }
                    fail(expression.location, "未知的内置成员函数: " + expression.text);
                }
                if (const std::optional<std::string> member =
                        currentMemberFunctionName(expression.text)) {
                    const FunctionDecl& declaration = *functions_.at(*member).declaration;
                    if (expression.arguments.size() + 1 != declaration.parameters.size()) {
                        fail(expression.location, "成员函数 " + currentFunction_->memberOf + "." +
                                                  expression.text + " 参数数量错误");
                    }
                    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                        const Parameter& parameter = declaration.parameters[index + 1];
                        if (parameter.reference) {
                            const Type argumentType = expressionType(*expression.arguments[index]);
                            if (argumentType != parameter.type) {
                                fail(expression.arguments[index]->location,
                                     "引用参数类型必须精确匹配 " + typeName(parameter.type));
                            }
                        } else if (expression.arguments[index]->kind != Expr::Kind::InitializerList) {
                            requireAssignable(parameter.type,
                                              expressionType(*expression.arguments[index]),
                                              expression.arguments[index]->location);
                        }
                    }
                    return declaration.returnType;
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
                if (expression.text == "unit_bind") {
                    if (expression.arguments.size() != 1) {
                        fail(expression.location, "unit_bind 需要一个 unit_kind 或 unit 参数");
                    }
                    const Type argument = expressionType(*expression.arguments[0]);
                    if (argument != TypeKind::UnitKind && argument != TypeKind::Unit) {
                        fail(expression.arguments[0]->location,
                             "unit_bind 参数必须是 unit_kind 或 unit");
                    }
                    return TypeKind::Unit;
                }
                if (const std::optional<LookupFunction> lookup = builtinLookupFunction(expression.text)) {
                    if (expression.arguments.size() != 1 ||
                        expressionType(*expression.arguments.front()) != TypeKind::Int) {
                        fail(expression.location, expression.text + " 需要一个 int 参数");
                    }
                    return lookup->result;
                }
                if (isBuiltinFunction(expression.text)) return TypeKind::Void;
                const auto function = functions_.find(expression.text);
                if (function == functions_.end()) fail(expression.location, "未定义的函数: " + expression.text);
                const FunctionDecl& declaration = *function->second.declaration;
                if (expression.arguments.size() != declaration.parameters.size()) {
                    fail(expression.location, "函数 " + declaration.name + " 参数数量错误");
                }
                for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                    if (declaration.parameters[index].reference) {
                        if (expression.arguments[index]->kind == Expr::Kind::InitializerList) {
                            fail(expression.arguments[index]->location, "引用参数需要可赋值左值");
                        }
                        const Type argumentType = expressionType(*expression.arguments[index]);
                        if (argumentType != declaration.parameters[index].type) {
                            fail(expression.arguments[index]->location,
                                 "引用参数类型必须精确匹配 " + typeName(declaration.parameters[index].type));
                        }
                    } else if (expression.arguments[index]->kind != Expr::Kind::InitializerList) {
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
                if (location) {
                    location->address = addressAdd(location->address, std::to_string(offset));
                    if (location->normalized) {
                        if (offset > static_cast<std::size_t>(std::numeric_limits<long long>::max()) ||
                            location->constantOffset > std::numeric_limits<long long>::max() -
                                                           static_cast<long long>(offset)) {
                            location->normalized = false;
                        } else {
                            location->constantOffset += static_cast<long long>(offset);
                        }
                    }
                }
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

    static Type conditionalCommonType(const Type& trueType, const Type& falseType,
                                      SourceLocation location) {
        if (trueType == falseType) return trueType;
        if (isNumeric(trueType) && isNumeric(falseType)) {
            return commonNumericType(trueType, falseType, "?:");
        }
        if (canAssign(trueType, falseType)) return trueType;
        if (canAssign(falseType, trueType)) return falseType;
        fail(location, "三目运算符两支类型不兼容: " + typeName(trueType) + " 和 " +
                       typeName(falseType));
    }

    bool isSelectSafe(const Expr& expression) const {
        switch (expression.kind) {
            case Expr::Kind::Number:
            case Expr::Kind::String:
            case Expr::Kind::Boolean:
            case Expr::Kind::Null:
            case Expr::Kind::Variable:
            case Expr::Kind::Sizeof:
                return true;
            case Expr::Kind::Unary:
                return isSelectSafe(*expression.right);
            case Expr::Kind::Binary:
                return isSelectSafe(*expression.left) && isSelectSafe(*expression.right);
            case Expr::Kind::Conditional:
                return isSelectSafe(*expression.left) && isSelectSafe(*expression.right) &&
                       isSelectSafe(*expression.third);
            case Expr::Kind::Call: {
                if (expression.receiver) return false;
                const bool pureBuiltin =
                    (builtinOpFunction(expression.text) && expression.text != "rand") ||
                    expression.text == "dot" || expression.text == "cross" ||
                    expression.text == "rgb" || expression.text == "rgba" ||
                    expression.text == "pack_color" || expression.text == "unpack_color";
                return pureBuiltin &&
                       std::all_of(expression.arguments.begin(), expression.arguments.end(),
                                   [&](const auto& argument) { return isSelectSafe(*argument); });
            }
            case Expr::Kind::Member:
                return isSelectSafe(*expression.left);
            case Expr::Kind::Index:
                return isSelectSafe(*expression.left) && isSelectSafe(*expression.right);
            case Expr::Kind::InitializerList:
            case Expr::Kind::TypedInitializer:
                return std::all_of(expression.arguments.begin(), expression.arguments.end(),
                                   [&](const auto& argument) { return isSelectSafe(*argument); });
            case Expr::Kind::Assign:
            case Expr::Kind::Prefix:
            case Expr::Kind::Postfix:
                return false;
        }
        return false;
    }

    struct SelectCondition {
        std::string operation;
        std::string left;
        std::string right;
        bool invert = false;
    };

    SelectCondition generateSelectCondition(const Expr& expression) {
        if (expression.kind == Expr::Kind::Binary) {
            static const std::unordered_map<std::string, std::string> operations = {
                {"==", "equal"}, {"!=", "notEqual"}, {"<", "lessThan"},
                {"<=", "lessThanEq"}, {">", "greaterThan"}, {">=", "greaterThanEq"},
            };
            const auto operation = operations.find(expression.text);
            if (operation != operations.end()) {
                (void)expressionType(expression);
                const ExpressionResult left = generateExpression(*expression.left);
                const ExpressionResult right = generateExpression(*expression.right);
                const bool nullComparison = left.type == TypeKind::Null || right.type == TypeKind::Null;
                if (nullComparison) {
                    return {"strictEqual", left.operand, right.operand, expression.text == "!="};
                }
                return {operation->second, left.operand, right.operand, false};
            }
        }
        const ExpressionResult condition = generateExpression(expression);
        if (condition.type == TypeKind::Bool) {
            return {"notEqual", condition.operand, "false", false};
        }
        if (!isNumeric(condition.type)) fail(expression.location, "条件需要 bool 或数值类型");
        return {"notEqual", condition.operand, "0", false};
    }

    ExpressionResult generateConditional(const Expr& expression) {
        const Type resultType = expressionType(expression);
        if (!isSelectSafe(*expression.right) || !isSelectSafe(*expression.third)) {
            const std::string falseLabel = uniqueLabel("conditional_false");
            const std::string endLabel = uniqueLabel("conditional_end");
            std::vector<std::string> results;
            results.reserve(typeSize(resultType));
            for (std::size_t index = 0; index < typeSize(resultType); ++index) {
                results.push_back(temporary());
            }

            generateJumpWhenFalse(*expression.left, falseLabel, expression.left->location);
            const ExpressionResult trueValue = generateValue(*expression.right, resultType);
            const std::vector<std::string> trueOperands = operandsOf(trueValue);
            for (std::size_t index = 0; index < results.size(); ++index) {
                emitter_.emit("set", {results[index], trueOperands[index]});
            }
            emitter_.emit("jump", {reference(endLabel), "always", "0", "0"});

            emitter_.label(falseLabel);
            const ExpressionResult falseValue = generateValue(*expression.third, resultType);
            const std::vector<std::string> falseOperands = operandsOf(falseValue);
            for (std::size_t index = 0; index < results.size(); ++index) {
                emitter_.emit("set", {results[index], falseOperands[index]});
            }
            emitter_.label(endLabel);

            if (resultType == TypeKind::Void) return {TypeKind::Void, "", false};
            if (resultType.isRuntimeAggregate()) return {resultType, "", false, std::move(results)};
            return {resultType, results.front(), false};
        }
        const SelectCondition condition = generateSelectCondition(*expression.left);
        const ExpressionResult trueValue = generateValue(*expression.right, resultType);
        const ExpressionResult falseValue = generateValue(*expression.third, resultType);
        const std::vector<std::string> trueOperands = operandsOf(trueValue);
        const std::vector<std::string> falseOperands = operandsOf(falseValue);
        std::vector<std::string> results;
        results.reserve(trueOperands.size());
        for (std::size_t index = 0; index < trueOperands.size(); ++index) {
            const std::string result = temporary();
            emitter_.emit("select", {result, condition.operation, condition.left, condition.right,
                                     condition.invert ? falseOperands[index] : trueOperands[index],
                                     condition.invert ? trueOperands[index] : falseOperands[index]});
            results.push_back(result);
        }
        if (resultType.isRuntimeAggregate()) return {resultType, "", false, std::move(results)};
        return {resultType, results.front(), false};
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
                const bool nullComparison = left.type == TypeKind::Null || right.type == TypeKind::Null;
                const bool compatible = nullComparison || left.type == right.type ||
                                        (isNumeric(left.type) && isNumeric(right.type));
                if (!compatible) fail(expression.location, "不能比较 " + typeName(left.type) + " 和 " + typeName(right.type));
            } else if (!isNumeric(left.type) || !isNumeric(right.type)) {
                fail(expression.location, "顺序比较需要数值操作数");
            }
            const bool nullComparison = left.type == TypeKind::Null || right.type == TypeKind::Null;
            if (nullComparison) {
                emitter_.emit("op", {"strictEqual", result, left.operand, right.operand});
                if (expression.text == "!=") {
                    emitter_.emit("op", {"equal", result, result, "false"});
                }
            } else {
                emitter_.emit("op", {comparison->second, result, left.operand, right.operand});
            }
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

    struct ReferenceBinding {
        std::size_t parameterIndex = 0;
        Type type;
        std::vector<std::string> directStorage;
        std::optional<ExpressionResult::MemoryLocation> memory;
        bool restricted = false;
        SourceLocation location;
    };

    enum class AliasRelation { Disjoint, Overlap, Unknown };

    AliasRelation memoryAlias(const ReferenceBinding& left, const ReferenceBinding& right) const {
        const ExpressionResult::MemoryLocation& first = *left.memory;
        const ExpressionResult::MemoryLocation& second = *right.memory;
        if (first.identityHandle != second.identityHandle) {
            const std::optional<Type> firstLink = implicitLinkType(first.identityHandle);
            const std::optional<Type> secondLink = implicitLinkType(second.identityHandle);
            if (firstLink == TypeKind::Memory && secondLink == TypeKind::Memory) {
                return AliasRelation::Disjoint;
            }
            return AliasRelation::Unknown;
        }
        if (first.address == second.address) return AliasRelation::Overlap;
        if (!first.normalized || !second.normalized ||
            first.identityBase != second.identityBase ||
            first.identityIndex != second.identityIndex ||
            first.indexScale != second.indexScale) {
            return AliasRelation::Unknown;
        }

        const long long firstStart = first.constantOffset;
        const long long secondStart = second.constantOffset;
        const std::uint64_t firstSize = static_cast<std::uint64_t>(typeSize(left.type));
        const std::uint64_t secondSize = static_cast<std::uint64_t>(typeSize(right.type));
        if (firstStart < secondStart) {
            const std::uint64_t distance = static_cast<std::uint64_t>(secondStart) -
                                           static_cast<std::uint64_t>(firstStart);
            return firstSize <= distance ? AliasRelation::Disjoint : AliasRelation::Overlap;
        }
        if (secondStart < firstStart) {
            const std::uint64_t distance = static_cast<std::uint64_t>(firstStart) -
                                           static_cast<std::uint64_t>(secondStart);
            return secondSize <= distance ? AliasRelation::Disjoint : AliasRelation::Overlap;
        }
        return AliasRelation::Overlap;
    }

    void validateReferenceAliases(const std::vector<ReferenceBinding>& bindings,
                                  const FunctionDecl& function) const {
        for (std::size_t left = 0; left < bindings.size(); ++left) {
            for (std::size_t right = left + 1; right < bindings.size(); ++right) {
                AliasRelation relation = AliasRelation::Disjoint;
                if (bindings[left].memory && bindings[right].memory) {
                    relation = memoryAlias(bindings[left], bindings[right]);
                } else if (!bindings[left].memory && !bindings[right].memory) {
                    for (const std::string& first : bindings[left].directStorage) {
                        if (std::find(bindings[right].directStorage.begin(),
                                      bindings[right].directStorage.end(), first) !=
                            bindings[right].directStorage.end()) {
                            relation = AliasRelation::Overlap;
                            break;
                        }
                    }
                }

                const Parameter& firstParameter = function.parameters[bindings[left].parameterIndex];
                const Parameter& secondParameter = function.parameters[bindings[right].parameterIndex];
                if (relation == AliasRelation::Overlap) {
                    fail(bindings[right].location,
                         "引用实参存在已知别名: " + firstParameter.name + " 与 " + secondParameter.name);
                }
                if (relation == AliasRelation::Unknown &&
                    (!bindings[left].restricted || !bindings[right].restricted)) {
                    fail(bindings[right].location,
                         "无法证明内存引用参数 " + firstParameter.name + " 与 " +
                         secondParameter.name + " 不重叠，需要 restrict");
                }
            }
        }
    }

    ExpressionResult generateCall(const Expr& expression) {
        if (expression.receiver) {
            const Type receiverType = expressionType(*expression.receiver);
            if (const std::optional<std::string> member =
                    memberFunctionName(receiverType, expression.text)) {
                const FunctionDecl& declaration = *functions_.at(*member).declaration;
                if (expression.arguments.size() + 1 != declaration.parameters.size()) {
                    fail(expression.location, "成员函数 " + receiverType.structName + "." +
                                              expression.text + " 参数数量错误");
                }
                std::vector<const Expr*> arguments;
                arguments.reserve(expression.arguments.size() + 1);
                arguments.push_back(expression.receiver.get());
                for (const auto& argument : expression.arguments) arguments.push_back(argument.get());
                return generateUserFunctionCall(*member, arguments, expression.location);
            }
            return generateBuiltinMemberCall(expression);
        }
        if (const std::optional<std::string> member = currentMemberFunctionName(expression.text)) {
            const FunctionDecl& declaration = *functions_.at(*member).declaration;
            if (expression.arguments.size() + 1 != declaration.parameters.size()) {
                fail(expression.location, "成员函数 " + currentFunction_->memberOf + "." +
                                          expression.text + " 参数数量错误");
            }
            Expr thisExpression;
            thisExpression.kind = Expr::Kind::Variable;
            thisExpression.location = expression.location;
            thisExpression.text = "__this";
            std::vector<const Expr*> arguments;
            arguments.reserve(expression.arguments.size() + 1);
            arguments.push_back(&thisExpression);
            for (const auto& argument : expression.arguments) arguments.push_back(argument.get());
            return generateUserFunctionCall(*member, arguments, expression.location);
        }
        if (builtinOpFunction(expression.text)) return generateBuiltinOp(expression);
        if (expression.text == "print") return generatePrint(expression);
        if (expression.text == "printchar" || expression.text == "putchar") return generatePrintChar(expression);
        if (expression.text == "format") return generateFormat(expression);
        if (expression.text == "printf") return generatePrintf(expression);
        if (expression.text == "printflush") return generatePrintFlush(expression);
        if (expression.text == "drawflush") return generateDrawFlush(expression);
        if (expression.text == "wait") return generateWait(expression);
        if (expression.text == "stop" || expression.text == "exit") return generateStop(expression);
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
        if (expression.text == "unit_bind") return generateUnitBind(expression);
        if (const std::optional<LookupFunction> lookup = builtinLookupFunction(expression.text)) {
            return generateLookup(expression, *lookup);
        }

        std::vector<const Expr*> arguments;
        arguments.reserve(expression.arguments.size());
        for (const auto& argument : expression.arguments) arguments.push_back(argument.get());
        return generateUserFunctionCall(expression.text, arguments, expression.location);
    }

    ExpressionResult generateUnitBind(const Expr& expression) {
        if (expression.arguments.size() != 1) fail(expression.location, "unit_bind 需要一个参数");
        const ExpressionResult value = generateExpression(*expression.arguments[0]);
        if (value.type != TypeKind::UnitKind && value.type != TypeKind::Unit) {
            fail(expression.arguments[0]->location, "unit_bind 参数必须是 unit_kind 或 unit");
        }
        emitter_.emit(value.type == TypeKind::Unit ? "ubindunit" : "ubind", {value.operand});
        const std::string result = temporary();
        emitter_.emit("set", {result, "@unit"});
        return {TypeKind::Unit, result, false};
    }

    ExpressionResult generateUserFunctionCall(const std::string& functionName,
                                                const std::vector<const Expr*>& argumentExpressions,
                                                SourceLocation location) {
        const auto functionIterator = functions_.find(functionName);
        if (functionIterator == functions_.end()) fail(location, "未定义的函数: " + functionName);
        const FunctionInfo& info = functionIterator->second;
        const FunctionDecl& function = *info.declaration;
        if (argumentExpressions.size() != function.parameters.size()) {
            fail(location, "函数 " + function.name + " 需要 " +
                                      std::to_string(function.parameters.size()) + " 个参数");
        }

        std::vector<ExpressionResult> arguments;
        std::vector<ReferenceBinding> referenceBindings;
        arguments.reserve(argumentExpressions.size());
        for (std::size_t index = 0; index < argumentExpressions.size(); ++index) {
            const Expr& argumentExpression = *argumentExpressions[index];
            const Parameter& parameter = function.parameters[index];
            if (!parameter.reference) {
                ExpressionResult value = generateValue(argumentExpression, parameter.type);
                arguments.push_back(materialize(value));
                continue;
            }

            ExpressionResult value = generateExpression(argumentExpression);
            if (value.type != parameter.type) {
                fail(argumentExpression.location,
                     "引用参数类型必须精确匹配 " + typeName(parameter.type));
            }
            if (!value.lvalue) fail(argumentExpression.location, "引用参数需要可赋值左值");

            ReferenceBinding binding;
            binding.parameterIndex = index;
            binding.type = parameter.type;
            binding.restricted = parameter.restricted;
            binding.location = argumentExpression.location;
            if (value.memoryLocation) {
                binding.memory = value.memoryLocation;
                const std::string frozenHandle = temporary();
                const std::string frozenAddress = temporary();
                emitter_.emit("set", {frozenHandle, binding.memory->handle});
                emitter_.emit("set", {frozenAddress, binding.memory->address});
                binding.memory->handle = frozenHandle;
                binding.memory->address = frozenAddress;
            } else {
                binding.directStorage = operandsOf(value);
            }
            arguments.push_back(materialize(value));
            referenceBindings.push_back(std::move(binding));
        }
        validateReferenceAliases(referenceBindings, function);
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
        std::vector<std::string> results;
        if (function.returnType != TypeKind::Void) {
            for (const std::string& resultStorage : info.resultStorage) {
                const std::string result = temporary();
                emitter_.emit("set", {result, resultStorage});
                results.push_back(result);
            }
        }
        for (const ReferenceBinding& binding : referenceBindings) {
            const std::vector<std::string>& parameterValues = info.parameterStorage[binding.parameterIndex];
            if (binding.memory) {
                const ExpressionResult destination{binding.type, "", true, {}, binding.memory};
                const ExpressionResult source = binding.type.isRuntimeAggregate()
                    ? ExpressionResult{binding.type, "", false, parameterValues}
                    : ExpressionResult{binding.type, parameterValues.front(), false};
                storeMemory(destination, source);
            } else {
                for (std::size_t component = 0; component < binding.directStorage.size(); ++component) {
                    emitter_.emit("set", {binding.directStorage[component], parameterValues[component]});
                }
            }
        }
        if (function.returnType == TypeKind::Void) return {TypeKind::Void, "", false};
        if (function.returnType.isRuntimeAggregate()) return {function.returnType, "", false, std::move(results)};
        return {function.returnType, results.front(), false, {}};
    }

    std::array<std::string, 2> generateUnitCoordinates(const Expr& expression,
                                                       std::size_t scalarCount,
                                                       std::size_t pointCount,
                                                       std::size_t& offset) {
        if (expression.arguments.size() == pointCount) {
            const ExpressionResult point = generateValue(*expression.arguments[0], Type("point"));
            offset = 1;
            return {point.components[0], point.components[1]};
        }
        if (expression.arguments.size() != scalarCount) {
            fail(expression.location, expression.text + " 参数数量错误");
        }
        const ExpressionResult x = generateExpression(*expression.arguments[0]);
        const ExpressionResult y = generateExpression(*expression.arguments[1]);
        if (!isNumeric(x.type) || !isNumeric(y.type)) {
            fail(expression.location, expression.text + " 的坐标必须是数值或 point");
        }
        offset = 2;
        return {x.operand, y.operand};
    }

    void emitUnitControl(const ExpressionResult& receiver, std::string command,
                         std::vector<std::string> parameters = {}) {
        emitter_.emit("ubindunit", {receiver.operand});
        parameters.resize(5, "0");
        parameters.insert(parameters.begin(), std::move(command));
        emitter_.emit("ucontrol", std::move(parameters));
    }

    ExpressionResult generateUnitMemberCall(const Expr& expression,
                                             const ExpressionResult& receiver) {
        if (expression.text == "idle" || expression.text == "stop" ||
            expression.text == "auto_pathfind" || expression.text == "payload_drop" ||
            expression.text == "payload_enter" || expression.text == "unbind") {
            static const std::unordered_map<std::string_view, std::string_view> commands = {
                {"idle", "idle"}, {"stop", "stop"}, {"auto_pathfind", "autoPathfind"},
                {"payload_drop", "payDrop"}, {"payload_enter", "payEnter"},
                {"unbind", "unbind"},
            };
            emitUnitControl(receiver, std::string(commands.at(expression.text)));
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "move" || expression.text == "pathfind" ||
            expression.text == "mine" || expression.text == "deconstruct") {
            std::size_t offset = 0;
            const auto coordinates = generateUnitCoordinates(expression, 2, 1, offset);
            (void)offset;
            emitUnitControl(receiver, expression.text, {coordinates[0], coordinates[1]});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "approach" || expression.text == "within") {
            std::size_t offset = 0;
            const auto coordinates = generateUnitCoordinates(expression, 3, 2, offset);
            const ExpressionResult radius = generateExpression(*expression.arguments[offset]);
            if (expression.text == "within") {
                const std::string result = temporary();
                emitUnitControl(receiver, "within", {coordinates[0], coordinates[1], radius.operand, result});
                return {TypeKind::Bool, result, false};
            }
            emitUnitControl(receiver, "approach", {coordinates[0], coordinates[1], radius.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "boost" || expression.text == "payload_take") {
            const ExpressionResult enabled = toBoolean(generateExpression(*expression.arguments[0]),
                                                       expression.arguments[0]->location);
            emitUnitControl(receiver, expression.text == "boost" ? "boost" : "payTake",
                            {enabled.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "target") {
            std::size_t offset = 0;
            const auto coordinates = generateUnitCoordinates(expression, 3, 2, offset);
            const ExpressionResult shoot = toBoolean(generateExpression(*expression.arguments[offset]),
                                                     expression.arguments[offset]->location);
            emitUnitControl(receiver, "target", {coordinates[0], coordinates[1], shoot.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "targetp") {
            const ExpressionResult target = generateValue(*expression.arguments[0], TypeKind::Posc);
            const ExpressionResult shoot = toBoolean(generateExpression(*expression.arguments[1]),
                                                     expression.arguments[1]->location);
            emitUnitControl(receiver, "targetp", {target.operand, shoot.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "item_drop" || expression.text == "discard_items") {
            std::string target = "@air";
            std::size_t amountIndex = 0;
            if (expression.text == "item_drop") {
                target = generateValue(*expression.arguments[0], TypeKind::Building).operand;
                amountIndex = 1;
            }
            const ExpressionResult amount = generateValue(*expression.arguments[amountIndex], TypeKind::Int);
            emitUnitControl(receiver, "itemDrop", {target, amount.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "item_take") {
            const ExpressionResult source = generateValue(*expression.arguments[0], TypeKind::Building);
            const ExpressionResult item = generateValue(*expression.arguments[1], TypeKind::Item);
            const ExpressionResult amount = generateValue(*expression.arguments[2], TypeKind::Int);
            emitUnitControl(receiver, "itemTake", {source.operand, item.operand, amount.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "set_flag") {
            const ExpressionResult value = generateExpression(*expression.arguments[0]);
            emitUnitControl(receiver, "flag", {value.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "build") {
            const bool pointForm = expressionType(*expression.arguments[0]) == Type("point");
            std::size_t offset = 0;
            std::array<std::string, 2> coordinates;
            if (pointForm) {
                const ExpressionResult point = generateValue(*expression.arguments[0], Type("point"));
                coordinates = {point.components[0], point.components[1]};
                offset = 1;
            } else {
                const ExpressionResult x = generateExpression(*expression.arguments[0]);
                const ExpressionResult y = generateExpression(*expression.arguments[1]);
                coordinates = {x.operand, y.operand};
                offset = 2;
            }
            const ExpressionResult block = generateValue(*expression.arguments[offset], TypeKind::Block);
            const ExpressionResult rotation = generateExpression(*expression.arguments[offset + 1]);
            std::string config = "0";
            if (expression.arguments.size() > offset + 2) {
                config = generateExpression(*expression.arguments[offset + 2]).operand;
            }
            emitUnitControl(receiver, "build",
                            {coordinates[0], coordinates[1], block.operand, rotation.operand, config});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "get_block") {
            std::size_t offset = 0;
            const auto coordinates = generateUnitCoordinates(expression, 5, 4, offset);
            const std::array<Type, 3> types = {TypeKind::Block, TypeKind::Building, TypeKind::Block};
            std::array<ExpressionResult, 3> destinations;
            std::vector<std::string> outputs;
            for (std::size_t index = 0; index < destinations.size(); ++index) {
                destinations[index] = generateExpression(*expression.arguments[offset + index]);
                if (destinations[index].type != types[index] || !destinations[index].lvalue) {
                    fail(expression.arguments[offset + index]->location,
                         "get_block 输出参数需要可赋值的 block、building、block 左值");
                }
                outputs.push_back(destinations[index].memoryLocation ? temporary()
                                                                      : destinations[index].operand);
            }
            emitUnitControl(receiver, "getBlock",
                            {coordinates[0], coordinates[1], outputs[0], outputs[1], outputs[2]});
            for (std::size_t index = 0; index < destinations.size(); ++index) {
                if (destinations[index].memoryLocation) {
                    storeMemory(destinations[index], {types[index], outputs[index], false});
                }
            }
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "get_block_type" || expression.text == "get_block_building" ||
            expression.text == "get_block_floor") {
            std::size_t offset = 0;
            const auto coordinates = generateUnitCoordinates(expression, 2, 1, offset);
            (void)offset;
            const std::string type = temporary();
            const std::string building = temporary();
            const std::string floor = temporary();
            emitUnitControl(receiver, "getBlock",
                            {coordinates[0], coordinates[1], type, building, floor});
            if (expression.text == "get_block_building") return {TypeKind::Building, building, false};
            return {TypeKind::Block, expression.text == "get_block_type" ? type : floor, false};
        }
        fail(expression.location, "未知的 unit 成员函数: " + expression.text);
    }

    ExpressionResult generateBuiltinMemberCall(const Expr& expression) {
        const ExpressionResult receiver = generateExpression(*expression.receiver);
        if (expression.text == "get") {
            if (!isSenseableReceiver(receiver.type)) {
                fail(expression.location, "get 的接收者不支持 sensor");
            }
            if (expression.arguments.size() != 1) {
                fail(expression.location, "get 需要一个 sensor 或内容常量参数");
            }
            const Expr& selectorExpression = *expression.arguments[0];
            const ExpressionResult selector = generateExpression(selectorExpression);
            Type resultType;
            if (selector.type == TypeKind::Sensor) {
                if (selectorExpression.kind != Expr::Kind::Variable || selectorExpression.text.empty() ||
                    selectorExpression.text.front() != '@' ||
                    !isSenseableSensorName(std::string_view(selectorExpression.text).substr(1))) {
                    fail(selectorExpression.location, "get 的 sensor 参数必须是可感知的内置 @ 常量");
                }
                resultType = *sensorResultType(std::string_view(selectorExpression.text).substr(1), receiver.type);
            } else if (selector.type == TypeKind::Item || selector.type == TypeKind::Liquid ||
                       selector.type == TypeKind::Block || selector.type == TypeKind::UnitKind) {
                resultType = TypeKind::Number;
            } else {
                fail(selectorExpression.location,
                     "get 参数必须是 sensor、item、liquid、block 或 unit_kind 常量");
            }
            const std::string result = temporary();
            emitter_.emit("sensor", {result, receiver.operand, selector.operand});
            return {resultType, result, false};
        }
        if (const std::optional<std::string_view> sensor = sensorAlias(expression.text)) {
            if (!isSenseableReceiver(receiver.type)) {
                fail(expression.location, expression.text + " 的接收者不支持 sensor");
            }
            if (!expression.arguments.empty()) {
                fail(expression.location, expression.text + " 不需要参数");
            }
            const std::string result = temporary();
            emitter_.emit("sensor", {result, receiver.operand, "@" + std::string(*sensor)});
            return {*sensorResultType(*sensor, receiver.type), result, false};
        }
        if (receiver.type == TypeKind::Unit) return generateUnitMemberCall(expression, receiver);
        if (expression.text == "radar" || builtinRadarMethod(expression.text)) {
            if (receiver.type != TypeKind::Building) {
                fail(expression.location, "Radar 的接收者必须是 building");
            }
            const std::optional<RadarMethod> method = builtinRadarMethod(expression.text);
            if (!method && expression.arguments.size() < 2) {
                fail(expression.location, "radar 需要 radar_sort 和 int order 参数");
            }
            const std::size_t filterCount = method
                ? expression.arguments.size()
                : expression.arguments.size() >= 2 ? expression.arguments.size() - 2 : 4;
            if (filterCount > 3) {
                fail(expression.location, expression.text + " 最多接受三个 Radar 筛选条件");
            }
            std::vector<std::string> filters;
            filters.reserve(3);
            for (std::size_t index = 0; index < filterCount; ++index) {
                const Expr& argument = *expression.arguments[index];
                const std::optional<RadarConstant> selector =
                    argument.kind == Expr::Kind::Variable
                        ? builtinRadarConstant(argument.text) : std::nullopt;
                if (!selector || selector->type != TypeKind::RadarFilter) {
                    fail(argument.location, "Radar 筛选条件必须是内置 radar_filter 常量");
                }
                filters.emplace_back(selector->operand);
            }
            while (filters.size() < 3) filters.emplace_back("any");

            std::string sort;
            std::string order;
            if (method) {
                sort = method->sort;
                order = std::to_string(method->order);
            } else {
                const Expr& sortExpression = *expression.arguments[filterCount];
                const std::optional<RadarConstant> selector =
                    sortExpression.kind == Expr::Kind::Variable
                        ? builtinRadarConstant(sortExpression.text) : std::nullopt;
                if (!selector || selector->type != TypeKind::RadarSort) {
                    fail(sortExpression.location, "Radar 排序必须是内置 radar_sort 常量");
                }
                sort = selector->operand;
                const ExpressionResult orderValue = generateExpression(*expression.arguments.back());
                if (orderValue.type != TypeKind::Int) {
                    fail(expression.arguments.back()->location, "Radar order 必须是 int");
                }
                order = orderValue.operand;
            }

            const std::string result = temporary();
            emitter_.emit("radar", {filters[0], filters[1], filters[2], sort,
                                    receiver.operand, order, result});
            return {TypeKind::Unit, result, false};
        }
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
        if (expression.text == "shoot") {
            if (expression.arguments.size() != 2) {
                fail(expression.location, "shoot 需要 point 和开火状态两个参数");
            }
            const ExpressionResult target = generateValue(*expression.arguments[0], Type("point"));
            const ExpressionResult enabled = toBoolean(generateExpression(*expression.arguments[1]),
                                                       expression.arguments[1]->location);
            emitter_.emit("control", {"shoot", receiver.operand, target.components[0],
                                      target.components[1], enabled.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "shootp") {
            if (expression.arguments.size() != 2) {
                fail(expression.location, "shootp 需要 posc 和开火状态两个参数");
            }
            const ExpressionResult target = generateValue(*expression.arguments[0], TypeKind::Posc);
            const ExpressionResult enabled = toBoolean(generateExpression(*expression.arguments[1]),
                                                       expression.arguments[1]->location);
            emitter_.emit("control", {"shootp", receiver.operand, target.operand, enabled.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "set_color") {
            if (expression.arguments.size() != 1) {
                fail(expression.location, "set_color 需要一个 packed_color 参数");
            }
            const ExpressionResult color = generateValue(*expression.arguments[0], TypeKind::PackedColor);
            emitter_.emit("control", {"color", receiver.operand, color.operand});
            return {TypeKind::Void, "", false};
        }
        if (const std::optional<Type> parameter = configValueMemberType(expression.text)) {
            if (expression.arguments.size() != 1) {
                fail(expression.location, expression.text + " 需要一个 " + typeName(*parameter) + " 参数");
            }
            const ExpressionResult value = generateValue(*expression.arguments[0], *parameter);
            emitter_.emit("control", {"config", receiver.operand, value.operand});
            return {TypeKind::Void, "", false};
        }
        if (isPayloadKindMember(expression.text)) {
            if (expression.arguments.size() != 1) {
                fail(expression.location, expression.text + " 需要一个 block 或 unit_kind 参数");
            }
            const ExpressionResult value = generateExpression(*expression.arguments[0]);
            if (value.type != TypeKind::Block && value.type != TypeKind::UnitKind) {
                fail(expression.arguments[0]->location,
                     expression.text + " 参数必须是 block 或 unit_kind");
            }
            emitter_.emit("control", {"config", receiver.operand, value.operand});
            return {TypeKind::Void, "", false};
        }
        if (isConfigClearMember(expression.text)) {
            if (!expression.arguments.empty()) {
                fail(expression.location, expression.text + " 不需要参数");
            }
            emitter_.emit("control", {"config", receiver.operand, "null"});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "copy_configuration_from") {
            if (expression.arguments.size() != 1) {
                fail(expression.location, "copy_configuration_from 需要一个 building 参数");
            }
            const ExpressionResult source = generateValue(*expression.arguments[0], TypeKind::Building);
            emitter_.emit("control", {"config", receiver.operand, source.operand});
            return {TypeKind::Void, "", false};
        }
        if (expression.text == "set_rotation") {
            if (expression.arguments.size() != 1) {
                fail(expression.location, "set_rotation 需要一个 int 参数");
            }
            const ExpressionResult rotation = generateValue(*expression.arguments[0], TypeKind::Int);
            emitter_.emit("control", {"config", receiver.operand, rotation.operand});
            return {TypeKind::Void, "", false};
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

    ExpressionResult generateStop(const Expr& expression) {
        if (!expression.arguments.empty()) fail(expression.location, expression.text + " 不需要参数");
        emitter_.emit("stop", {});
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

    ExpressionResult generateLookup(const Expr& expression, const LookupFunction& lookup) {
        if (expression.arguments.size() != 1) fail(expression.location, expression.text + " 需要一个参数");
        const ExpressionResult index = generateExpression(*expression.arguments.front());
        if (index.type != TypeKind::Int) {
            fail(expression.location, expression.text + " 参数必须是 int，实际为 " + typeName(index.type));
        }
        const std::string result = temporary();
        emitter_.emit("lookup", {std::string(lookup.content), result, index.operand});
        return {lookup.result, result, false};
    }

    const Program& program_;
    [[maybe_unused]] CompileOptions options_;
    IrBuilder emitter_;
    std::unordered_map<std::string, const StructDecl*> structs_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> memberFunctions_;
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
    const std::string mainInitEndLabel_ = "__main_init_end";
};

} // namespace

std::string compile(std::string_view source, const CompileOptions& options) {
    const std::string preprocessed = preprocess(source, options);
    Lexer lexer(preprocessed);
    Parser parser(lexer.scan());
    Program program = parser.parse();
    return Generator(program, options).generate();
}

} // namespace mdtc
