// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/extract/scanner.hpp"

#include <string>

#include "test_support.hpp"

using namespace rs;
using namespace rs::extract;

namespace {

// The shape of the pattern from shadPS4 issue #4157, wrapped across lines the
// way real source is written.
const char* kGtaVPattern = R"(#include <sys/mman.h>

void* MapGuestDirectMemory(void) {
    void* p = mmap((void*)0x1307200000, 0x20000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        ASSERT_MSG(0, "Mapping cannot fit inside free region");
    }
    return p;
}
)";

const Candidate* find(const ScanReport& r, const std::string& recognizer) {
    for (const auto& c : r.candidates) {
        if (c.recognizer == recognizer) return &c;
    }
    return nullptr;
}

}  // namespace

// The whole point of the tool: a real call, wrapped across lines, recovered
// with the same address and size the hand-written contract carries.
RS_TEST(recovers_the_gtav_pattern_from_wrapped_source) {
    const ScanReport r = scan_source("memory.c", kGtaVPattern);
    const Candidate* c = find(r, "mmap_fixed_literal");
    RS_CHECK(c != nullptr);
    if (!c) return;

    RS_CHECK(c->requirement.request.address.has_value());
    if (c->requirement.request.address) {
        RS_CHECK_EQ(*c->requirement.request.address, std::uint64_t{0x1307200000});
    }
    RS_CHECK_EQ(c->requirement.request.size, std::uint64_t{0x20000});
    RS_CHECK(c->requirement.request.exact_address_required);
    RS_CHECK(c->requirement.failure_sink.kind == vm::FailureSinkKind::FatalAssert);
}

// The first version of this scanner read one line at a time and missed EVERY
// MAP_FIXED site, because the flag sits on the continuation line. That is the
// most consequential bug the tool has had, so it gets its own test.
RS_TEST(a_call_split_across_lines_is_still_one_call) {
    const char* split = R"(void f(void) {
    void* p = mmap((void*)0x40000000,
                   65536,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                   -1, 0);
}
)";
    const ScanReport r = scan_source("s.c", split);
    RS_CHECK(find(r, "mmap_fixed_literal") != nullptr);
}

// `for (int i = 0; i < 30; i++)` opens with the literal 0. Taking the first
// literal as the bound made every retry loop invisible - including LuaJIT's
// thirty attempts, the exact shape RS-VM-0015 exists to judge.
RS_TEST(loop_bound_is_the_largest_literal_not_the_first) {
    const char* retry = R"(void* f(void) {
    for (int i = 0; i < 30; i++) {
        void* p = mmap((void*)0x40000000, 65536, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_FIXED, -1, 0);
        if (p != MAP_FAILED) return p;
    }
    return 0;
}
)";
    const ScanReport r = scan_source("s.c", retry);
    const Candidate* c = find(r, "mmap_fixed_literal");
    RS_CHECK(c != nullptr);
    if (!c) return;
    RS_CHECK(c->requirement.assumptions.retries_on_failure);
    RS_CHECK(c->requirement.assumptions.max_retries.has_value());
    if (c->requirement.assumptions.max_retries) {
        RS_CHECK_EQ(*c->requirement.assumptions.max_retries, std::uint64_t{30});
    }
}

// The honesty invariant, enforced rather than trusted. A requirement this tool
// emits without saying what it could not determine would read as an assertion
// about the program, which it never is.
RS_TEST(every_candidate_states_what_the_scanner_could_not_determine) {
    const char* mixed = R"(#define LJ_PAGESIZE 16384
void* a(void) {
    return mmap((void*)0x1307200000, 0x20000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_FIXED, -1, 0);
}
void* b(size_t n) {
    return mmap(0, n, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
int c(void* p, size_t n) { return mprotect(p, n, PROT_READ | PROT_EXEC); }
)";
    const ScanReport r = scan_source("s.c", mixed);
    RS_CHECK(r.candidates.size() >= 3);
    for (const auto& c : r.candidates) {
        RS_CHECK_MESSAGE(!c.requirement.extraction_limitations.empty(),
                         "candidate from " + c.recognizer +
                             " states no extraction limitation");
        // statically_inferred ceilings any derived finding at COUNTEREXAMPLE.
        // A scanner that claimed a stronger class would let a text match reach
        // PROVEN.
        RS_CHECK(c.requirement.assumption_evidence ==
                 EvidenceClass::StaticallyInferred);
    }
}

// A mapping call the scanner does not understand must be reported, not
// swallowed. Silence here would read as "this program has no other
// virtual-memory requirements", which the tool cannot know.
RS_TEST(unrecognised_mapping_calls_are_reported) {
    const char* opaque = R"(void* f(void* where, size_t n) {
    return mmap(where, n, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
)";
    const ScanReport r = scan_source("s.c", opaque);
    RS_CHECK(r.candidates.empty());
    RS_CHECK_EQ(r.rejected_sites.size(), std::size_t{1});
}

// The page-size recognizer records `equal`, the strictest reading, and must say
// so - campaign defect 4 was the analyzer assuming equality where the code meant
// at_most, and here the assumption belongs to the extractor.
RS_TEST(hardcoded_page_size_admits_it_guessed_the_relation) {
    const ScanReport r = scan_source("arch.h", "#define LJ_PAGESIZE 16384\n");
    const Candidate* c = find(r, "hardcoded_page_size");
    RS_CHECK(c != nullptr);
    if (!c) return;
    RS_CHECK_EQ(c->requirement.request.required_page_size.value_or(0),
                std::uint64_t{16384});
    RS_CHECK(c->requirement.request.required_page_size_relation ==
             vm::SizeRelation::Equal);
    bool admits = false;
    for (const auto& l : c->requirement.extraction_limitations) {
        if (l.find("at_most") != std::string::npos) admits = true;
    }
    RS_CHECK(admits);
}

// The bundle must carry the scan's own limits, or a consumer reads the
// requirement list as a complete account of the program.
RS_TEST(the_bundle_carries_the_scans_limitations) {
    const ScanReport r = scan_source("s.c", kGtaVPattern);
    const json::Value bundle = to_bundle({r}, "test");
    const json::Value* notes = bundle.find("x_scan_notes");
    RS_CHECK(notes != nullptr);
    if (!notes) return;
    RS_CHECK(!notes->as_array().empty());
}

RS_TEST(the_recognizer_list_has_not_silently_shrunk) {
    // Each entry must say what it CANNOT determine; an entry without that is a
    // recognizer whose limits nobody wrote down.
    const auto list = recognizers();
    RS_CHECK(list.size() >= 5);
    for (const auto& r : list) {
        RS_CHECK(r.name != nullptr && r.name[0] != '\0');
        RS_CHECK(r.cannot_determine != nullptr && r.cannot_determine[0] != '\0');
    }
}

RS_TEST_MAIN("extract")
