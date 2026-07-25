// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/address_range.hpp"

#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;

RS_TEST(half_open_semantics) {
    const AddressRange r{0x1000, 0x2000};
    RS_CHECK_EQ(r.length(), std::uint64_t{0x1000});
    RS_CHECK(r.contains(0x1000));
    RS_CHECK(r.contains(0x1fff));
    RS_CHECK(!r.contains(0x2000));  // exclusive end
    RS_CHECK(!r.contains(0xfff));
}

RS_TEST(empty_ranges) {
    RS_CHECK((AddressRange{0x1000, 0x1000}.empty()));
    RS_CHECK((AddressRange{0x2000, 0x1000}.empty()));
    RS_CHECK_EQ(AddressRange({0x2000, 0x1000}).length(), std::uint64_t{0});
    RS_CHECK(!AddressRange({0x1000, 0x1001}).empty());
}

RS_TEST(from_base_size_detects_wraparound) {
    RS_CHECK(AddressRange::from_base_size(0x1000, 0x1000).has_value());
    RS_CHECK(!AddressRange::from_base_size(UINT64_MAX, 2).has_value());
    RS_CHECK(!AddressRange::from_base_size(0x1000, 0).has_value());
    const auto max_ok = AddressRange::from_base_size(UINT64_MAX - 1, 1);
    RS_CHECK(max_ok.has_value());
}

RS_TEST(adjacent_ranges_do_not_intersect) {
    const AddressRange a{0x1000, 0x2000};
    const AddressRange b{0x2000, 0x3000};
    RS_CHECK(!a.intersects(b));
    RS_CHECK(!b.intersects(a));
    RS_CHECK(a.intersects(AddressRange{0x1fff, 0x3000}));
}

RS_TEST(containment_is_strict_about_partial_overlap) {
    const AddressRange outer{0x1000, 0x4000};
    RS_CHECK(outer.contains(AddressRange{0x2000, 0x3000}));
    RS_CHECK(outer.contains(outer));
    RS_CHECK(!outer.contains(AddressRange{0x3000, 0x5000}));
    RS_CHECK(!outer.contains(AddressRange{0x0500, 0x2000}));
    RS_CHECK(!outer.contains(AddressRange{0x2000, 0x2000}));  // empty
}

RS_TEST(intersection_is_the_overlap) {
    const AddressRange a{0x1000, 0x3000};
    const AddressRange b{0x2000, 0x4000};
    const auto i = a.intersection(b);
    RS_CHECK(i.has_value());
    RS_CHECK(*i == AddressRange({0x2000, 0x3000}));
    RS_CHECK(!a.intersection(AddressRange{0x8000, 0x9000}).has_value());
}

RS_TEST(alignment_helpers_require_power_of_two) {
    RS_CHECK(is_power_of_two(4096));
    RS_CHECK(!is_power_of_two(0));
    RS_CHECK(!is_power_of_two(4095));
    RS_CHECK(!is_power_of_two(6));

    RS_CHECK(is_aligned(0x2000, 4096));
    RS_CHECK(!is_aligned(0x2001, 4096));
    // A non-power-of-two alignment must not silently produce an answer.
    RS_CHECK(!is_aligned(0x2000, 6));
    RS_CHECK(!align_up(0x1001, 6).has_value());
}

RS_TEST(align_up_and_down) {
    RS_CHECK_EQ(*align_up(0x1001, 4096), std::uint64_t{0x2000});
    RS_CHECK_EQ(*align_up(0x1000, 4096), std::uint64_t{0x1000});
    RS_CHECK_EQ(*align_down(0x1fff, 4096), std::uint64_t{0x1000});
    RS_CHECK_EQ(*align_up(0, 4096), std::uint64_t{0});
    // Overflow near the top of the address space must be reported, not wrapped.
    RS_CHECK(!align_up(UINT64_MAX, 4096).has_value());
}

RS_TEST(round_up_to_granularity) {
    RS_CHECK_EQ(*round_up_to(1, 65536), std::uint64_t{65536});
    RS_CHECK_EQ(*round_up_to(65536, 65536), std::uint64_t{65536});
    RS_CHECK_EQ(*round_up_to(65537, 65536), std::uint64_t{131072});
}

RS_TEST(classified_range_requires_explicit_evidence) {
    auto parsed = json::parse("{\"start\":\"0x1000\",\"end\":\"0x2000\"}");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!ClassifiedRange::from_json(*parsed.value, error).has_value());
    RS_CHECK(!error.empty());
}

RS_TEST(classified_range_round_trips) {
    auto parsed = json::parse(
        "{\"start\":\"0x1000000000\",\"end\":\"0x1000004000\","
        "\"evidence\":\"measured_capability\",\"note\":\"n\"}");
    RS_CHECK(parsed.ok());
    std::string error;
    auto range = ClassifiedRange::from_json(*parsed.value, error);
    RS_CHECK(range.has_value());
    RS_CHECK_EQ(range->range.start, std::uint64_t{0x1000000000});
    RS_CHECK(range->evidence == EvidenceClass::MeasuredCapability);

    const json::Value back = range->to_json();
    RS_CHECK_EQ(back.find("start")->as_string(), std::string("0x1000000000"));
}

RS_TEST(classified_range_rejects_inverted_bounds) {
    auto parsed = json::parse(
        "{\"start\":\"0x2000\",\"end\":\"0x1000\","
        "\"evidence\":\"measured_capability\"}");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!ClassifiedRange::from_json(*parsed.value, error).has_value());
}

namespace {

ClassifiedRange cr(std::uint64_t start, std::uint64_t end, std::string note) {
    ClassifiedRange r;
    r.range = AddressRange{start, end};
    r.evidence = EvidenceClass::MeasuredCapability;
    r.note = std::move(note);
    return r;
}

}  // namespace

// The macOS probe learns about one refused entry from three directions: the
// survey ladder samples a page, the scan probes a 4 MiB window, and vm_region
// then reports that the entry behind both is 384 GiB wide. Keeping all three
// would state one fact at three different sizes.
RS_TEST(contained_ranges_collapse_into_the_widest) {
    std::vector<ClassifiedRange> ranges{
        cr(0x1000000000, 0x1000001000, "ladder: single-page hole"),
        cr(0x1307200000, 0x1307600000, "scan: 4 MiB probe window"),
        cr(0x1000000000, 0x7000000000, "vm_region: the whole entry"),
    };
    collapse_contained_ranges(ranges);

    RS_CHECK_EQ(ranges.size(), std::size_t{1});
    RS_CHECK_EQ(ranges[0].range.start, std::uint64_t{0x1000000000});
    RS_CHECK_EQ(ranges[0].range.end, std::uint64_t{0x7000000000});
    // The survivor must be the one that can explain itself.
    RS_CHECK(ranges[0].note.find("vm_region") != std::string::npos);
}

// The commpage ends exactly where the GPU carveout begins. They are two
// entries, reported separately by the kernel, carrying different notes.
// Merging them would state a fact vm_region never reported.
RS_TEST(adjacent_ranges_are_not_merged) {
    std::vector<ClassifiedRange> ranges{
        cr(0x1000000000, 0x7000000000, "gpu carveout"),
        cr(0xfc0000000, 0x1000000000, "commpage"),
    };
    collapse_contained_ranges(ranges);

    RS_CHECK_EQ(ranges.size(), std::size_t{2});
    RS_CHECK_EQ(ranges[0].range.start, std::uint64_t{0xfc0000000});
    RS_CHECK_EQ(ranges[0].range.end, std::uint64_t{0x1000000000});
    RS_CHECK_EQ(ranges[1].range.start, std::uint64_t{0x1000000000});
}

// Neither subsumes the other, and each carries its own account of how it was
// found. Inventing a union would lose one of the two explanations.
RS_TEST(partially_overlapping_ranges_are_both_kept) {
    std::vector<ClassifiedRange> ranges{
        cr(0x2000, 0x6000, "b"),
        cr(0x1000, 0x4000, "a"),
    };
    collapse_contained_ranges(ranges);
    RS_CHECK_EQ(ranges.size(), std::size_t{2});
}

RS_TEST(identical_ranges_collapse_to_one) {
    std::vector<ClassifiedRange> ranges{
        cr(0x1000, 0x2000, "first"),
        cr(0x1000, 0x2000, "second"),
    };
    collapse_contained_ranges(ranges);
    RS_CHECK_EQ(ranges.size(), std::size_t{1});
}

RS_TEST(collapse_sorts_and_tolerates_an_empty_list) {
    std::vector<ClassifiedRange> empty;
    collapse_contained_ranges(empty);
    RS_CHECK(empty.empty());

    std::vector<ClassifiedRange> ranges{
        cr(0x9000, 0xa000, "high"),
        cr(0x1000, 0x2000, "low"),
    };
    collapse_contained_ranges(ranges);
    RS_CHECK_EQ(ranges.size(), std::size_t{2});
    RS_CHECK(ranges[0].range < ranges[1].range);
}

RS_TEST_MAIN("address_range")
