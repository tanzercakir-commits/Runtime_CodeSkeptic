// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_CORE_IO_HPP
#define RUNTIMESKEPTIC_CORE_IO_HPP

#include <optional>
#include <string>

namespace rs::io {

// Reads a whole file. Returns nullopt on failure and sets `error`.
// The path "-" reads standard input.
std::optional<std::string> read_file(const std::string& path, std::string& error);

// Writes a whole file. The path "-" writes to standard output.
bool write_file(const std::string& path, const std::string& contents,
                std::string& error);

}  // namespace rs::io

#endif  // RUNTIMESKEPTIC_CORE_IO_HPP
