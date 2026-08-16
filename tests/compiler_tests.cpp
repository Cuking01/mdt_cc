#include "mdtc/compiler.hpp"

#include <cmath>
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

using Value = std::variant<std::monostate, double, std::string, ObjectValue>;

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
        if (name == "add") return number(left) + number(right);
        if (name == "sub") return number(left) - number(right);
        if (name == "mul") return number(left) * number(right);
        if (name == "div") return number(left) / number(right);
        if (name == "mod") return std::fmod(number(left), number(right));
        if (name == "equal") return equal(left, right) ? 1.0 : 0.0;
        if (name == "notEqual") return equal(left, right) ? 0.0 : 1.0;
        if (name == "lessThan") return number(left) < number(right) ? 1.0 : 0.0;
        if (name == "lessThanEq") return number(left) <= number(right) ? 1.0 : 0.0;
        if (name == "greaterThan") return number(left) > number(right) ? 1.0 : 0.0;
        if (name == "greaterThanEq") return number(left) >= number(right) ? 1.0 : 0.0;
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
extern message message1;

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

void testPrintFlushStringTarget() {
    const std::string source = R"(
void main_loop() {
    print("hello");
    printflush("message1");
}
)";

    const std::string assembly = mdtc::compile(source);
    require(assembly.find("printflush message1\n") != std::string::npos,
            "字符串字面量没有转换为裸链接名称");
    require(assembly.find("printflush \"message1\"\n") == std::string::npos,
            "字符串字面量错误地保留了引号");
    Simulator simulator(assembly);
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).target == "message1", "字符串 printflush 目标错误");
    require(simulator.flushes().at(0).text == "hello", "字符串 printflush 内容错误");
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
    printflush("message1");
}
)";

    Simulator simulator(mdtc::compile(source));
    simulator.runUntilFlushes(1);
    require(simulator.flushes().at(0).text == "1", "空语句循环体编译错误");
}

void testDiagnostics() {
    requireCompileError([] {
        (void)mdtc::compile("void recurse() { recurse(); } void main_loop() {}");
    }, "递归调用");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { int value = 1; printflush(value); }");
    }, "必须是 message 或字符串字面量");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { string target = \"message1\"; printflush(target); }");
    }, "必须是 message 或字符串字面量");

    requireCompileError([] {
        (void)mdtc::compile("int value = 1;");
    }, "main_loop");

    requireCompileError([] {
        (void)mdtc::compile("void main_loop() { building value = getlink(1.5); }");
    }, "必须是 int");
}

} // namespace

int main() {
    try {
        testFunctionsControlFlowAndPrint();
        testGlobalPersistsAcrossMainLoop();
        testWhileAndShortCircuit();
        testBasicTypes();
        testGetLink();
        testPrintFlushStringTarget();
        testEmptyStatementLoopBody();
        testDiagnostics();
    } catch (const std::exception& error) {
        std::cerr << "测试失败: " << error.what() << '\n';
        return 1;
    }
    std::cout << "全部测试通过\n";
    return 0;
}
