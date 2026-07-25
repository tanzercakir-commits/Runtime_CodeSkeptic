// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/extract/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "runtimeskeptic/core/sha256.hpp"

namespace rs::extract {
namespace {

// ---------------------------------------------------------------------------
// Text helpers. Hand-written rather than std::regex: the patterns are simple,
// and a scanner that is slow on a large tree does not get run.
// ---------------------------------------------------------------------------

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Whole-word search, so PROT_EXEC does not match MY_PROT_EXECUTE and `mmap`
// does not match `my_mmap_wrapper`.
bool contains_word(const std::string& hay, const std::string& needle) {
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        const bool left_ok = pos == 0 || !is_ident_char(hay[pos - 1]);
        const std::size_t end = pos + needle.size();
        const bool right_ok = end >= hay.size() || !is_ident_char(hay[end]);
        if (left_ok && right_ok) return true;
        pos = end;
    }
    return false;
}

std::string strip_comment(const std::string& line) {
    const std::size_t slashes = line.find("//");
    return slashes == std::string::npos ? line : line.substr(0, slashes);
}

// The first hex or decimal literal after `from`. Returns false when there is
// none, which is the common and important case: an address held in a variable
// is exactly what this scanner cannot follow.
bool first_integer_literal(const std::string& s, std::size_t from,
                           std::uint64_t& out, std::string& text) {
    for (std::size_t i = from; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) continue;
        if (i > 0 && is_ident_char(s[i - 1])) continue;  // part of a name
        std::size_t j = i;
        int base = 10;
        if (s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            base = 16;
            j = i + 2;
            while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j]))) ++j;
        } else {
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        }
        text = s.substr(i, j - i);
        out = std::strtoull(text.c_str(), nullptr, base);
        return true;
    }
    return false;
}

// Best-effort enclosing function name: the nearest preceding line that looks
// like a definition. Wrong sometimes, and labelled as best-effort wherever it
// is used, because a wrong source location in a report is worse than none.
std::string enclosing_symbol(const std::vector<std::string>& lines,
                             std::size_t index) {
    for (std::size_t k = index + 1; k-- > 0;) {
        const std::string& l = lines[k];
        if (l.empty() || l[0] == ' ' || l[0] == '\t' || l[0] == '#') continue;
        const std::size_t paren = l.find('(');
        if (paren == std::string::npos || paren == 0) continue;
        if (l.find(';') != std::string::npos) continue;   // a declaration
        std::size_t end = paren;
        while (end > 0 && !is_ident_char(l[end - 1])) --end;
        std::size_t start = end;
        while (start > 0 && is_ident_char(l[start - 1])) --start;
        if (start < end) return l.substr(start, end - start);
    }
    return {};
}

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// Failure sink. What the program does when the call fails is not something the
// scanner can know in general - it is a control-flow question - so this looks
// only for an unmistakable marker within a few lines and reports `unknown`
// otherwise. `unknown` is the right answer far more often than not.
// ---------------------------------------------------------------------------
vm::FailureSinkKind find_sink(const std::vector<std::string>& lines,
                              std::size_t from, std::size_t window,
                              std::size_t& sink_line) {
    const std::size_t last = std::min(lines.size(), from + window);
    for (std::size_t i = from; i < last; ++i) {
        const std::string l = strip_comment(lines[i]);
        if (contains_word(l, "assert") || contains_word(l, "ASSERT") ||
            contains_word(l, "ASSERT_MSG") || contains_word(l, "UNREACHABLE")) {
            sink_line = i + 1;
            return vm::FailureSinkKind::FatalAssert;
        }
        if (contains_word(l, "abort") || contains_word(l, "exit") ||
            contains_word(l, "_exit") || contains_word(l, "panic")) {
            sink_line = i + 1;
            return vm::FailureSinkKind::ProcessExit;
        }
        if (contains_word(l, "return") &&
            (contains_word(l, "NULL") || contains_word(l, "nullptr") ||
             l.find("-1") != std::string::npos || contains_word(l, "false"))) {
            sink_line = i + 1;
            return vm::FailureSinkKind::ErrorReturn;
        }
    }
    return vm::FailureSinkKind::Unknown;
}

vm::SourceLocation loc(const std::string& file, std::size_t line,
                       const std::string& symbol) {
    vm::SourceLocation s;
    s.file = file;
    s.line = line;
    s.symbol = symbol;
    return s;
}

}  // namespace

std::vector<RecognizerInfo> recognizers() {
    return {
        {"mmap_fixed_literal",
         "a call to mmap/mach_vm_allocate carrying MAP_FIXED or "
         "MAP_FIXED_NOREPLACE together with a literal address",
         "whether the address is reached on every path, what the size means "
         "when it is not a literal, and whether a translation layer exists"},
        {"mmap_write_execute",
         "a mapping call requesting PROT_WRITE and PROT_EXEC at once",
         "whether the protection is later narrowed, and whether the platform "
         "path taken at build time is the one scanned"},
        {"mprotect_to_execute",
         "an mprotect adding PROT_EXEC without PROT_WRITE, the write-then-"
         "execute idiom",
         "whether the same pages were previously writable - the scanner sees "
         "one call, not the sequence"},
        {"hardcoded_page_size",
         "a #define of a page-size constant, e.g. LJ_PAGESIZE 16384",
         "whether the constant is compared against the host page size at all, "
         "and in which direction"},
        {"retry_around_mapping",
         "a bounded loop whose body contains a mapping call",
         "whether the retry changes anything between attempts, which is what "
         "decides if retrying is futile"},
    };
}

ScanReport scan_source(const std::string& path, const std::string& text,
                       const ScanOptions& options) {
    ScanReport report;
    report.files_scanned.push_back(path);

    std::vector<std::string> lines;
    {
        std::string current;
        for (char c : text) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else if (c != '\r') {
                current.push_back(c);
            }
        }
        lines.push_back(current);
    }

    // Tracks the innermost bounded loop, so a mapping call inside one can be
    // reported as retried. Deliberately shallow: one level, literal bound only.
    std::size_t loop_open_line = 0;
    std::uint64_t loop_bound = 0;
    bool in_loop = false;
    int loop_depth = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string raw = lines[i];
        // A real mapping call does not fit on one line. The first version of
        // this scanner read line by line and missed EVERY MAP_FIXED site in the
        // test fixture, including the GTA V pattern the whole project is built
        // around, because the flag sat on the continuation line.
        //
        // So join forward until the parentheses balance. Bounded to a few
        // lines: an unbalanced paren must not swallow the rest of the file.
        std::string line = strip_comment(raw);
        {
            int depth = 0;
            for (char c : line) {
                if (c == '(') ++depth;
                if (c == ')') --depth;
            }
            std::size_t j = i;
            while (depth > 0 && j + 1 < lines.size() && j - i < 6) {
                ++j;
                const std::string more = strip_comment(lines[j]);
                line += " " + trim(more);
                for (char c : more) {
                    if (c == '(') ++depth;
                    if (c == ')') --depth;
                }
            }
        }
        if (trim(line).empty()) continue;

        // --- loop tracking -------------------------------------------------
        if (!in_loop && (contains_word(line, "for") || contains_word(line, "while"))) {
            // The LARGEST literal in the header, not the first. `for (int i =
            // 0; i < 30; i++)` opens with 0, and taking the first literal made
            // every retry loop in the test fixture invisible - including
            // LuaJIT's thirty attempts, which is the exact shape RS-VM-0015
            // exists to judge.
            std::uint64_t bound = 0;
            std::string bound_text;
            const std::size_t paren = line.find('(');
            if (paren != std::string::npos) {
                std::size_t at = paren;
                std::uint64_t value = 0;
                std::string value_text;
                while (first_integer_literal(line, at, value, value_text)) {
                    if (value > bound) { bound = value; bound_text = value_text; }
                    const std::size_t next = line.find(value_text, at);
                    if (next == std::string::npos) break;
                    at = next + value_text.size();
                }
            }
            if (bound > 1 && bound < 100000) {
                in_loop = true;
                loop_open_line = i + 1;
                loop_bound = bound;
                loop_depth = 0;
            }
        }
        if (in_loop) {
            for (char c : line) {
                if (c == '{') ++loop_depth;
                if (c == '}') --loop_depth;
            }
            if (loop_depth < 0) in_loop = false;
        }

        const std::string symbol = enclosing_symbol(lines, i);

        // --- hardcoded page size -------------------------------------------
        // Substring, not whole-word: the names that matter are SUFFIXES of a
        // macro - LJ_PAGESIZE, MI_SEGMENT_PAGE_SIZE - and `_` is an identifier
        // character, so a whole-word test rejects every one of them. It
        // rejected LuaJIT's, which is the constant this recognizer was written
        // for.
        if (line.find("#define") != std::string::npos &&
            (line.find("PAGESIZE") != std::string::npos ||
             line.find("PAGE_SIZE") != std::string::npos)) {
            std::uint64_t value = 0;
            std::string value_text;
            const std::size_t after = line.find("define") + 6;
            if (first_integer_literal(line, after, value, value_text) &&
                value >= 512 && value <= (1u << 30) && (value & (value - 1)) == 0) {
                Candidate c;
                c.recognizer = "hardcoded_page_size";
                c.file = path;
                c.line = i + 1;
                c.quote = trim(raw);
                vm::Requirement& r = c.requirement;
                r.name = "build-time page-size constant " + value_text +
                         " assumed to match the host";
                r.component = options.component;
                r.operation = vm::OperationKind::VirtualMemoryMap;
                r.request.size = value;
                r.request.required_page_size = value;
                // Deliberately Equal, the strictest reading, because the
                // scanner CANNOT see the comparison. Campaign defect 4 was the
                // analyzer assuming equality when the code meant at_most; here
                // the assumption is the extractor's and it is flagged rather
                // than hidden.
                r.request.required_page_size_relation = vm::SizeRelation::Equal;
                r.request.protection = {true, true, false};
                r.assumption_evidence = EvidenceClass::StaticallyInferred;
                r.source_locations.push_back(loc(path, i + 1, symbol));
                r.failure_sink.kind = vm::FailureSinkKind::Unknown;
                r.extraction_limitations = {
                    "the relation is recorded as `equal`, the strictest "
                    "reading, because the scanner sees the constant but not "
                    "the comparison. If the code tests `host_page > CONST` the "
                    "true relation is `at_most` and this requirement will "
                    "overstate - check the comparison before trusting the "
                    "verdict",
                    "nothing here establishes that the constant is used as a "
                    "page size at all; it was recognised by name",
                };
                report.candidates.push_back(std::move(c));
                continue;
            }
        }

        // --- mapping calls --------------------------------------------------
        const bool is_mmap = contains_word(line, "mmap") ||
                             contains_word(line, "mach_vm_allocate") ||
                             contains_word(line, "VirtualAlloc");
        const bool is_mprotect = contains_word(line, "mprotect") ||
                                 contains_word(line, "mach_vm_protect");
        if (!is_mmap && !is_mprotect) continue;

        const bool wants_exec = contains_word(line, "PROT_EXEC") ||
                                contains_word(line, "VM_PROT_EXECUTE") ||
                                contains_word(line, "PAGE_EXECUTE_READWRITE");
        const bool wants_write = contains_word(line, "PROT_WRITE") ||
                                 contains_word(line, "VM_PROT_WRITE") ||
                                 contains_word(line, "PAGE_EXECUTE_READWRITE");
        const bool fixed = contains_word(line, "MAP_FIXED") ||
                           contains_word(line, "MAP_FIXED_NOREPLACE") ||
                           contains_word(line, "VM_FLAGS_FIXED");
        const bool destructive_fixed =
            contains_word(line, "MAP_FIXED") &&
            !contains_word(line, "MAP_FIXED_NOREPLACE");

        std::size_t sink_line = 0;
        const vm::FailureSinkKind sink =
            find_sink(lines, i + 1, options.sink_search_lines, sink_line);

        Candidate c;
        c.file = path;
        c.line = i + 1;
        c.symbol = symbol;
        c.quote = trim(line);
        vm::Requirement& r = c.requirement;
        r.component = options.component.empty() ? symbol : options.component;
        r.assumption_evidence = EvidenceClass::StaticallyInferred;
        r.source_locations.push_back(loc(path, i + 1, symbol));
        r.failure_sink.kind = sink;
        if (sink_line != 0) {
            r.failure_sink.location = loc(path, sink_line, symbol);
            r.failure_sink.description =
                "recognised by a marker on line " + std::to_string(sink_line) +
                "; the scanner does not know that this line is reached only on "
                "failure";
        }
        r.request.protection = {true, wants_write, wants_exec};

        if (is_mprotect && wants_exec && !wants_write) {
            c.recognizer = "mprotect_to_execute";
            r.operation = vm::OperationKind::VirtualMemoryProtect;
            r.name = "protection changed to executable at " + path + ":" +
                     std::to_string(i + 1);
            r.request.write_then_execute = true;
            r.request.size = 4096;
            r.extraction_limitations = {
                "the scanner sees ONE call. Whether these pages were writable "
                "beforehand - which is what makes this the write-then-execute "
                "idiom rather than something else - is a sequence it cannot "
                "follow",
                "request.size is a placeholder: the length argument was not a "
                "literal at this site",
            };
            r.required_postconditions = {
                "code written before this call is executable after it"};
            report.candidates.push_back(std::move(c));
            continue;
        }

        if (!is_mmap) continue;

        std::uint64_t address = 0;
        std::string address_text;
        const std::size_t open = line.find('(');
        const bool has_literal_address =
            open != std::string::npos &&
            first_integer_literal(line, open, address, address_text) &&
            address_text.size() > 4 && address_text.rfind("0x", 0) == 0;

        // With the statement joined, the length argument is usually the next
        // literal after the address. Still a guess about argument order, and
        // labelled as one wherever it is used.
        std::uint64_t length = 0;
        std::string length_text;
        bool has_literal_length = false;
        if (has_literal_address) {
            const std::size_t after = line.find(address_text) + address_text.size();
            has_literal_length =
                first_integer_literal(line, after, length, length_text) &&
                length >= 4096;
        }

        if (fixed && has_literal_address) {
            c.recognizer = "mmap_fixed_literal";
            r.operation = vm::OperationKind::VirtualMemoryMap;
            r.name = "exact mapping at " + address_text + " (" + path + ":" +
                     std::to_string(i + 1) + ")";
            r.request.address = address;
            r.request.exact_address_required = true;
            r.request.size = has_literal_length ? length : 4096;
            r.assumptions.guest_host_identity_required = true;
            r.assumptions.translation_layer_available = false;
            r.required_postconditions = {
                "the mapping is placed at exactly " + address_text};
            r.extraction_limitations = {
                has_literal_length
                    ? "request.size " + length_text + " was read as the literal "
                      "following the address, which assumes mmap's argument "
                      "order; the scanner does not parse the call"
                    : std::string("request.size is a placeholder: the length "
                      "argument at this site was not a literal the scanner "
                      "could read"),
                "guest_host_identity_required is inferred from the presence of "
                "MAP_FIXED, not read from the program. A caller that tolerates "
                "a different address would not use MAP_FIXED, but the scanner "
                "cannot confirm the caller checks the result",
                "translation_layer_available defaults to false; set it by hand "
                "if the program has an address translation layer",
            };
            if (destructive_fixed) {
                r.extraction_limitations.push_back(
                    "the call uses the DESTRUCTIVE MAP_FIXED rather than "
                    "MAP_FIXED_NOREPLACE, so success does not imply the range "
                    "was free - it may have evicted an existing mapping");
            }
            if (in_loop) {
                r.assumptions.retries_on_failure = true;
                r.assumptions.max_retries = loop_bound;
                r.extraction_limitations.push_back(
                    "this call is inside a bounded loop opened at line " +
                    std::to_string(loop_open_line) + "; whether anything "
                    "changes between attempts is what decides if retrying is "
                    "futile, and the scanner does not know");
            }
            report.candidates.push_back(std::move(c));
            continue;
        }

        if (wants_exec && wants_write) {
            c.recognizer = "mmap_write_execute";
            r.operation = vm::OperationKind::VirtualMemoryMap;
            r.name = "writable and executable mapping at " + path + ":" +
                     std::to_string(i + 1);
            r.request.simultaneous_write_execute = true;
            r.request.size = 4096;
            r.required_postconditions = {
                "the mapping is writable and executable at the same time"};
            r.extraction_limitations = {
                "request.size is a placeholder: the length argument at this "
                "site was not a literal the scanner could read",
                "whether the protection is narrowed later is a sequence the "
                "scanner cannot follow; if it is, this is the write-then-"
                "execute idiom and not simultaneous W+X",
            };
            report.candidates.push_back(std::move(c));
            continue;
        }

        report.rejected_sites.push_back(
            path + ":" + std::to_string(i + 1) +
            " looks like a mapping call but matched no recognizer (no literal "
            "address, no executable protection): " + trim(raw));
    }

    return report;
}

json::Value to_bundle(const std::vector<ScanReport>& reports,
                      const std::string& tool_version) {
    json::Value bundle = json::Value::object();
    bundle["schema"] = std::string(vm::kRequirementBundleSchema);

    json::Value producer = json::Value::object();
    producer["tool"] = std::string("rs-extract");
    producer["version"] = tool_version;
    producer["rule"] = std::string("bounded-source-scan");
    bundle["producer"] = producer;

    json::Value requirements = json::Value::array();
    std::size_t scanned = 0;
    std::size_t rejected = 0;
    for (const auto& r : reports) {
        scanned += r.files_scanned.size();
        rejected += r.rejected_sites.size();
        for (const auto& c : r.candidates) {
            requirements.push_back(c.requirement.to_json());
        }
    }
    bundle["requirements"] = requirements;

    // The bundle carries what the scan could NOT do, because a consumer that
    // sees only the requirements would read this as a complete account of the
    // program's virtual-memory behaviour. It is not one.
    json::Value notes = json::Value::array();
    notes.push_back(std::string(
        "Produced by a bounded text scanner, not a compiler. It does not "
        "resolve macros, follow dataflow, evaluate constant expressions, or "
        "know which branch runs."));
    notes.push_back(std::string(
        "Every requirement here is a CANDIDATE for review. A pattern the "
        "scanner does not recognise produces nothing, and nothing is not "
        "evidence that the program has no such requirement."));
    notes.push_back(std::string("files scanned: " + std::to_string(scanned)));
    notes.push_back(std::string(
        "mapping calls seen but not recognised: " + std::to_string(rejected) +
        " - see rs-extract --explain for each one"));
    bundle["x_scan_notes"] = notes;
    return bundle;
}

}  // namespace rs::extract
