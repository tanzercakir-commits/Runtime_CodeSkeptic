// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/io.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace rs::io {

std::optional<std::string> read_file(const std::string& path,
                                     std::string& error) {
    if (path == "-") {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        return buffer.str();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path + "': " + std::strerror(errno);
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        error = "error while reading '" + path + "'";
        return std::nullopt;
    }
    return buffer.str();
}

bool write_file(const std::string& path, const std::string& contents,
                std::string& error) {
    if (path == "-") {
        std::cout << contents;
        std::cout.flush();
        return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write '" + path + "': " + std::strerror(errno);
        return false;
    }
    out << contents;
    out.flush();
    if (!out) {
        error = "error while writing '" + path + "'";
        return false;
    }
    return true;
}

}  // namespace rs::io
