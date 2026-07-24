// SPDX-License-Identifier: Apache-2.0
//
// Report rendering. Three formats share one rule: a report never states a
// conclusion more strongly than the finding it came from, and it always shows
// the evidence chain that produced it (ROADMAP 8.6).
#ifndef RUNTIMESKEPTIC_REPORTS_REPORT_HPP
#define RUNTIMESKEPTIC_REPORTS_REPORT_HPP

#include <string>

#include "runtimeskeptic/vm/analyzer.hpp"
#include "runtimeskeptic/vm/profile.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::reports {

struct Options {
    bool color = false;
    // Print the full evidence chain and remediation classes for every finding.
    bool verbose = true;
    // Include informational findings (support impact UNKNOWN, severity info).
    bool include_info = true;
};

std::string render_text(const vm::AnalysisResult& result,
                        const vm::Requirement& requirement,
                        const vm::EnvironmentProfile& profile,
                        const Options& options = {});

std::string render_markdown(const vm::AnalysisResult& result,
                            const vm::Requirement& requirement,
                            const vm::EnvironmentProfile& profile,
                            const Options& options = {});

// Exit codes. Documented here because CI configurations depend on them and
// they must not drift.
//
//   0  SUPPORTED
//   1  UNSUPPORTED
//   2  CONDITIONALLY_SUPPORTED
//   3  UNKNOWN
//   64 usage error
//   65 input could not be read or did not satisfy its schema
//   70 internal error
namespace exit_code {
inline constexpr int kSupported = 0;
inline constexpr int kUnsupported = 1;
inline constexpr int kConditional = 2;
inline constexpr int kUnknown = 3;
inline constexpr int kUsage = 64;
inline constexpr int kInput = 65;
inline constexpr int kInternal = 70;
}  // namespace exit_code

int exit_code_for(SupportLevel level);

}  // namespace rs::reports

#endif  // RUNTIMESKEPTIC_REPORTS_REPORT_HPP
