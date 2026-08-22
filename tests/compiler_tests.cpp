#include "mdtc/compiler.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        if ((operand.size() == 7 || operand.size() == 9) && operand.front() == '%') {
            const auto component = [&](std::size_t offset, double fallback) {
                unsigned value = 0;
                const auto [end, error] = std::from_chars(operand.data() + offset,
                                                           operand.data() + offset + 2, value, 16);
                return error == std::errc{} && end == operand.data() + offset + 2
                    ? static_cast<double>(value) / 255.0
                    : fallback;
            };
            return ColorValue{component(1, 0), component(3, 0), component(5, 0),
                              operand.size() == 9 ? component(7, 1) : 1};
        }
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
        if (operation == "strictEqual") return left.index() == right.index() && equal(left, right);
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
        } else if (opcode == "read") {
            const std::string handle = printable(read(instruction.at(2)));
            const auto address = static_cast<long long>(number(read(instruction.at(3))));
            const auto iterator = memory_.find(handle + ":" + std::to_string(address));
            write(instruction.at(1), iterator == memory_.end() ? Value{std::monostate{}} : iterator->second);
        } else if (opcode == "write") {
            const std::string handle = printable(read(instruction.at(2)));
            const auto address = static_cast<long long>(number(read(instruction.at(3))));
            memory_[handle + ":" + std::to_string(address)] = read(instruction.at(1));
        } else if (opcode == "op") {
            write(instruction.at(2), operation(instruction.at(1), read(instruction.at(3)), read(instruction.at(4))));
        } else if (opcode == "select") {
            const bool selected = condition(instruction.at(2), read(instruction.at(3)), read(instruction.at(4)));
            write(instruction.at(1), read(instruction.at(selected ? 5 : 6)));
        } else if (opcode == "jump") {
            if (condition(instruction.at(2), read(instruction.at(3)), read(instruction.at(4)))) {
                counter_ = static_cast<std::size_t>(std::stoull(instruction.at(1)));
            }
        } else if (opcode == "getlink") {
            const auto index = static_cast<long long>(number(read(instruction.at(2))));
            write(instruction.at(1), ObjectValue{"link" + std::to_string(index)});
        } else if (opcode == "lookup") {
            const auto index = static_cast<long long>(number(read(instruction.at(3))));
            write(instruction.at(2), ObjectValue{instruction.at(1) + "#" + std::to_string(index)});
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
    std::unordered_map<std::string, Value> memory_;
    std::size_t counter_ = 0;
    std::string textBuffer_;
    std::vector<FlushEvent> flushes_;
    std::vector<std::string> drawFlushes_;
    std::vector<std::vector<std::string>> draws_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t countOccurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t position = 0;
         (position = text.find(needle, position)) != std::string::npos;
         position += needle.size()) {
        ++count;
    }
    return count;
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

void testSwitchCaseAndJumpTables() {
    const std::string source = R"(
void main_loop() {
    int selector = 2;
    int value = 0;
    switch (selector) {
        case 0:
            value += 1;
        case 1:
            value += 10;
            break;
        case 2:
            value += 100;
        case 3:
            value += 1000;
            break;
        default:
            value = -1;
    }

    switch (99) {
        case 0:
            value = -100;
    }

    for (int index = 0; index < 4; index++) {
        switch (index) {
            case 1:
                continue;
            default:
                value += index;
        }
    }

    print(value);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("set @counter") != std::string::npos,
            "连续整数 case 应生成跳转表");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "1105",
            "switch 贯穿、break、continue 或无 default 语义错误，实际为 " +
                simulator.flushes().at(0).text);

    const std::string sparse = mdtc::compile(R"(
void main_loop() {
    int selector = 20;
    switch (selector) {
        case 0: print(0); break;
        case 100: print(1); break;
        case 200: print(2); break;
        case 300: print(3); break;
    }
    printflush(message1);
}
)");
    require(sparse.find("set @counter") == std::string::npos,
            "稀疏 switch 不应生成跳转表");

    const std::string denseWithHoles = mdtc::compile(R"(
void main_loop() {
    int value = 0;
    switch (-1) {
        case -2: value = 2; break;
        case -1: value = 1; break;
        case 0: value = 0; break;
        case +1: value = -1; break;
    }
    switch (3) {
        case 0: value = 100; break;
        case 2: value = 200; break;
        case 4: value = 400; break;
        case 6: value = 600; break;
        default: value += 10;
    }
    print(value);
    printflush(message1);
}
)");
    Simulator denseWithHolesSimulator(denseWithHoles);
    denseWithHolesSimulator.runUntilFlushes(1);
    require(denseWithHolesSimulator.flushes().at(0).text == "11",
            "负 case、正号 case 或跳转表空洞处理错误");

    const std::string returning = mdtc::compile(R"(
int classify(int value) {
    switch (value) {
        case 0:
        case 1:
            return 10;
        default:
            return 20;
    }
}
void main_loop() {
    print(classify(1));
    printflush(message1);
}
)");
    Simulator returningSimulator(returning);
    returningSimulator.runUntilFlushes(1);
    require(returningSimulator.flushes().at(0).text == "10",
            "switch 的完整返回分析错误");

    requireCompileError([] {
        (void)mdtc::compile(
            "int f(int x) { switch (x) { case 0: return 1; } } "
            "void main_loop() { print(f(1)); }");
    }, "并非所有路径都返回值");
    requireCompileError([] {
        (void)mdtc::compile(
            "void main_loop() { switch (0) { case 1: break; case 1: break; } }");
    }, "重复的 case 值");
    requireCompileError([] {
        (void)mdtc::compile(
            "void main_loop() { switch (0) { default: break; default: break; } }");
    }, "只能有一个 default");
    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { switch (1.5) { case 1: break; } }");
    }, "switch 条件必须是 int");
    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { switch (1) { case 1.5: break; } }");
    }, "case 值必须是整数常量");
}

void testAutomaticInlining() {
    const std::string profitableSource = R"(
int state = -1;

int classify(int value) {
    if (value < 0) {
        return 7;
    }
    return 9;
}

void main_loop() {
    print(classify(state));
    printflush(message1);
    state = 1;
}
)";

    const std::string profitableAssembly = mdtc::compile(profitableSource);
    require(profitableAssembly.find("__function_classify_return_address") == std::string::npos,
            "可缩短代码的函数没有被内联");
    Simulator profitableSimulator(profitableAssembly);
    profitableSimulator.runUntilFlushes(2);
    require(profitableSimulator.flushes().at(0).text == "7", "内联函数第一个返回路径错误");
    require(profitableSimulator.flushes().at(1).text == "9", "内联函数第二个返回路径错误");

    const std::string unprofitableSource = R"(
void verbose() {
    print("a");
    print("b");
    print("c");
    print("d");
    print("e");
}

void main_loop() {
    verbose();
    verbose();
    printflush(message1);
}
)";

    const std::string unprofitableAssembly = mdtc::compile(unprofitableSource);
    require(unprofitableAssembly.find("__function_verbose_return_address") != std::string::npos,
            "不能缩短代码的函数不应被内联");
    Simulator unprofitableSimulator(unprofitableAssembly);
    unprofitableSimulator.runUntilFlushes(1);
    require(unprofitableSimulator.flushes().at(0).text == "abcdeabcde",
            "保留普通调用后函数语义错误");
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

void testMainInitRunsOnceBeforeMainLoop() {
    const std::string source = R"(
int value = 2;
int initialization_calls = 0;

void add_initial_value() {
    value += 3;
}

void main_init() {
    initialization_calls++;
    add_initial_value();
    value *= 2;
    if (value == 10) return;
    value = 100;
}

void main_loop() {
    print(initialization_calls);
    print(":");
    print(value);
    printflush(message1);
    value++;
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(2);
    require(simulator.flushes().at(0).text == "1:10",
            "main_init 没有在全局初始化后、main_loop 前执行");
    require(simulator.flushes().at(1).text == "1:11",
            "main_init 被重复执行或 main_loop 状态没有保留");

    requireCompileError([] {
        (void)mdtc::compile(
            "void main_init(int value) {} void main_loop() {}");
    }, "void main_init()");
    requireCompileError([] {
        (void)mdtc::compile(
            "void main_init() {} void invoke() { main_init(); } void main_loop() { invoke(); }");
    }, "不能显式调用 main_init");
    requireCompileError([] {
        (void)mdtc::compile(
            "void main_init() { return 1; } void main_loop() {}");
    }, "main_init 不能返回值");
}

void testGlobalInitializerFunctionReachability() {
    const std::string source = R"(
int initialize() {
    return 7;
}

int value = initialize();

void main_loop() {
    print(value);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("__function_initialize_return_address") == std::string::npos,
            "全局初始化阶段的函数调用没有参与内联");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "7", "全局初始化调用的函数被错误删除");
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

void testSafeComparisonJumpFusion() {
    const std::string falseJumpSource = R"(
void main_loop() {
    int left = 1;
    int right = 2;
    if (left != right) {
        print("if");
    }
    while (left != right) {
        left += 1;
    }
    if (left) {
        print(" numeric");
    }
    printflush(message1);
}
)";
    const std::string falseJumpAssembly = mdtc::compile(falseJumpSource);
    require(falseJumpAssembly.find("op notEqual ") == std::string::npos,
            "安全的 != 条件没有与 jump 融合");
    require(falseJumpAssembly.find("jump ") != std::string::npos &&
                falseJumpAssembly.find(" equal ") != std::string::npos,
            "融合后的条件跳转缺失");
    Simulator falseJumpSimulator(falseJumpAssembly);
    falseJumpSimulator.runUntilFlushes(1);
    require(falseJumpSimulator.flushes().at(0).text == "if numeric",
            "安全条件跳转融合改变了控制流语义");

    const std::string trueJumpSource = R"(
void main_loop() {
    number left = 1;
    number right = 2;
    if (left < right) {
        print("then");
    } else {
        print("else");
    }
    printflush(message1);
}
)";
    const std::string trueJumpAssembly = mdtc::compile(trueJumpSource);
    require(trueJumpAssembly.find("op lessThan ") == std::string::npos,
            "带 else 的比较没有使用原谓词直接跳转");
    require(trueJumpAssembly.find(" lessThan ") != std::string::npos,
            "融合后的原谓词跳转缺失");
    Simulator trueJumpSimulator(trueJumpAssembly);
    trueJumpSimulator.runUntilFlushes(1);
    require(trueJumpSimulator.flushes().at(0).text == "then",
            "交换分支布局后 if/else 语义错误");

    const std::string unsafeInverseAssembly = mdtc::compile(R"(
void main_loop() {
    number left = 1;
    number right = 2;
    if (left < right) {
        print("then");
    }
    printflush(message1);
}
)");
    require(unsafeInverseAssembly.find("op lessThan ") != std::string::npos,
            "可能包含 NaN 的大小比较被不安全地反向融合");
}

void testLoopCarriedValueAcrossEmptyBlocks() {
    const std::string source = R"(
int count_values() {
    int count = 0;
    for (int index = 0; index < 4; index++) {
        if (index < 3) {
            count += 1;
        }
    }
    return count;
}

void main_loop() {
    print(count_values());
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "3",
            "空基本块导致跨循环变量更新被错误删除");
}

void testValueAfterFunctionCallRemainsLive() {
    const std::string source = R"(
bool initialized = false;

void initialize() {
    print("init");
}

void main_loop() {
    if (!initialized) {
        initialize();
        initialized = true;
    }
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(2);
    require(simulator.flushes().at(0).text == "init", "初始化函数没有执行");
    require(simulator.flushes().at(1).text.empty(),
            "函数调用后的赋值被错误删除，初始化函数重复执行");
}

void testCalleeStateUpdatesRemainLive() {
    const std::string source = R"(
int total = 0;

void accumulate(int value) {
    print("");
    print("");
    print("");
    print("");
    print("");
    if (value != 0) {
        total += value;
    }
}

void main_loop() {
    accumulate(1);
    accumulate(2);
    accumulate(3);
    accumulate(4);
    print(total);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("__function_accumulate_return_address") != std::string::npos,
            "跨函数活跃性测试需要保留普通函数调用");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "10", "被调函数中的状态更新被错误删除");
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

void testUtf8SourceIdentifiersStringsAndComments() {
    const std::string source = std::string("\xEF\xBB\xBF") + R"(
/* UTF-8 块注释：搬运计划 🚚 */
int 求和(int 左值, int 右值) {
    return 左值 + 右值;
}

void main_loop() {
    // UTF-8 行注释不会生成指令
    int　结果 = 求和(20, 22);
    string 提示 = "搬运完成：你好，世界 🌍";
    print(提示);
    print(" = ");
    print(结果);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "搬运完成：你好，世界 🌍 = 42",
            "UTF-8 标识符、字符串或注释处理错误");

    requireCompileError([] {
        std::string invalid = "void main_loop() { int ";
        invalid.push_back(static_cast<char>(0xC3));
        invalid += "( = 0; }";
        (void)mdtc::compile(invalid);
    }, "非法 UTF-8 编码");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { /* 未结束的 UTF-8 注释");
    }, "未结束的块注释");
}

void testPreprocessorDefinesAndIncludes() {
    namespace fs = std::filesystem;
    struct TemporaryDirectory {
        fs::path path;
        ~TemporaryDirectory() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } temporary{fs::temp_directory_path() / "mdtc_preprocessor_tests"};

    std::error_code error;
    fs::remove_all(temporary.path, error);
    fs::create_directories(temporary.path / "relative", error);
    require(!error, "无法创建预处理器测试目录");
    fs::create_directories(temporary.path / "includes", error);
    require(!error, "无法创建预处理器包含目录");

    {
        std::ofstream nested(temporary.path / "nested.mdtc", std::ios::binary);
        nested << "#define NESTED_VALUE 30\n";
        std::ofstream relative(temporary.path / "relative/values.mdtc", std::ios::binary);
        relative << "#include \"../nested.mdtc\"\n#define RELATIVE_VALUE NESTED_VALUE\n";
        std::ofstream searched(temporary.path / "includes/searched.mdtc", std::ios::binary);
        searched << "#define SEARCH_VALUE 12 // 搜索路径中的常量\n";
    }

    mdtc::CompileOptions options;
    options.sourcePath = (temporary.path / "main.mdtc").string();
    options.includePaths.push_back((temporary.path / "includes").string());
    const std::string source = R"(
#include "relative/values.mdtc"
#include <searched.mdtc>
#define FIRST SECOND
#define SECOND RELATIVE_VALUE
#define 中文宏 SEARCH_VALUE

void main_loop() {
    // FIRST 和中文宏在注释中不能展开。
    print("结果=");
    print(FIRST + 中文宏);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source, options));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "结果=42",
            "对象宏、递归相对包含或 -I 搜索包含错误");

    requireCompileError([] {
        (void)mdtc::compile("#define CALL(value) value\nvoid main_loop() {}");
    }, "暂不支持带参数的宏");

    requireCompileError([&] {
        mdtc::CompileOptions missingOptions;
        missingOptions.sourcePath = (temporary.path / "missing-main.mdtc").string();
        (void)mdtc::compile("#include \"missing.mdtc\"\nvoid main_loop() {}", missingOptions);
    }, "找不到包含文件");
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

void testContentConstantsAndLookup() {
    const std::string source = R"(
void main_loop() {
    item resource = @copper;
    liquid fluid = @neoplasm;
    block environment = @stone;
    unit_kind produced = @dagger;
    team owner = @sharded;
    bool same = resource == @copper;
    block found_block = lookup_block(0);
    unit_kind found_unit = lookup_unit(1);
    item found_item = lookup_item(2);
    liquid found_liquid = lookup_liquid(3);
    team found_team = lookup_team(4);
    print(resource); print(fluid); print(environment); print(produced); print(owner); print(same);
    print(found_block); print(found_unit); print(found_item); print(found_liquid); print(found_team);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("@copper") != std::string::npos &&
                assembly.find("@neoplasm") != std::string::npos &&
                assembly.find("@stone") != std::string::npos &&
                assembly.find("@dagger") != std::string::npos &&
                assembly.find("@sharded") != std::string::npos,
            "内容常量没有保留为原生 @ 名称");
    require(assembly.find("lookup block ") != std::string::npos,
            "lookup_block 没有生成 block lookup");
    require(assembly.find("lookup unit ") != std::string::npos,
            "lookup_unit 没有生成 unit lookup");
    require(assembly.find("lookup item ") != std::string::npos,
            "lookup_item 没有生成 item lookup");
    require(assembly.find("lookup liquid ") != std::string::npos,
            "lookup_liquid 没有生成 liquid lookup");
    require(assembly.find("lookup team ") != std::string::npos,
            "lookup_team 没有生成 team lookup");

    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text.find("block#0unit#1item#2liquid#3team#4") != std::string::npos,
            "类型化 lookup 的模拟执行错误");
}

void testBuiltinBuildingMemberMethods() {
    const std::string source = R"(
extern posc enemy;

void main_loop() {
    bool enabled = switch1.get_enabled();
    switch1.enable(false);
    switch2.enable(enabled);
    point target = {80, 40};
    posc tracked = turret2;
    turret1.shoot(target, true);
    turret1.shootp(tracked, false);
    turret2.shootp(conveyor1, true);
    turret3.shootp(enemy, true);
    turret4.shootp(cell1, false);
    illuminator1.set_color(pack_color(255, 0, 0));
}

)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("sensor ") != std::string::npos,
            "get_enabled 没有生成 sensor 指令");
    require(assembly.find("sensor ") != std::string::npos &&
                assembly.find(" @enabled\n") != std::string::npos,
            "get_enabled 没有读取 enabled 属性");
    require(assembly.find("control enabled switch1 false\n") != std::string::npos,
            "enable(false) 没有生成 control 指令");
    require(assembly.find("control enabled switch2 ") != std::string::npos,
            "enable(bool) 没有生成 control 指令");
    require(assembly.find("control shoot turret1 80 40 true\n") != std::string::npos,
            "shoot(point, bool) 没有生成 control shoot 指令");
    require(assembly.find("control shootp turret1 ") != std::string::npos,
            "shootp(posc, bool) 没有生成 control shootp 指令");
    require(assembly.find("control shootp turret2 conveyor1 true\n") != std::string::npos,
            "shootp 没有接受隐式转换为 posc 的 building");
    require(assembly.find("control shootp turret3 enemy true\n") != std::string::npos,
            "shootp 没有接受 extern posc");
    require(assembly.find("control shootp turret4 cell1 false\n") != std::string::npos,
            "shootp 没有接受具备 Posc 能力的专用建筑句柄");
    require(assembly.find("control color illuminator1 %ff0000ff\n") != std::string::npos,
            "set_color(packed_color) 没有生成 control color 指令");
}

void testUnitBindAndControlMemberMethods() {
    const std::string source = R"(
void main_loop() {
    unit worker = unit_bind(@poly);
    worker.idle();
    worker.stop();
    worker.move(10, 20);
    worker.move(point{30, 40});
    worker.approach(point{50, 60}, 4);
    worker.pathfind(70, 80);
    worker.auto_pathfind();
    worker.boost(true);
    worker.target(point{90, 100}, true);
    worker.targetp(worker, false);
    worker.item_drop(container1, 10);
    worker.discard_items(3);
    worker.item_take(vault1, @copper, 20);
    worker.payload_drop();
    worker.payload_take(true);
    worker.payload_enter();
    worker.mine(point{11, 12});
    worker.set_flag(7);
    worker.build(point{13, 14}, @router, build_up);
    worker.build(15, 16, @conveyor, 2, @copper);
    worker.deconstruct(point{17, 18});
    block type = null;
    building building_at = null;
    block floor = null;
    worker.get_block(point{19, 20}, type, building_at, floor);
    type = worker.get_block_type(21, 22);
    building_at = worker.get_block_building(point{23, 24});
    floor = worker.get_block_floor(25, 26);
    bool close = worker.within(point{27, 28}, 5);
    print(close);
    unit rebound = unit_bind(worker);
    rebound.stop();
    worker.unbind();
    worker.idle();
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("ubindunit") == std::string::npos,
            "内部 ubindunit 操作没有降低为 ubind");
    require(countOccurrences(assembly, "ubind ") == 2,
            "刚绑定或连续控制同一 unit 时没有消除重复 ubind");
    require(assembly.find("ubind @poly\n") != std::string::npos,
            "unit_bind(unit_kind) 没有生成 ubind");
    require(assembly.find("ucontrol move 10 20 0 0 0\n") != std::string::npos &&
                assembly.find("ucontrol move 30 40 0 0 0\n") != std::string::npos,
            "move 的标量或 point 重载错误");
    require(assembly.find("ucontrol itemDrop container1 10 0 0 0\n") != std::string::npos &&
                assembly.find("ucontrol itemDrop @air 3 0 0 0\n") != std::string::npos,
            "item_drop 或 discard_items 封装错误");
    require(assembly.find("ucontrol itemTake vault1 @copper 20 0 0\n") != std::string::npos,
            "item_take 封装错误");
    require(assembly.find("ucontrol build 13 14 @router 1 0\n") != std::string::npos &&
                assembly.find("ucontrol build 15 16 @conveyor 2 @copper\n") != std::string::npos,
            "build 重载、方向常量或配置参数错误");
    require(countOccurrences(assembly, "ucontrol getBlock ") == 4,
            "get_block 原版或拆分接口没有生成 getBlock");
    require(assembly.find("ucontrol within 27 28 5 ") != std::string::npos,
            "within 没有使用返回值输出槽");
    require(assembly.find("ucontrol unbind 0 0 0 0 0\nubind @unit\n") != std::string::npos,
            "unbind 后错误删除了必要的重新绑定");
}

void testConfigMemberMethods() {
    const std::string source = R"(
extern building factory;
extern building item_source;
extern building liquid_source;
extern building sorter;
extern building unloader;
extern building landing_pad;
extern building payload_source;
extern building payload_router;
extern building constructor;
extern building other_factory;

void main_loop() {
    factory.set_production(@dagger);
    factory.clear_unit_command();
    item_source.set_output_item(@copper);
    item_source.clear_output_item();
    liquid_source.set_output_liquid(@water);
    liquid_source.clear_output_liquid();
    sorter.set_sort_item(@lead);
    sorter.clear_sort_item();
    unloader.set_unload_item(@titanium);
    unloader.clear_unload_item();
    landing_pad.set_delivery_item(@silicon);
    landing_pad.clear_delivery_item();
    payload_source.set_payload_kind(@router);
    payload_source.set_payload_kind(@dagger);
    payload_source.clear_payload_kind();
    payload_router.set_straight_payload(@router);
    payload_router.set_straight_payload(@dagger);
    payload_router.clear_straight_payload();
    payload_router.set_rotation(3);
    constructor.set_recipe(@beryllium-wall-large);
    constructor.clear_recipe();
    factory.copy_configuration_from(other_factory);
}
)";

    const std::string assembly = mdtc::compile(source);
    const std::vector<std::string> expected = {
        "control config factory @dagger\n",
        "control config factory null\n",
        "control config item_source @copper\n",
        "control config item_source null\n",
        "control config liquid_source @water\n",
        "control config liquid_source null\n",
        "control config sorter @lead\n",
        "control config sorter null\n",
        "control config unloader @titanium\n",
        "control config unloader null\n",
        "control config landing_pad @silicon\n",
        "control config landing_pad null\n",
        "control config payload_source @router\n",
        "control config payload_source @dagger\n",
        "control config payload_source null\n",
        "control config payload_router @router\n",
        "control config payload_router @dagger\n",
        "control config payload_router null\n",
        "control config payload_router 3\n",
        "control config constructor @beryllium-wall-large\n",
        "control config constructor null\n",
        "control config factory other_factory\n",
    };
    for (const std::string& instruction : expected) {
        require(assembly.find(instruction) != std::string::npos,
                "缺少专用 config lowering: " + instruction);
    }
}

void testSensorMemberMethods() {
    const std::vector<std::pair<std::string, std::string>> aliases = {
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

    std::string source = "extern building target; extern posc tracked; void main_loop() {";
    for (const auto& [method, sensor] : aliases) {
        (void)sensor;
        source += "print(target." + method + "());";
    }
    source += "number health = target.get(@health);";
    source += "bool enabled = target.get(@enabled);";
    source += "item first = target.get(@firstItem);";
    source += "packed_color sensed_color = target.get(@color);";
    source += "string target_name = target.get_name();";
    source += "block constructed = target.get_building();";
    source += "bool breaking = target.get_breaking();";
    source += "number copper = target.get(@copper);";
    source += "number water = target.get(@water);";
    source += "number payloads = target.get(@dagger);";
    source += "number blocks = target.get(@router);";
    source += "print(@router.get_id());";
    source += "print(message1.get_buffer_size());";
    source += "print(tracked.get_dead());";
    source += "building planned = tracked.get_building();";
    source += "sensor_value dynamic_value = target.get_config();";
    source += "print(dynamic_value.get_name());";
    source += "print(\"abc\".get_size());";
    source += "printflush(message1);}";

    const std::string assembly = mdtc::compile(source);
    for (const auto& [method, sensor] : aliases) {
        (void)method;
        require(assembly.find("sensor ") != std::string::npos &&
                    assembly.find(" target @" + sensor + "\n") != std::string::npos,
                "缺少 sensor 别名 lowering: " + sensor);
    }
    require(assembly.find(" target @copper\n") != std::string::npos &&
                assembly.find(" target @water\n") != std::string::npos &&
                assembly.find(" target @dagger\n") != std::string::npos &&
                assembly.find(" target @router\n") != std::string::npos,
            "内容常量没有作为 sensor 属性生成");
    require(assembly.find(" @router @id\n") != std::string::npos,
            "内容对象接收者没有生成 sensor");
    require(assembly.find(" message1 @bufferSize\n") != std::string::npos,
            "专用建筑句柄没有生成 sensor");
    require(assembly.find(" tracked @dead\n") != std::string::npos,
            "posc 接收者没有生成 sensor");
}

void testNullLiteralAndComparison() {
    const std::string source = R"(
void main_loop() {
    number missing = cell1[0];
    number zero = 0;
    sensor_value dynamic_value = null;
    building absent = null;
    print(missing == null); print(",");
    print(zero == null); print(",");
    print(missing != null); print(",");
    print(zero != null); print(",");
    print(dynamic_value == null); print(",");
    print(absent != null);
    if (missing != null) print(",bad");
    if (zero != null) print(",ok");
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("strictEqual") != std::string::npos,
            "null 比较没有使用 strictEqual");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(assembly.find(" strictEqual ") != std::string::npos,
            "null 条件没有融合为 strictEqual jump");
    require(simulator.flushes().at(0).text == "1,0,0,1,1,0,ok",
            "null 比较没有区分对象态 null 和数值零");
}

void testStopAndExit() {
    const std::string assembly = mdtc::compile(R"(
void halt() {
    exit();
}

void main_loop() {
    stop();
    halt();
}
)");
    std::size_t stopCount = 0;
    for (std::size_t position = assembly.find("stop\n"); position != std::string::npos;
         position = assembly.find("stop\n", position + 1)) {
        ++stopCount;
    }
    require(stopCount == 2, "stop() 和 exit() 没有都降低为 stop 指令");
}

void testRadarMemberMethods() {
    const std::string assembly = mdtc::compile(R"(
int order = 0;

void main_loop() {
    unit first = turret1.radar_nearest();
    posc second = turret2.radar_max_health(radar_enemy);
    posc third = turret3.radar_min_shield(radar_enemy, radar_flying);
    posc fourth = turret4.radar_max_armor(radar_enemy, radar_flying, radar_attacker);
    posc fifth = turret5.radar(radar_max_health, order);
    posc sixth = turret6.radar(radar_ground, radar_distance, order);
}
)");

    const std::vector<std::string> expected = {
        "radar any any any distance turret1 1 ",
        "radar enemy any any health turret2 1 ",
        "radar enemy flying any shield turret3 0 ",
        "radar enemy flying attacker armor turret4 1 ",
        "radar any any any maxHealth turret5 ",
        "radar ground any any distance turret6 ",
    };
    for (const std::string& instruction : expected) {
        require(assembly.find(instruction) != std::string::npos,
                "缺少 Radar member lowering: " + instruction);
    }
}

void testConditionalSelect() {
    const std::string source = R"(
int side_effects = 0;

int record(int value) {
    side_effects++;
    return value;
}

void main_loop() {
    int first = true ? 3 : 4;
    number mixed = false ? 1 : 2.5;
    building selected = true ? turret1 : null;
    point position = false ? point{1, 2} : point{3, 4};
    int nested = true ? (false ? 5 : 6) : 7;
    int compared = 2 < 3 ? 8 : 9;
    int numeric_condition = 2 ? 10 : 11;
    int pure_builtin = true ? abs(-12) : max(13, 14);
    int lazy_true = true ? record(20) : record(21);
    int lazy_false = false ? record(22) : record(23);
    int branch_value = 0;
    int assigned = false ? (branch_value = 24) : (branch_value = 25);
    int incremented = true ? branch_value++ : branch_value--;
    number lazy_random = false ? rand(100) : 26;
    print(first); print(","); print(mixed); print(",");
    print(selected == turret1); print(",");
    print(position.x); print(","); print(position.y); print(",");
    print(nested); print(","); print(compared); print(","); print(numeric_condition); print(",");
    print(pure_builtin);
    print(","); print(lazy_true); print(","); print(lazy_false); print(",");
    print(side_effects); print(","); print(assigned); print(",");
    print(incremented); print(","); print(branch_value); print(","); print(lazy_random);
    true ? print(",void-true") : print(",void-false");
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("select ") != std::string::npos,
            "三目运算符没有生成 select 指令");
    require(assembly.find(" lessThan 2 3 8 9\n") != std::string::npos,
            "三目比较条件没有融合进 select");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text ==
                "3,2.5,1,3,4,6,8,10,12,20,23,2,25,25,26,26,void-true",
            "三目 select 运行语义错误");
}

void testUserMemberFunctions() {
    const std::string source = R"(
struct counter {
    int value;

    void add(int amount) {
        this->value += amount;
    }

    int get() {
        return value;
    }

    int add_and_get(int amount) {
        this->add(amount);
        return get();
    }
};

struct wrapper {
    counter inner;

    void add_twice(int amount) {
        inner.add(amount);
        this->inner.add(amount);
    }
};

void main_loop() {
    counter local{1};
    local.add(2);
    int first = local.add_and_get(3);

    wrapper nested{{10}};
    nested.add_twice(4);

    arr<counter> values{cell1, 0};
    values[0] = counter{20};
    values[0].add(5);

    print(first); print(","); print(local.get()); print(",");
    print(nested.inner.get()); print(","); print(values[0].get());
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "6,6,18,25",
            "用户成员函数或 this-> 语义错误");
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

void testConstantPackedColorFolding() {
    const std::string source = R"(
int red = 9;

void main_loop() {
    packed_color first = pack_color(1, 2, 3);
    packed_color second = pack_color(4, 5, 6, 7);
    packed_color third = pack_color(rgb(8, 9, 10));
    packed_color fourth = pack_color(color{11, 12, 13, 14});
    packed_color clamped = pack_color(-1, 256, 15, 300);
    packed_color dynamic = pack_color(red, 10, 11);
    set_packed_color(third);
    set_packed_color(fourth);
    set_packed_color(clamped);
    set_packed_color(dynamic);
    print(unpack_color(first).a);
    print(",");
    print(unpack_color(second).a);
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("%010203ff") != std::string::npos, "三分量常量颜色没有折叠");
    require(assembly.find("%04050607") != std::string::npos, "四分量常量颜色没有折叠");
    require(assembly.find("%08090aff") != std::string::npos, "rgb 常量颜色没有折叠");
    require(assembly.find("%0b0c0d0e") != std::string::npos, "color 初始化常量没有折叠");
    require(assembly.find("%00ff0fff") != std::string::npos, "常量颜色没有按运行时规则截断");
    require(assembly.find("packcolor ") != std::string::npos, "运行期颜色被错误折叠");

    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "255,7", "颜色字面量模拟语义错误");
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
    print(sizeof(item));
    print(sizeof(liquid));
    print(sizeof(block));
    print(sizeof(unit_kind));
    print(sizeof(team));
    print(sizeof(getlink(0)));
    printflush(message1);
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("getlink") == std::string::npos, "sizeof 错误地求值了操作数");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "11111111111111", "内置类型 sizeof 不全为 1");
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
        "max", "min", "angle", "angleDiff", "len", "abs", "sign", "log", "logn", "log10",
        "floor", "ceil", "round", "sqrt", "sin", "cos", "tan", "asin", "acos", "atan",
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

void testMemoryArrays() {
    const std::string source = R"(
arr<int> values = {cell1, 3};
arr2d<int> matrix = {bank1, 10, 8};
void main_loop() {
    values[2] = 10;
    values[2] += 5;
    matrix[2][3] = 7;
    print(values[2]);
    print(",");
    print(matrix[2][3]);
    printflush(message1);
}
)";
    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "15,7", "memory 数组读写错误");

    const std::string aggregateSource = R"(
arr<point> points = {bank1, 100};
void main_loop() {
    points[2] = point{4, 5};
    print(points[2].x);
    print(",");
    print(points[2].y);
    printflush(message1);
}
)";
    Simulator aggregateSimulator(mdtc::compile(aggregateSource));
    aggregateSimulator.runUntilFlushes(1);
    require(aggregateSimulator.flushes().at(0).text == "4,5", "聚合类型数组读写错误");
}

void testReferenceParameters() {
    const std::string source = R"(
arr<int> values = {cell1, 0};
arr<point> points = {cell2, 0};
int selected = 0;

int update(int& value) {
    value += 3;
    return value * 2;
}

void shift(point& value) {
    value.x += 4;
    value.y += 5;
}

void swap(int& first, int& second) {
    int temporary = first;
    first = second;
    second = temporary;
}

void set_value(int& value, int unused) {
    value = 9;
}

int change_selection() {
    selected = 1;
    return 0;
}

void main_loop() {
    int direct = 4;
    int returned = update(direct);
    point local = {1, 2};
    shift(local);

    values[0] = 10;
    values[1] = 20;
    swap(values[0], values[1]);
    points[0] = point{7, 8};
    shift(points[0]);

    selected = 0;
    set_value(values[selected], change_selection());
    print(direct); print(","); print(returned); print(",");
    print(local.x); print(","); print(local.y); print(",");
    print(values[0]); print(","); print(values[1]); print(",");
    print(points[0].x); print(","); print(points[0].y);
    printflush(message1);
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "7,14,5,7,9,10,11,13",
            "引用参数复制、返回写回、结构体写回或地址固定错误");

    const std::string restrictedSource = R"(
arr<int> values = {cell1, 0};
void swap(restrict int& first, restrict int& second) {
    int temporary = first;
    first = second;
    second = temporary;
}
void main_loop() {
    int first = 0;
    int second = 1;
    values[0] = 3;
    values[1] = 4;
    swap(values[first], values[second]);
    print(values[0]); print(","); print(values[1]);
    printflush(message1);
}
)";
    Simulator restrictedSimulator(mdtc::compile(restrictedSource));
    restrictedSimulator.runUntilFlushes(1);
    require(restrictedSimulator.flushes().at(0).text == "4,3",
            "restrict 动态内存引用写回错误");

    const std::string distinctMemorySource = R"(
void swap(number& first, number& second) {
    number temporary = first;
    first = second;
    second = temporary;
}
void main_loop() {
    cell1[0] = 1;
    cell2[0] = 2;
    swap(cell1[0], cell2[0]);
}
)";
    (void)mdtc::compile(distinctMemorySource);
}

void testDiagnostics() {
    const std::string deadFunctionAssembly =
        mdtc::compile("void recurse() { recurse(); } void main_loop() {}");
    require(deadFunctionAssembly.find("__function_recurse") == std::string::npos,
            "未调用函数没有被删除");

    requireCompileError([] {
        (void)mdtc::compile("void recurse() { recurse(); } void main_loop() { recurse(); }");
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
        (void)mdtc::compile("void main_loop() { int value = @copper; }");
    }, "不能把 item 赋值给 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { print(@copper + @lead); }");
    }, "算术运算需要数值操作数");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { print(@copper == @water); }");
    }, "不能比较 item 和 liquid");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { unit_kind value = @router; }");
    }, "不能把 block 赋值给 unit_kind");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { print(@not-a-content); }");
    }, "未知或暂不支持");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<item> values = {}; }");
    }, "数组元素类型不能存储");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { item value = lookup_item(1.5); }");
    }, "参数必须是 int");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building factory; void main_loop() { factory.set_production(@copper); }");
    }, "不能把 item 赋值给 unit_kind");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building source; void main_loop() { source.set_output_item(@water); }");
    }, "不能把 liquid 赋值给 item");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building source; void main_loop() { source.set_payload_kind(@copper); }");
    }, "参数必须是 block 或 unit_kind");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building source; void main_loop() { source.clear_recipe(1); }");
    }, "不需要参数");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building router; void main_loop() { router.set_rotation(1.5); }");
    }, "不能把 number 赋值给 int");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building first; void main_loop() { first.copy_configuration_from(@router); }");
    }, "不能把 block 赋值给 building");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { print(target.get(@shoot)); }");
    }, "必须是可感知的内置 @ 常量");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { print(target.get(@sharded)); }");
    }, "必须是 sensor、item、liquid、block 或 unit_kind");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { print(target.get(@health, @x)); }");
    }, "需要一个 sensor 或内容常量参数");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { print(target.get_health(1)); }");
    }, "不需要参数");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { int value = target.get_first_item(); }");
    }, "不能把 item 赋值给 int");

    requireCompileError([] {
        (void)mdtc::compile(
            "extern building target; void main_loop() { sensor property = @health; print(target.get(property)); }");
    }, "必须是可感知的内置 @ 常量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<sensor_value> values = {}; }");
    }, "数组元素类型不能存储");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { number value = null; }");
    }, "不能把 null 赋值给 number");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { stop(1); }");
    }, "不需要参数");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { exit(false); }");
    }, "不需要参数");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { radar_filter filter = radar_enemy; }");
    }, "选择器类型不能声明变量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int radar_enemy = 0; }");
    }, "内置常量名称不能被声明");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.radar(); }");
    }, "需要 radar_sort 和 int order");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.radar_nearest(radar_distance); }");
    }, "必须是内置 radar_filter 常量");

    requireCompileError([] {
        (void)mdtc::compile(
            "void main_loop() { turret1.radar_nearest(radar_enemy, radar_flying, radar_attacker, radar_boss); }");
    }, "最多接受三个");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.radar(radar_enemy, radar_health, true); }");
    }, "order 必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.radar(radar_enemy, radar_enemy, 1); }");
    }, "必须是内置 radar_sort 常量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int value = true ? 1 : @copper; }");
    }, "两支类型不兼容");

    requireCompileError([] {
        (void)mdtc::compile(
            "void main_loop() { print(true ? radar_enemy : radar_flying); }");
    }, "选择器不能作为三目运算符结果");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int value = true ? 1; }");
    }, "缺少冒号");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { true ? print(1) : 2; }");
    }, "必须同时为 void 或同时产生值");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { int x; int bad() { return this; } }; void main_loop() {}");
    }, "this 不能作为独立表达式");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { int x; }; void main_loop() { value v{}; print(v->x); }");
    }, "需要表达式");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { int x; void set() { this->x = 1; } }; "
            "void main_loop() { value{}.set(); }");
    }, "引用参数需要可赋值左值");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { int x; void set(int amount) { x = amount; } }; "
            "void main_loop() { value v{}; v.set(); }");
    }, "参数数量错误");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { int x; void take(restrict int& input) { x = input; } }; "
            "void main_loop() { value v{}; v.take(v.x); }");
    }, "引用实参存在已知别名");

    requireCompileError([] {
        (void)mdtc::compile(
            "struct value { void recurse() { recurse(); } }; void main_loop() { value v{}; v.recurse(); }");
    }, "暂不支持递归调用");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.shoot(1, true); }");
    }, "不能把 int 赋值给 point");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.shoot(point{}, point{}); }");
    }, "条件需要 bool 或数值类型");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { turret1.shootp(1, true); }");
    }, "不能把 int 赋值给 posc");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { illuminator1.set_color(color{}); }");
    }, "不能把 color 赋值给 packed_color");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<posc> targets = {}; }");
    }, "数组元素类型不能存储");

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

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<string> value = {}; }");
    }, "数组元素类型不能存储");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<int> value = {}; print(value[1.0]); }");
    }, "数组索引必须是 int");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr2d<int> value = {}; value[1] = {}; }");
    }, "赋值左侧必须是可修改变量");

    requireCompileError([] {
        (void)mdtc::compile("void f(int& a, int& b) {} void main_loop() { int x = 0; f(x, x); }");
    }, "已知别名");

    requireCompileError([] {
        (void)mdtc::compile(
            "void f(point& whole, number& field) {} void main_loop() { point p = {}; f(p, p.x); }");
    }, "已知别名");

    requireCompileError([] {
        (void)mdtc::compile(
            "arr<int> a = {cell1, 0}; void f(restrict int& x, restrict int& y) {} "
            "void main_loop() { f(a[0], a[0]); }");
    }, "已知别名");

    requireCompileError([] {
        (void)mdtc::compile(
            "arr<int> a = {cell1, 0}; void f(int& x, int& y) {} "
            "void main_loop() { int i = 0; int j = 1; f(a[i], a[j]); }");
    }, "需要 restrict");

    requireCompileError([] {
        (void)mdtc::compile("void f(int& value) {} void main_loop() { f(1 + 2); }");
    }, "可赋值左值");

    requireCompileError([] {
        (void)mdtc::compile("void f(int& value) {} void main_loop() { number x = 1; f(x); }");
    }, "精确匹配");

    requireCompileError([] {
        (void)mdtc::compile("void f(restrict int value) {} void main_loop() {}");
    }, "只能修饰引用参数");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { arr<unit> workers = {}; }");
    }, "数组元素类型不能存储");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { unit worker = unit_bind(@copper); }");
    }, "unit_kind 或 unit");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int build_up = 1; }");
    }, "内置常量名称不能被声明");

    requireCompileError([] {
        (void)mdtc::compile(
            "void main_loop() { unit worker = unit_bind(@poly); worker.get_block(point{}, @stone, router1, @air); }");
    }, "可赋值");
}

} // namespace

int main() {
    try {
        testFunctionsControlFlowAndPrint();
        testSwitchCaseAndJumpTables();
        testAutomaticInlining();
        testGlobalPersistsAcrossMainLoop();
        testMainInitRunsOnceBeforeMainLoop();
        testGlobalInitializerFunctionReachability();
        testWhileAndShortCircuit();
        testSafeComparisonJumpFusion();
        testLoopCarriedValueAcrossEmptyBlocks();
        testValueAfterFunctionCallRemainsLive();
        testCalleeStateUpdatesRemainLive();
        testBasicTypes();
        testUtf8SourceIdentifiersStringsAndComments();
        testPreprocessorDefinesAndIncludes();
        testGetLink();
        testContentConstantsAndLookup();
        testBuiltinBuildingMemberMethods();
        testUnitBindAndControlMemberMethods();
        testConfigMemberMethods();
        testSensorMemberMethods();
        testNullLiteralAndComparison();
        testStopAndExit();
        testRadarMemberMethods();
        testConditionalSelect();
        testUserMemberFunctions();
        testImplicitLinksAndPrintFlushIndex();
        testPrintCharFormatAndPrintf();
        testDrawFlush();
        testDrawColorAndStrokeAliases();
        testColorAndDrawCommands();
        testConstantPackedColorFolding();
        testEmptyStatementLoopBody();
        testStructsInitializersFunctionsAndSizeof();
        testSizeofBuiltinTypes();
        testBuiltinPointVectorAndRect();
        testVectorDotAndCross();
        testOpBuiltinFunctions();
        testMemoryArrays();
        testReferenceParameters();
        testDiagnostics();
    } catch (const std::exception& error) {
        std::cerr << "测试失败: " << error.what() << '\n';
        return 1;
    }
    std::cout << "全部测试通过\n";
    return 0;
}
