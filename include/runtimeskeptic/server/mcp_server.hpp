// SPDX-License-Identifier: Apache-2.0
//
// Model Context Protocol server.
//
// The framing, protocol version, error codes and result shape are public
// RuntimeSkeptic protocol choices. The server is standalone and requires no
// second tool or agent configuration. Specifically:
//
//   - newline-delimited JSON-RPC 2.0 on stdio, NOT Content-Length framing
//   - protocolVersion "2024-11-05", capabilities {"tools": {}}
//   - notifications receive no response at all
//   - only -32700 / -32600 / -32601 / -32602 are used
//   - a tool result is the payload SERIALIZED AS A JSON STRING inside
//     content[0].text; isError is never set, because findings are the
//     successful result of an analysis rather than an error
//
// The message handler is split from the I/O loop so it can be unit-tested
// without a process.
#ifndef RUNTIMESKEPTIC_SERVER_MCP_SERVER_HPP
#define RUNTIMESKEPTIC_SERVER_MCP_SERVER_HPP

#include <string>

namespace rs::server {

inline constexpr const char* kProtocolVersion = "2024-11-05";
inline constexpr const char* kServerName = "runtimeskeptic";

// Handles one JSON-RPC line. Returns the response line, or an empty string
// when the message was a notification and no response is owed.
std::string handle_mcp_message(const std::string& line);

// Reads newline-delimited JSON-RPC from stdin until EOF.
int run_mcp_server();

}  // namespace rs::server

#endif  // RUNTIMESKEPTIC_SERVER_MCP_SERVER_HPP
