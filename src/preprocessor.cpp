#include "preprocessor.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mdtc {
namespace {

namespace fs = std::filesystem;

class Preprocessor {
public:
    explicit Preprocessor(const CompileOptions& options) : options_(options) {}

    std::string run(std::string_view source) {
        fs::path sourcePath;
        if (!options_.sourcePath.empty()) {
            sourcePath = fs::absolute(options_.sourcePath).lexically_normal();
            activeIncludes_.insert(sourcePath.string());
        }
        std::string result = process(source, sourcePath);
        if (!sourcePath.empty()) activeIncludes_.erase(sourcePath.string());
        return result;
    }

private:
    [[noreturn]] void fail(const fs::path& path, std::size_t line,
                           std::size_t column, const std::string& message) const {
        const std::string prefix = path.empty() ? "" : path.string() + ": ";
        throw CompileError(line, column, prefix + message);
    }

    static bool isIdentifierStart(unsigned char character) {
        return character == '_' || std::isalpha(character) || character >= 0x80;
    }

    void validateUtf8(std::string_view source, const fs::path& path) const {
        std::size_t position = 0;
        std::size_t line = 1;
        std::size_t column = 1;
        while (position < source.size()) {
            const unsigned char first = static_cast<unsigned char>(source[position]);
            std::size_t length = 1;
            char32_t value = first;
            if (first >= 0x80) {
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
                    fail(path, line, column, "非法 UTF-8 编码");
                }
                if (position + length > source.size()) {
                    fail(path, line, column, "非法 UTF-8 编码");
                }
                for (std::size_t offset = 1; offset < length; ++offset) {
                    const unsigned char continuation =
                        static_cast<unsigned char>(source[position + offset]);
                    if ((continuation & 0xC0) != 0x80) {
                        fail(path, line, column, "非法 UTF-8 编码");
                    }
                    value = (value << 6) | (continuation & 0x3F);
                }
                if ((length == 3 && value < 0x800) ||
                    (length == 4 && value < 0x10000) ||
                    (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
                    fail(path, line, column, "非法 UTF-8 编码");
                }
            }
            position += length;
            if (value == U'\n' || value == 0x2028 || value == 0x2029) {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    }

    static bool isIdentifierPart(unsigned char character) {
        return isIdentifierStart(character) || std::isdigit(character);
    }

    static void skipHorizontalWhitespace(std::string_view line, std::size_t& position) {
        while (position < line.size() &&
               (line[position] == ' ' || line[position] == '\t' ||
                line[position] == '\v' || line[position] == '\f' ||
                line[position] == '\r')) {
            ++position;
        }
    }

    static std::string readFile(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("无法打开包含文件: " + path.string());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    fs::path resolveInclude(std::string_view name, bool quoted,
                            const fs::path& currentPath) const {
        std::vector<fs::path> candidates;
        if (quoted) {
            if (!currentPath.empty()) {
                candidates.push_back(currentPath.parent_path() / std::string(name));
            } else {
                candidates.push_back(fs::current_path() / std::string(name));
            }
        }
        for (const std::string& includePath : options_.includePaths) {
            candidates.push_back(fs::path(includePath) / std::string(name));
        }
        for (fs::path candidate : candidates) {
            std::error_code error;
            candidate = fs::absolute(candidate, error).lexically_normal();
            if (!error && fs::is_regular_file(candidate, error) && !error) return candidate;
        }
        return {};
    }

    std::string expand(std::string_view text, bool& inBlockComment,
                       std::unordered_set<std::string>& expanding,
                       const fs::path& path, std::size_t line, std::size_t depth) {
        if (depth > 256) fail(path, line, 1, "宏展开层数超过 256");
        std::string result;
        std::size_t position = 0;
        while (position < text.size()) {
            if (inBlockComment) {
                const std::size_t end = text.find("*/", position);
                if (end == std::string_view::npos) {
                    result.append(text.substr(position));
                    break;
                }
                result.append(text.substr(position, end + 2 - position));
                position = end + 2;
                inBlockComment = false;
                continue;
            }
            if (text[position] == '/' && position + 1 < text.size() &&
                text[position + 1] == '/') {
                result.append(text.substr(position));
                break;
            }
            if (text[position] == '/' && position + 1 < text.size() &&
                text[position + 1] == '*') {
                result += "/*";
                position += 2;
                inBlockComment = true;
                continue;
            }
            if (text[position] == '"') {
                const std::size_t begin = position++;
                bool escaped = false;
                while (position < text.size()) {
                    const char character = text[position++];
                    if (!escaped && character == '"') break;
                    if (!escaped && character == '\\') {
                        escaped = true;
                    } else {
                        escaped = false;
                    }
                }
                result.append(text.substr(begin, position - begin));
                continue;
            }
            const unsigned char character = static_cast<unsigned char>(text[position]);
            if (!isIdentifierStart(character)) {
                result.push_back(text[position++]);
                continue;
            }
            const std::size_t begin = position++;
            while (position < text.size() &&
                   isIdentifierPart(static_cast<unsigned char>(text[position]))) {
                ++position;
            }
            const std::string identifier(text.substr(begin, position - begin));
            const auto macro = macros_.find(identifier);
            if (macro == macros_.end() || expanding.contains(identifier)) {
                result += identifier;
                continue;
            }
            expanding.insert(identifier);
            bool replacementComment = false;
            result += expand(macro->second, replacementComment, expanding, path, line, depth + 1);
            expanding.erase(identifier);
        }
        return result;
    }

    std::string expandLine(std::string_view lineText, bool& inBlockComment,
                           const fs::path& path, std::size_t line) {
        std::unordered_set<std::string> expanding;
        return expand(lineText, inBlockComment, expanding, path, line, 0);
    }

    std::string cleanReplacement(std::string_view replacement, const fs::path& path,
                                 std::size_t line, std::size_t column) const {
        std::string result;
        std::size_t position = 0;
        while (position < replacement.size()) {
            if (replacement[position] == '"') {
                const std::size_t begin = position++;
                bool escaped = false;
                while (position < replacement.size()) {
                    const char character = replacement[position++];
                    if (!escaped && character == '"') break;
                    if (!escaped && character == '\\') escaped = true;
                    else escaped = false;
                }
                result.append(replacement.substr(begin, position - begin));
                continue;
            }
            if (replacement[position] == '/' && position + 1 < replacement.size() &&
                replacement[position + 1] == '/') {
                break;
            }
            if (replacement[position] == '/' && position + 1 < replacement.size() &&
                replacement[position + 1] == '*') {
                const std::size_t end = replacement.find("*/", position + 2);
                if (end == std::string_view::npos) {
                    fail(path, line, column + position, "宏定义中的块注释必须在同一行结束");
                }
                result.push_back(' ');
                position = end + 2;
                continue;
            }
            result.push_back(replacement[position++]);
        }
        while (!result.empty() &&
               (result.back() == ' ' || result.back() == '\t' || result.back() == '\r')) {
            result.pop_back();
        }
        return result;
    }

    std::string processDirective(std::string_view lineText, std::size_t hashPosition,
                                 const fs::path& currentPath, std::size_t line) {
        std::size_t position = hashPosition + 1;
        skipHorizontalWhitespace(lineText, position);
        const std::size_t directiveBegin = position;
        while (position < lineText.size() &&
               std::isalpha(static_cast<unsigned char>(lineText[position]))) {
            ++position;
        }
        const std::string directive(lineText.substr(directiveBegin, position - directiveBegin));
        if (directive.empty()) return {};

        if (directive == "define") {
            skipHorizontalWhitespace(lineText, position);
            if (position >= lineText.size() ||
                !isIdentifierStart(static_cast<unsigned char>(lineText[position]))) {
                fail(currentPath, line, position + 1, "#define 缺少宏名称");
            }
            const std::size_t nameBegin = position++;
            while (position < lineText.size() &&
                   isIdentifierPart(static_cast<unsigned char>(lineText[position]))) {
                ++position;
            }
            const std::string name(lineText.substr(nameBegin, position - nameBegin));
            if (position < lineText.size() && lineText[position] == '(') {
                fail(currentPath, line, position + 1, "暂不支持带参数的宏");
            }
            skipHorizontalWhitespace(lineText, position);
            std::string replacement = cleanReplacement(lineText.substr(position), currentPath,
                                                       line, position + 1);
            const auto existing = macros_.find(name);
            if (existing != macros_.end() && existing->second != replacement) {
                fail(currentPath, line, nameBegin + 1, "宏 " + name + " 被重复定义");
            }
            macros_[name] = std::move(replacement);
            return {};
        }

        if (directive == "include") {
            skipHorizontalWhitespace(lineText, position);
            if (position >= lineText.size() ||
                (lineText[position] != '"' && lineText[position] != '<')) {
                fail(currentPath, line, position + 1, "#include 需要 \"文件\" 或 <文件>");
            }
            const bool quoted = lineText[position] == '"';
            const char closing = quoted ? '"' : '>';
            const std::size_t nameBegin = ++position;
            const std::size_t nameEnd = lineText.find(closing, position);
            if (nameEnd == std::string_view::npos) {
                fail(currentPath, line, nameBegin, "未结束的 #include 路径");
            }
            const std::string name(lineText.substr(nameBegin, nameEnd - nameBegin));
            position = nameEnd + 1;
            skipHorizontalWhitespace(lineText, position);
            if (position < lineText.size() && lineText[position] != '\r' &&
                !(lineText[position] == '/' && position + 1 < lineText.size() &&
                  lineText[position + 1] == '/')) {
                fail(currentPath, line, position + 1, "#include 路径后存在多余内容");
            }
            const fs::path includedPath = resolveInclude(name, quoted, currentPath);
            if (includedPath.empty()) {
                fail(currentPath, line, nameBegin, "找不到包含文件: " + name);
            }
            const std::string key = includedPath.string();
            if (activeIncludes_.contains(key)) {
                fail(currentPath, line, nameBegin, "检测到循环包含: " + key);
            }
            activeIncludes_.insert(key);
            std::string included;
            try {
                included = process(readFile(includedPath), includedPath);
            } catch (...) {
                activeIncludes_.erase(key);
                throw;
            }
            activeIncludes_.erase(key);
            return included;
        }

        fail(currentPath, line, directiveBegin + 1, "不支持的预处理命令 #" + directive);
    }

    std::string process(std::string_view source, const fs::path& currentPath) {
        validateUtf8(source, currentPath);
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        std::string result;
        bool inBlockComment = false;
        std::size_t offset = 0;
        std::size_t line = 1;
        while (offset < source.size()) {
            const std::size_t newline = source.find('\n', offset);
            const bool hasNewline = newline != std::string_view::npos;
            const std::size_t end = hasNewline ? newline : source.size();
            const std::string_view lineText = source.substr(offset, end - offset);

            std::size_t first = 0;
            skipHorizontalWhitespace(lineText, first);
            if (!inBlockComment && first < lineText.size() && lineText[first] == '#') {
                std::string directiveResult = processDirective(lineText, first, currentPath, line);
                result += directiveResult;
                if (hasNewline && (directiveResult.empty() || directiveResult.back() != '\n')) {
                    result.push_back('\n');
                }
            } else {
                result += expandLine(lineText, inBlockComment, currentPath, line);
                if (hasNewline) result.push_back('\n');
            }
            offset = hasNewline ? newline + 1 : source.size();
            ++line;
        }
        return result;
    }

    const CompileOptions& options_;
    std::unordered_map<std::string, std::string> macros_;
    std::unordered_set<std::string> activeIncludes_;
};

} // namespace

std::string preprocess(std::string_view source, const CompileOptions& options) {
    return Preprocessor(options).run(source);
}

} // namespace mdtc
