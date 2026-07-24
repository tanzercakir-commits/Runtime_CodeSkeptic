// SPDX-License-Identifier: Apache-2.0
//
// Virtual address range arithmetic.
//
// CONVENTION: ranges are HALF-OPEN, [start, end).
//
// The ROADMAP's illustrative YAML shows `start: 0x1000000000, end:
// 0x6fffffffff`, which reads as an inclusive range. We deliberately use
// half-open intervals instead, because:
//
//   - `end - start` is the length, with no off-by-one
//   - an empty range is representable (start == end)
//   - adjacency and overlap tests have no special cases
//
// The schema documents this explicitly and `rs-profile verify` rejects
// ranges where end < start.
//
// LIMITATION: a range ending at the very top of the address space is not
// representable, because its exclusive end would be 2^64. `end == 0` is NOT
// used as a sentinel for that case - it simply means an empty range. In
// practice no user-mode address space reaches UINT64_MAX, so rs-env-probe
// never needs the case; if a future platform does, the representation has to
// change rather than be worked around at the call site.
#ifndef RUNTIMESKEPTIC_VM_ADDRESS_RANGE_HPP
#define RUNTIMESKEPTIC_VM_ADDRESS_RANGE_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/core/json.hpp"

namespace rs::vm {

struct AddressRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;  // exclusive

    static std::optional<AddressRange> from_base_size(std::uint64_t base,
                                                      std::uint64_t size);

    bool empty() const { return end <= start; }
    std::uint64_t length() const { return empty() ? 0 : end - start; }

    bool contains(std::uint64_t addr) const {
        return addr >= start && addr < end;
    }

    bool contains(const AddressRange& other) const {
        return !other.empty() && other.start >= start && other.end <= end;
    }

    bool intersects(const AddressRange& other) const {
        if (empty() || other.empty()) return false;
        return start < other.end && other.start < end;
    }

    std::optional<AddressRange> intersection(const AddressRange& other) const;

    std::string to_string() const;

    friend bool operator==(const AddressRange& a, const AddressRange& b) {
        return a.start == b.start && a.end == b.end;
    }
    friend bool operator<(const AddressRange& a, const AddressRange& b) {
        return a.start != b.start ? a.start < b.start : a.end < b.end;
    }
};

// A range plus the evidence class that established it. An "unavailable range"
// with evidence Unknown is meaningless and must never be produced: absence of
// observation is not evidence of impossibility (ROADMAP 10.1).
struct ClassifiedRange {
    AddressRange range;
    EvidenceClass evidence = EvidenceClass::Unknown;
    std::string note;

    json::Value to_json() const;
    static std::optional<ClassifiedRange> from_json(const json::Value& v,
                                                    std::string& error);
};

// Alignment helpers. `alignment` must be a power of two; the helpers return
// nullopt otherwise so callers cannot silently compute garbage.
bool is_power_of_two(std::uint64_t v);
bool is_aligned(std::uint64_t value, std::uint64_t alignment);
std::optional<std::uint64_t> align_up(std::uint64_t value, std::uint64_t alignment);
std::optional<std::uint64_t> align_down(std::uint64_t value,
                                        std::uint64_t alignment);

// Number of bytes the platform would actually reserve for `size` given
// `granularity`. Returns nullopt on overflow.
std::optional<std::uint64_t> round_up_to(std::uint64_t size,
                                         std::uint64_t granularity);

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_ADDRESS_RANGE_HPP
