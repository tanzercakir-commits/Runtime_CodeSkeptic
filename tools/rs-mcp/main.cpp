// SPDX-License-Identifier: Apache-2.0
//
//   rs-mcp [--serve]
//
// Model Context Protocol server over stdio. Registration:
//
//   { "mcpServers": {
//       "runtimeskeptic": { "command": "/path/to/rs-mcp", "args": ["--serve"] }
//   } }
//
// `--serve` is accepted rather than required, so a client that launches the
// binary bare still gets a server instead of a usage error.
#include <iostream>
#include <string>

#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/server/mcp_server.hpp"
#include "runtimeskeptic/version.hpp"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--serve") continue;
        if (arg == "--version") {
            std::cout << rs::kToolVersion << "\n";
            return 0;
        }
        if (arg == "-h" || arg == "--help") {
            std::cout <<
                R"(rs-mcp - RuntimeSkeptic as a Model Context Protocol server

USAGE
  rs-mcp [--serve]

Speaks newline-delimited JSON-RPC 2.0 on stdin/stdout, protocol version
2024-11-05. The framing and conventions match CodeSkeptic's --serve mode so
both can sit in one agent configuration.

TOOLS
  probe_host          measure this machine's virtual-memory behaviour
  check_requirement   can this host satisfy this application requirement?
  verify_profile      is this profile well-formed, and how much does it know?
  diff_profiles       did the platform's measured behaviour change?
  describe_findings   the finding registry and the confidence vocabulary

REGISTRATION
  { "mcpServers": {
      "runtimeskeptic": { "command": "rs-mcp", "args": ["--serve"] }
  } }
)";
            return 0;
        }
        std::cerr << "rs-mcp: unrecognized option '" << arg << "'\n";
        return rs::reports::exit_code::kUsage;
    }
    return rs::server::run_mcp_server();
}
