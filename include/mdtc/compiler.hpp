#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mdtc {

class CompileError : public std::runtime_error {
public:
    CompileError(std::size_t line, std::size_t column, const std::string& message);

    [[nodiscard]] std::size_t line() const noexcept;
    [[nodiscard]] std::size_t column() const noexcept;

private:
    std::size_t line_;
    std::size_t column_;
};

struct CompileOptions {
    bool debug = false;
    std::string sourcePath;
    std::vector<std::string> includePaths;
};

[[nodiscard]] std::string compile(std::string_view source, const CompileOptions& options = {});

} // namespace mdtc
