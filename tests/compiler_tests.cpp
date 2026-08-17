#include "mdtc/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct ObjectValue {
    std::string name;

    bool operator==(const ObjectValue&) const = default;
};

struct ColorValue {
    double red = 0;
    double green = 0;
    double blue = 0;
    double alpha = 0;

    bool operator==(const ColorValue&) const = default;
};

using Value = std::variant<std::monostate, double, std::string, ObjectValue, ColorValue>;

struct FlushEvent {
    std::string target;
    std::string text;
};

std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    bool quoted = false;
    bool escaped = false;
    for (char character : line) {
        if (quoted) {
            token.push_back(character);
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
        } else if (character == '"') {
            quoted = true;
            token.push_back(character);
        } else if (character == ' ' || character == '\t') {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(character);
        }
    }
    if (!token.empty()) tokens.push_back(std::move(token));
    return tokens;
}

std::string decodeString(const std::string& token) {
    std::string value;
    for (std::size_t index = 1; index + 1 < token.size(); ++index) {
        char character = token[index];
        if (character == '\\' && index + 1 < token.size() - 1) {
            const char escaped = token[++index];
            switch (escaped) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                default: value.push_back(escaped); break;
            }
        } else {
            value.push_back(character);
        }
    }
    return value;
}

class Simulator {
public:
    explicit Simulator(const std::string& assembly) {
        std::istringstream input(assembly);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty()) instructions_.push_back(splitTokens(line));
        }
    }

    void runUntilFlushes(std::size_t count, std::size_t instructionLimit = 10000) {
        for (std::size_t executed = 0; executed < instructionLimit && flushes_.size() < count; ++executed) {
            step();
        }
        if (flushes_.size() < count) throw std::runtime_error("模拟器未在限制内产生预期 printflush");
    }

    const std::vector<FlushEvent>& flushes() const { return flushes_; }
    const std::vector<std::string>& drawFlushes() const { return drawFlushes_; }
    const std::vector<std::vector<std::string>>& draws() const { return draws_; }

private:
    static bool isNumberToken(const std::string& token) {
        if (token.empty()) return false;
        char* end = nullptr;
        std::strtod(token.c_str(), &end);
        return end == token.c_str() + token.size();
    }

    Value read(const std::string& operand) const {
        if (operand == "true") return 1.0;
        if (operand == "false") return 0.0;
        if (operand == "null") return std::monostate{};
        if (!operand.empty() && operand.front() == '"') return decodeString(operand);
        if (isNumberToken(operand)) return std::strtod(operand.c_str(), nullptr);
        const auto iterator = variables_.find(operand);
        if (iterator != variables_.end()) return iterator->second;
        return ObjectValue{operand};
    }

    void write(const std::string& destination, Value value) {
        if (const double* number = std::get_if<double>(&value);
            number != nullptr && (!std::isfinite(*number))) {
            value = std::monostate{};
        }
        if (destination == "@counter") {
            counter_ = static_cast<std::size_t>(number(value));
        } else {
            variables_[destination] = std::move(value);
        }
    }

    static double number(const Value& value) {
        if (const double* result = std::get_if<double>(&value)) return *result;
        if (std::holds_alternative<std::monostate>(value)) return 0.0;
        return 1.0;
    }

    static bool boolean(const Value& value) {
        if (const double* result = std::get_if<double>(&value)) return std::abs(*result) >= 0.00001;
        return !std::holds_alternative<std::monostate>(value);
    }

    static bool equal(const Value& left, const Value& right) {
        if (left.index() == right.index()) return left == right;
        if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
            return std::abs(number(left) - number(right)) < 0.000001;
        }
        return false;
    }

    static std::string printable(const Value& value) {
        if (std::holds_alternative<std::monostate>(value)) return "null";
        if (const std::string* string = std::get_if<std::string>(&value)) return *string;
        if (const ObjectValue* object = std::get_if<ObjectValue>(&value)) return object->name;
        if (std::holds_alternative<ColorValue>(value)) return "<color>";
        const double numberValue = std::get<double>(value);
        if (std::abs(numberValue - std::round(numberValue)) < 0.00001) {
            return std::to_string(static_cast<long long>(std::llround(numberValue)));
        }
        std::ostringstream output;
        output << numberValue;
        return output.str();
    }

    bool condition(const std::string& operation, const Value& left, const Value& right) const {
        if (operation == "always") return true;
        if (operation == "equal") return equal(left, right);
        if (operation == "notEqual") return !equal(left, right);
        if (operation == "lessThan") return number(left) < number(right);
        if (operation == "lessThanEq") return number(left) <= number(right);
        if (operation == "greaterThan") return number(left) > number(right);
        if (operation == "greaterThanEq") return number(left) >= number(right);
        throw std::runtime_error("模拟器不支持条件: " + operation);
    }

    Value operation(const std::string& name, const Value& left, const Value& right) const {
        constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;
        constexpr double radiansToDegrees = 180.0 / 3.14159265358979323846;
        if (name == "add") return number(left) + number(right);
        if (name == "sub") return number(left) - number(right);
        if (name == "mul") return number(left) * number(right);
        if (name == "div") return number(left) / number(right);
        if (name == "idiv") return std::floor(number(left) / number(right));
        if (name == "mod") return std::fmod(number(left), number(right));
        if (name == "emod") return std::fmod(std::fmod(number(left), number(right)) + number(right), number(right));
        if (name == "pow") return std::pow(number(left), number(right));
        if (name == "round") return std::round(number(left));
        if (name == "equal") return equal(left, right) ? 1.0 : 0.0;
        if (name == "notEqual") return equal(left, right) ? 0.0 : 1.0;
        if (name == "strictEqual") return left.index() == right.index() && equal(left, right) ? 1.0 : 0.0;
        if (name == "lessThan") return number(left) < number(right) ? 1.0 : 0.0;
        if (name == "lessThanEq") return number(left) <= number(right) ? 1.0 : 0.0;
        if (name == "greaterThan") return number(left) > number(right) ? 1.0 : 0.0;
        if (name == "greaterThanEq") return number(left) >= number(right) ? 1.0 : 0.0;
        const auto integer = [&](const Value& value) { return static_cast<std::int64_t>(number(value)); };
        const unsigned shift = static_cast<unsigned>(integer(right)) & 63U;
        if (name == "shl") return static_cast<double>(static_cast<std::int64_t>(static_cast<std::uint64_t>(integer(left)) << shift));
        if (name == "shr") return static_cast<double>(integer(left) >> shift);
        if (name == "ushr") return static_cast<double>(static_cast<std::uint64_t>(integer(left)) >> shift);
        if (name == "or") return static_cast<double>(integer(left) | integer(right));
        if (name == "and") return static_cast<double>(integer(left) & integer(right));
        if (name == "xor") return static_cast<double>(integer(left) ^ integer(right));
        if (name == "not") return static_cast<double>(~integer(left));
        if (name == "max") return std::max(number(left), number(right));
        if (name == "min") return std::min(number(left), number(right));
        if (name == "angle") return std::atan2(number(right), number(left)) * radiansToDegrees;
        if (name == "angleDiff") {
            double difference = std::fmod(std::abs(number(left) - number(right)), 360.0);
            return std::min(difference, 360.0 - difference);
        }
        if (name == "len") return std::hypot(number(left), number(right));
        if (name == "noise") return std::sin(number(left) * 12.9898 + number(right) * 78.233);
        if (name == "abs") return std::abs(number(left));
        if (name == "sign") return number(left) > 0 ? 1.0 : number(left) < 0 ? -1.0 : 0.0;
        if (name == "log") return std::log(number(left));
        if (name == "logn") return std::log(number(left)) / std::log(number(right));
        if (name == "log10") return std::log10(number(left));
        if (name == "floor") return std::floor(number(left));
        if (name == "ceil") return std::ceil(number(left));
        if (name == "sqrt") return std::sqrt(number(left));
        if (name == "rand") return number(left) * 0.5;
        if (name == "sin") return std::sin(number(left) * degreesToRadians);
        if (name == "cos") return std::cos(number(left) * degreesToRadians);
        if (name == "tan") return std::tan(number(left) * degreesToRadians);
        if (name == "asin") return std::asin(number(left)) * radiansToDegrees;
        if (name == "acos") return std::acos(number(left)) * radiansToDegrees;
        if (name == "atan") return std::atan(number(left)) * radiansToDegrees;
        throw std::runtime_error("模拟器不支持 op: " + name);
    }

    void step() {
        if (counter_ >= instructions_.size()) counter_ = 0;
        const std::vector<std::string>& instruction = instructions_[counter_++];
        const std::string& opcode = instruction.at(0);
        if (opcode == "set") {
            write(instruction.at(1), read(instruction.at(2)));
        } else if (opcode == "op") {
            write(instruction.at(2), operation(instruction.at(1), read(instruction.at(3)), read(instruction.at(4))));
        } else if (opcode == "jump") {
            if (condition(instruction.at(2), read(instruction.at(3)), read(instruction.at(4)))) {
                counter_ = static_cast<std::size_t>(std::stoull(instruction.at(1)));
            }
        } else if (opcode == "getlink") {
            const auto index = static_cast<long long>(number(read(instruction.at(2))));
            write(instruction.at(1), ObjectValue{"link" + std::to_string(index)});
        } else if (opcode == "print") {
            textBuffer_ += printable(read(instruction.at(1)));
        } else if (opcode == "printchar") {
            textBuffer_.push_back(static_cast<char>(std::floor(number(read(instruction.at(1))))));
        } else if (opcode == "format") {
            std::size_t placeholder = std::string::npos;
            int placeholderNumber = 10;
            for (std::size_t index = 0; index + 2 < textBuffer_.size(); ++index) {
                const char digit = textBuffer_[index + 1];
                if (textBuffer_[index] == '{' && digit >= '0' && digit <= '9' &&
                    textBuffer_[index + 2] == '}' && digit - '0' < placeholderNumber) {
                    placeholderNumber = digit - '0';
                    placeholder = index;
                }
            }
            if (placeholder != std::string::npos) {
                textBuffer_.replace(placeholder, 3, printable(read(instruction.at(1))));
                if (textBuffer_.size() > 400) textBuffer_.resize(400);
            }
        } else if (opcode == "packcolor") {
            const auto component = [&](std::size_t index) {
                return std::clamp(number(read(instruction.at(index))), 0.0, 1.0);
            };
            write(instruction.at(1), ColorValue{component(2), component(3), component(4), component(5)});
        } else if (opcode == "unpackcolor") {
            const Value packed = read(instruction.at(5));
            const ColorValue color = std::get_if<ColorValue>(&packed) ? std::get<ColorValue>(packed) : ColorValue{};
            write(instruction.at(1), color.red);
            write(instruction.at(2), color.green);
            write(instruction.at(3), color.blue);
            write(instruction.at(4), color.alpha);
        } else if (opcode == "draw") {
            draws_.push_back(instruction);
            if (instruction.at(1) == "print") textBuffer_.clear();
        } else if (opcode == "drawflush") {
            drawFlushes_.push_back(printable(read(instruction.at(1))));
        } else if (opcode == "wait") {
        } else if (opcode == "printflush") {
            const Value targetValue = read(instruction.at(1));
            flushes_.push_back({printable(targetValue), textBuffer_});
            textBuffer_.clear();
        } else {
            throw std::runtime_error("模拟器不支持指令: " + opcode);
        }
    }

    std::vector<std::vector<std::string>> instructions_;
    std::unordered_map<std::string, Value> variables_;
    std::size_t counter_ = 0;
    std::string textBuffer_;
    std::vector<FlushEvent> flushes_;
    std::vector<std::string> drawFlushes_;
    std::vector<std::vector<std::string>> draws_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireCompileError(Function&& function, const std::string& expectedText) {
    try {
        function();
    } catch (const mdtc::CompileError& error) {
        require(std::string(error.what()).find(expectedText) != std::string::npos,
                "编译错误内容不符合预期: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("预期编译失败，但实际成功");
}

void testFunctionsControlFlowAndPrint() {
    const std::string source = R"(
int adjust(int value) {
    if (value > 2) {
        return value * 2;
    } else {
        return value + 1;
    }
}

void main_loop() {
    int total = 0;
    for (int index = 0; index < 5; index++) {
        if (index == 1) {
            continue;
        }
        if (index == 4) {
            break;
        }
        total += adjust(index);
    }
    print("total=");
    print(total);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).target == "message1", "printflush 目标错误");
    require(simulator.flushes().at(0).text == "total=10", "控制流或函数结果错误");
}

void testGlobalPersistsAcrossMainLoop() {
    const std::string source = R"(
extern message output;
int cycles = 0;

void main_loop() {
    cycles++;
    print(cycles);
    printflush(output);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(2);
    require(simulator.flushes().at(0).text == "1", "第一次 main_loop 的全局变量错误");
    require(simulator.flushes().at(1).text == "2", "全局变量没有跨 main_loop 保留");
}

void testWhileAndShortCircuit() {
    const std::string source = R"(
extern message output;

bool side_effect() {
    print("bad");
    return true;
}

void main_loop() {
    int value = 0;
    while (value < 3) {
        value += 1;
    }
    if (false && side_effect()) {
        print("unreachable");
    }
    print(value);
    printflush(output);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "3", "while 或短路求值错误");
}

void testBasicTypes() {
    const std::string source = R"(
extern message output;

void main_loop() {
    bool ready = 2.0 > 1.0;
    int count = 2;
    float scale = 1.5;
    number value = count * scale;
    string prefix = "value=";
    if (ready) {
        print(prefix);
        print(value);
        printflush(output);
    }
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "value=3", "基础类型或隐式数值提升错误");
}

void testGetLink() {
    const std::string source = R"(
extern message output;

void main_loop() {
    building target = getlink(2);
    print(target);
    printflush(output);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "link2", "getlink 返回的链接错误");
}

void testImplicitLinksAndPrintFlushIndex() {
    const std::string source = R"(
void main_loop() {
    print(message1);
    print(conveyor1);
    printflush(message1);
    print("second");
    printflush(2);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("printflush message1\n") != std::string::npos,
            "内置信息板链接没有生成裸链接名称");
    require(assembly.find("printflush message2\n") != std::string::npos,
            "整型字面量没有转换为信息板链接名称");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(2);
    require(simulator.flushes().at(0).target == "message1", "字符串 printflush 目标错误");
    require(simulator.flushes().at(1).target == "message2", "整型 printflush 目标错误");
}

void testPrintCharFormatAndPrintf() {
    const std::string source = R"(
void main_loop() {
    printchar(65);
    putchar(66);
    printf(" value={0}/{1}", 3, "ok");
    print(" raw={0}");
    format(7);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("printchar 65\n") != std::string::npos, "printchar 没有生成对应指令");
    require(assembly.find("format ") != std::string::npos, "printf 没有展开 format 指令");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "AB value=3/ok raw=7",
            "printchar、putchar、format 或 printf 结果错误");
}

void testDrawFlush() {
    const std::string source = R"(
void main_loop() {
    drawflush(display1);
    drawflush(2);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("drawflush display1\n") != std::string::npos,
            "内置显示屏链接没有生成 drawflush");
    require(assembly.find("drawflush display2\n") != std::string::npos,
            "整型字面量没有转换为显示屏链接名称");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.drawFlushes() == std::vector<std::string>{"display1", "display2"},
            "drawflush 目标或顺序错误");
}

void testDrawColorAndStrokeAliases() {
    const std::string source = R"(
void main_loop() {
    draw_color(1, 2, 3, 0);
    set_color(4, 5, 6, 7);
    draw_color(rgba(8, 9, 10, 0));
    draw_stroke(2);
    set_stroke(3);
    drawflush(display1);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.draws().size() == 5, "颜色或线宽绘图指令数量错误");
    require(simulator.draws().at(0).at(1) == "color", "draw_color 子命令错误");
    require(simulator.draws().at(0).at(5) != "0", "透明 alpha 被汇编器错误改写的规避失效");
    require(simulator.draws().at(1).at(1) == "color", "set_color 子命令错误");
    require(simulator.draws().at(2).at(1) == "color" && simulator.draws().at(2).at(5) != "0",
            "color 的透明 alpha 被汇编器错误改写的规避失效");
    require(simulator.draws().at(3).at(1) == "stroke" &&
            simulator.draws().at(4).at(1) == "stroke", "线宽别名子命令错误");
}

void testColorAndDrawCommands() {
    const std::string source = R"(
color keep_color(color value) {
    return value;
}

void main_loop() {
    color red = rgb(255, 0, 0);
    color translucent = keep_color(rgba(10, 20, 30, 128));
    translucent.a = 64;
    packed_color packed = pack_color(translucent);
    color unpacked = unpack_color(packed);
    packed_color opaque_packed = pack_color(1, 2, 3);
    packed_color alpha_packed = pack_color(4, 5, 6, 7);
    color opaque = unpack_color(opaque_packed);
    color alpha = unpack_color(alpha_packed);
    point first = {1, 2};
    point second = {8, 10};
    point third = {4, 12};
    vec size = {7, 8};
    vec scale = {1.5, 0.5};
    rect bounds = {first, second};

    draw_clear(red);
    draw_color(translucent);
    set_color(red);
    draw_col(packed);
    set_packed_color(alpha_packed);
    draw_stroke(2);
    draw_line(point{1, 2}, point{8, 10});
    draw_rect(rect{{1, 2}, {8, 10}});
    draw_line_rect(first, size);
    draw_poly(first, 6, 4, 15);
    draw_line_poly(second, 5, 3, 0);
    draw_triangle(point{1, 2}, point{8, 10}, point{4, 12});
    draw_image(point{1, 2}, display2, 16, 45);
    print("drawn");
    draw_print(first, 0);
    draw_translate(size);
    draw_scale(scale);
    draw_rotate(90);
    draw_reset();
    drawflush(display1);
    print(opaque.a);
    print(",");
    print(alpha.a);
    print(":");
    print(point{9, 10}.x);
    print(":");
    print(sizeof(color));
    print(sizeof(packed_color));
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("packcolor ") != std::string::npos, "pack_color 没有生成 packcolor");
    require(assembly.find("unpackcolor ") != std::string::npos, "unpack_color 没有生成 unpackcolor");

    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    const std::vector<std::string> expected = {
        "clear", "color", "color", "col", "col", "stroke", "line", "rect", "lineRect",
        "poly", "linePoly", "triangle", "image", "print", "translate", "scale", "rotate", "reset",
    };
    require(simulator.draws().size() == expected.size(), "绘图子命令数量错误");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(simulator.draws()[index][1] == expected[index], "绘图子命令顺序或名称错误");
    }
    require(simulator.flushes().at(0).text == "255,7:9:41",
            "类型化初始化、颜色打包或 draw_print 清缓冲语义错误");
}

void testEmptyStatementLoopBody() {
    const std::string source = R"(
int completed = 0;

void wait_cycles(int count) {
    for (int index = 0; index < count; index += 3);
    completed += 1;
}

void main_loop() {
    wait_cycles(6);
    print(completed);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "1", "空语句循环体编译错误");
}

void testStructsInitializersFunctionsAndSizeof() {
    const std::string source = R"(
struct pair {
    int first;
    int second;
};

struct box {
    pair position;
    pair size;
};

pair swapped(pair value) {
    return {value.second, value.first};
}

int side_effect() {
    print("bad");
    return 0;
}

box global_box = {{1, 2}, {3, 4}};

void main_loop() {
    pair current = {5};
    current = {current.second, current.first};
    box area = global_box;
    area.position = current;
    pair result = swapped(pair{7, 8});
    print(sizeof(int));
    print(sizeof(pair));
    print(sizeof(box));
    print(sizeof(global_box));
    print(sizeof(side_effect()));
    print(sizeof(pair{1, 2}));
    print(int{6});
    print(":");
    print(result.first);
    print(",");
    print(result.second);
    print(":");
    print(area.position.first);
    print(",");
    print(area.position.second);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "1244126:8,7:0,5",
            "结构体初始化、字段、函数或 sizeof 语义错误");
}

void testSizeofBuiltinTypes() {
    const std::string source = R"(
void main_loop() {
    print(sizeof(bool));
    print(sizeof(int));
    print(sizeof(float));
    print(sizeof(number));
    print(sizeof(string));
    print(sizeof(message));
    print(sizeof(building));
    print(sizeof(display));
    print(sizeof(getlink(0)));
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("getlink") == std::string::npos, "sizeof 错误地求值了操作数");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "111111111", "内置类型 sizeof 不全为 1");
}

void testBuiltinPointVectorAndRect() {
    const std::string source = R"(
void main_loop() {
    point position = {1, 2};
    vec offset = {3, 4};
    point moved = position + offset;
    point reverse_moved = offset + position;
    vec difference = moved - position;
    offset += difference;
    offset *= 2;
    double scale = 0.5;
    offset = offset * scale;
    position += offset;
    position -= difference;
    vec negative = -difference;
    vec scaled = 2 * difference;
    rect bounds = {{1, 2}, {9, 10}};

    print(position.x); print(","); print(position.y); print(":");
    print(reverse_moved.x); print(","); print(reverse_moved.y); print(":");
    print(offset.x); print(","); print(offset.y); print(":");
    print(negative.x); print(","); print(negative.y); print(":");
    print(scaled.x); print(","); print(scaled.y); print(":");
    print(bounds.min.x); print(","); print(bounds.min.y); print(",");
    print(bounds.max.x); print(","); print(bounds.max.y); print(":");
    print(sizeof(point)); print(sizeof(vec)); print(sizeof(rect));
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "4,6:4,6:6,8:-3,-4:6,8:1,2,9,10:224",
            "point、vec 或 rect 内置语义错误");
}

void testVectorDotAndCross() {
    const std::string source = R"(
void main_loop() {
    vec first = {2, 3};
    vec second = {4, 5};
    print(dot(first, second));
    print(",");
    print(cross(first, second));
    print(":");
    print(sizeof(dot(first, second)));
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "23,-2:1", "vec 点积或叉积语义错误");
}

void testOpBuiltinFunctions() {
    const std::string source = R"(
void main_loop() {
    number noise_value = noise(point{1, 2});
    number random_value = rand(10);
    wait(0.01);
    print(idiv(7, 2)); print(",");
    print(mod(5.5, 2)); print(",");
    print(emod(-1, 5)); print(",");
    print(pow(2, 3)); print(",");
    print(strict_equal(2, 2)); print(",");
    print(shl(3, 2)); print(",");
    print(shr(8, 2)); print(",");
    print(ushr(8, 2)); print(",");
    print(bit_or(5, 2)); print(",");
    print(bit_and(5, 3)); print(",");
    print(bit_xor(5, 3)); print(",");
    print(bit_not(0)); print(",");
    print(max(9, 4)); print(",");
    print(min(9, 2)); print(",");
    print(angle(vec{0, 1})); print(",");
    print(angle_diff(350, 10)); print(",");
    print(len(vec{3, 4})); print(",");
    print(abs(-3)); print(",");
    print(sign(-3)); print(",");
    print(log(1)); print(",");
    print(logn(8, 2)); print(",");
    print(log10(100)); print(",");
    print(floor(2.9)); print(",");
    print(ceil(2.1)); print(",");
    print(round(2.6)); print(",");
    print(sqrt(9)); print(",");
    print(sin(30)); print(",");
    print(cos(60)); print(",");
    print(tan(45)); print(",");
    print(asin(1)); print(",");
    print(acos(1)); print(",");
    print(atan(1));
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("wait 0.01\n") != std::string::npos, "wait 没有生成原生指令");
    const std::vector<std::string> operations = {
        "idiv", "mod", "emod", "pow", "strictEqual", "shl", "shr", "ushr", "or", "and", "xor", "not",
        "max", "min", "angle", "angleDiff", "len", "noise", "abs", "sign", "log", "logn", "log10",
        "floor", "ceil", "round", "sqrt", "rand", "sin", "cos", "tan", "asin", "acos", "atan",
    };
    for (const std::string& operation : operations) {
        require(assembly.find("op " + operation + " ") != std::string::npos,
                "缺少 op 内置函数降低: " + operation);
    }

    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text ==
            "3,1.5,4,8,1,12,2,2,7,1,6,-1,9,2,90,20,5,3,-1,0,3,2,2,3,3,3,0.5,0.5,1,90,0,45",
            "op 内置函数运行语义错误");
}

void testDiagnostics() {
    requireCompileError([] {
        (void)mdtc::compile("void recurse() { recurse(); } void main_loop() {}");
    }, "递归调用");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int value = 1; printflush(value); }");
    }, "必须是 message 或正整数字面量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { printflush(\"message1\"); }");
    }, "不接受字符串字面量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int message1 = 0; }");
    }, "链接标识符不能被声明");

    requireCompileError([] {
        (void)mdtc::compile("void conveyor1() {} void main_loop() {}");
    }, "链接标识符不能被声明");

    requireCompileError([] {
        (void)mdtc::compile("extern message message1; void main_loop() {}");
    }, "链接标识符不能被声明");

    requireCompileError([] {
        (void)mdtc::compile("int value = 1;");
    }, "main_loop");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { building value = getlink(1.5); }");
    }, "必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { printchar(65.0); }");
    }, "参数必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { printf(1, 2); }");
    }, "第一个参数必须是 string");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { drawflush(\"display1\"); }");
    }, "不接受字符串字面量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { building target = getlink(0); drawflush(target); }");
    }, "必须是 display 或正整数字面量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { draw_color(0, 0, 0, 1.0); }");
    }, "参数必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { draw_col(1); }");
    }, "必须是 packed_color");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { color value = rgb(1, 2, 3); packed_color bad = value; }");
    }, "不能把 color 赋值给 packed_color");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { draw_line(point{1, 2, 3}, point{}); }");
    }, "初始化项过多");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { packed_color value = pack_color(1, 2.0, 3); }");
    }, "参数必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int value = bit_and(1.0, 1); }");
    }, "参数必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { number value = angle(point{1, 2}); }");
    }, "单参数形式需要 vec");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { wait(point{1, 2}); }");
    }, "参数必须是数值类型");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { point p = {}; vec v = {}; draw_line(p, v); }");
    }, "需要两个 point");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { point p = {}; draw_image(p, message1, 1, 0); }");
    }, "只支持 display 图像源");

    requireCompileError([] {
        (void)mdtc::compile("struct node { node child; }; void main_loop() {}");
    }, "不能直接或间接包含自身");

    requireCompileError([] {
        (void)mdtc::compile("struct point { int value; }; void main_loop() {}");
    }, "重复的结构体名称");

    requireCompileError([] {
        (void)mdtc::compile("struct pair { int x; int y; }; void main_loop() { pair value = {1, 2, 3}; }");
    }, "初始化项过多");

    requireCompileError([] {
        (void)mdtc::compile("struct pair { int x; int y; }; void main_loop() { pair value = {}; print(value.z); }");
    }, "没有字段 z");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { print(sizeof(void)); }");
    }, "不能对 void 使用 sizeof");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { point a = {}; point b = {}; point c = a + b; }");
    }, "加法只支持");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { vec value = {}; point p = {}; vec bad = value - p; }");
    }, "减法只支持");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { point p = {}; vec v = {}; print(dot(p, v)); }");
    }, "参数必须都是 vec");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { vec v = {}; print(cross(v)); }");
    }, "需要两个参数");
}

} // namespace

int main() {
    try {
        testFunctionsControlFlowAndPrint();
        testGlobalPersistsAcrossMainLoop();
        testWhileAndShortCircuit();
        testBasicTypes();
        testGetLink();
        testImplicitLinksAndPrintFlushIndex();
        testPrintCharFormatAndPrintf();
        testDrawFlush();
        testDrawColorAndStrokeAliases();
        testColorAndDrawCommands();
        testEmptyStatementLoopBody();
        testStructsInitializersFunctionsAndSizeof();
        testSizeofBuiltinTypes();
        testBuiltinPointVectorAndRect();
        testVectorDotAndCross();
        testOpBuiltinFunctions();
        testDiagnostics();
    } catch (const std::exception& error) {
        std::cerr << "测试失败: " << error.what() << '\n';
        return 1;
    }
    std::cout << "全部测试通过\n";
    return 0;
}
