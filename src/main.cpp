#include "mdtc/compiler.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法打开输入文件: " + path);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("无法打开输出文件: " + path);
    }
    output << content;
}

void printUsage() {
    std::cerr << "用法: mdtc [--debug] [-I <包含目录>] <输入文件> [-o <输出文件>]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string outputPath;
    mdtc::CompileOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--debug") {
            options.debug = true;
        } else if (argument == "-I") {
            if (++index >= argc) {
                printUsage();
                return 2;
            }
            options.includePaths.push_back(argv[index]);
        } else if (argument.size() > 2 && argument.starts_with("-I")) {
            options.includePaths.push_back(argument.substr(2));
        } else if (argument == "-o") {
            if (++index >= argc) {
                printUsage();
                return 2;
            }
            outputPath = argv[index];
        } else if (!argument.empty() && argument.front() == '-') {
            printUsage();
            return 2;
        } else if (inputPath.empty()) {
            inputPath = argument;
        } else {
            printUsage();
            return 2;
        }
    }

    if (inputPath.empty()) {
        printUsage();
        return 2;
    }

    try {
        options.sourcePath = inputPath;
        const std::string output = mdtc::compile(readFile(inputPath), options);
        if (outputPath.empty()) {
            std::cout << output;
        } else {
            writeFile(outputPath, output);
        }
    } catch (const mdtc::CompileError& error) {
        std::cerr << inputPath << ':' << error.line() << ':' << error.column()
                  << ": 编译错误: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "错误: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
