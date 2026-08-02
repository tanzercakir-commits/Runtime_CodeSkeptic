// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/address_range.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rs::vm {

void collapse_contained_ranges(std::vector<ClassifiedRange>& ranges) {
    std::sort(ranges.begin(), ranges.end(),
              [](const ClassifiedRange& a, const ClassifiedRange& b) {
                  if (a.range.start != b.range.start) {
                      return a.range.start < b.range.start;
                  }
                  return a.range.end > b.range.end;  // widest first
              });
    std::vector<ClassifiedRange> kept;
    for (auto& candidate : ranges) {
        // contains() is false for an empty argument, so an equal-range
        // duplicate has to be caught separately or it survives forever.
        const bool covered = std::any_of(
            kept.begin(), kept.end(), [&](const ClassifiedRange& k) {
                return k.range == candidate.range ||
                       k.range.contains(candidate.range);
            });
        if (!covered) kept.push_back(std::move(candidate));
    }
    ranges = std::move(kept);
}

std::optional<AddressRange> AddressRange::from_base_size(std::uint64_t base,
                                                         std::uint64_t size) {
    if (size == 0) return std::nullopt;
    if (base > std::numeric_limits<std::uint64_t>::max() - size) {
        return std::nullopt;  // wraps past the end of the address space
    }
    return AddressRange{base, base + size};
}

std::optional<AddressRange> AddressRange::intersection(
    const AddressRange& other) const {
    if (!intersects(other)) return std::nullopt;
    return AddressRange{start > other.start ? start : other.start,
                        end < other.end ? end : other.end};
}

std::string AddressRange::to_string() const {
    return "[" + json::to_hex(start) + ", " + json::to_hex(end) + ")";
}

json::Value ClassifiedRange::to_json() const {
    json::Value v = json::Value::object();
    v["start"] = json::to_hex(range.start);
    v["end"] = json::to_hex(range.end);
    v["evidence"] = std::string(rs::to_string(evidence));
    if (!source.empty()) v["source"] = source;
    if (!note.empty()) v["note"] = note;
    return v;
}

std::optional<ClassifiedRange> ClassifiedRange::from_json(const json::Value& v,
                                                          std::string& error) {
    if (!v.is_object()) {
        error = "range entry must be an object";
        return std::nullopt;
    }
    // The schema types a range with additionalProperties:false: an unrecognized
    // key is a misspelled field, not data, and accepting it silently was part
    // of the "extra field accepted" defect the independent review flagged (A2).
    for (const auto& [key, child] : v.as_object()) {
        (void)child;
        if (key != "start" && key != "end" && key != "evidence" &&
            key != "note" && key != "source") {
            error = "range entry has an unrecognized field '" + key + "'";
            return std::nullopt;
        }
    }
    const json::Value* start = v.find("start");
    const json::Value* end = v.find("end");
    const json::Value* evidence = v.find("evidence");
    if (start == nullptr || end == nullptr) {
        error = "range entry requires 'start' and 'end'";
        return std::nullopt;
    }

    auto read_addr = [&](const json::Value& value,
                         std::uint64_t& out) -> bool {
        if (value.is_string()) {
            auto parsed = json::from_hex(value.as_string());
            if (!parsed) {
                error = "address must be a hex string like \"0x1000\": got \"" +
                        value.as_string() + "\"";
                return false;
            }
            out = *parsed;
            return true;
        }
        if (value.type() == json::Type::UInt || value.type() == json::Type::Int) {
            out = value.as_uint();
            return true;
        }
        error = "address must be a hex string";
        return false;
    };

    ClassifiedRange out;
    if (!read_addr(*start, out.range.start)) return std::nullopt;
    if (!read_addr(*end, out.range.end)) return std::nullopt;
    if (out.range.end < out.range.start) {
        error = "range end precedes start: " + out.range.to_string();
        return std::nullopt;
    }

    if (evidence != nullptr) {
        if (!evidence->is_string() ||
            !evidence_class_from_string(evidence->as_string(), out.evidence)) {
            error = "unrecognized evidence class on range";
            return std::nullopt;
        }
    } else {
        error = "range entry requires an explicit 'evidence' class";
        return std::nullopt;
    }

    if (const json::Value* source = v.find("source"); source != nullptr) {
        if (!source->is_string()) {
            error = "range 'source' must be a string";
            return std::nullopt;
        }
        out.source = source->as_string();
    }
    if (const json::Value* note = v.find("note"); note != nullptr) {
        if (!note->is_string()) {
            error = "range 'note' must be a string";
            return std::nullopt;
        }
        out.note = note->as_string();
    }
    return out;
}

bool is_power_of_two(std::uint64_t v) { return v != 0 && (v & (v - 1)) == 0; }

bool is_aligned(std::uint64_t value, std::uint64_t alignment) {
    if (!is_power_of_two(alignment)) return false;
    return (value & (alignment - 1)) == 0;
}

std::optional<std::uint64_t> align_up(std::uint64_t value,
                                      std::uint64_t alignment) {
    if (!is_power_of_two(alignment)) return std::nullopt;
    const std::uint64_t mask = alignment - 1;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
        return std::nullopt;
    }
    return (value + mask) & ~mask;
}

std::optional<std::uint64_t> align_down(std::uint64_t value,
                                        std::uint64_t alignment) {
    if (!is_power_of_two(alignment)) return std::nullopt;
    return value & ~(alignment - 1);
}

std::optional<std::uint64_t> round_up_to(std::uint64_t size,
                                         std::uint64_t granularity) {
    return align_up(size, granularity);
}

}  // namespace rs::vm
