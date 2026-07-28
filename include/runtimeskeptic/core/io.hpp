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

// Creates `path` and every parent that does not exist, like `mkdir -p`.
// Succeeds if the directory already exists. An existing NON-directory at the
// path is an error, not a success: a bundle written on top of a file is not a
// bundle.
bool make_directories(const std::string& path, std::string& error);

}  // namespace rs::io

#endif  // RUNTIMESKEPTIC_CORE_IO_HPP
