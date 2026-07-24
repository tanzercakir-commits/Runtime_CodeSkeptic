// SPDX-License-Identifier: Apache-2.0
//
// SHA-256 (FIPS 180-4). Used for evidence hashes on environment profiles,
// requirement documents, contracts and analysis bundles.
//
// This is an integrity/identity hash, not a security primitive in an
// adversarial setting. See docs/architecture/determinism.md.
#ifndef RUNTIMESKEPTIC_CORE_SHA256_HPP
#define RUNTIMESKEPTIC_CORE_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rs::hash {

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    void update(std::string_view text) { update(text.data(), text.size()); }

    // Finalizes the hash. The object must not be updated afterwards.
    std::array<std::uint8_t, 32> digest();

    // Lowercase hex, 64 characters.
    std::string hex_digest();

private:
    void transform(const std::uint8_t block[64]);

    std::uint32_t state_[8];
    std::uint64_t bit_length_ = 0;
    std::uint8_t buffer_[64]{};
    std::size_t buffer_len_ = 0;
    bool finalized_ = false;
};

std::string sha256_hex(std::string_view text);

// "sha256:<64 hex chars>" — the form written into JSON artifacts.
std::string sha256_uri(std::string_view text);

}  // namespace rs::hash

#endif  // RUNTIMESKEPTIC_CORE_SHA256_HPP
