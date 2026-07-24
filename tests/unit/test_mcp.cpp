// SPDX-License-Identifier: Apache-2.0
//
// MCP protocol conformance. The handler is tested directly, without a
// process, which is why it is split from the I/O loop.
#include "runtimeskeptic/server/mcp_server.hpp"

#include <string>

#include "runtimeskeptic/core/json.hpp"
#include "test_support.hpp"

using namespace rs;

namespace {

json::Value call(const std::string& line) {
    const std::string response = server::handle_mcp_message(line);
    auto parsed = json::parse(response);
    if (!parsed.ok()) return json::Value();
    return *parsed.value;
}

// Tool payloads arrive as a JSON string inside content[0].text - the
// CodeSkeptic convention. This unwraps that envelope.
json::Value tool_payload(const json::Value& response) {
    const json::Value* result = response.find("result");
    if (result == nullptr) return json::Value();
    const json::Value* content = result->find("content");
    if (content == nullptr || content->as_array().empty()) return json::Value();
    const json::Value* text = content->as_array().front().find("text");
    if (text == nullptr) return json::Value();
    auto parsed = json::parse(text->as_string());
    if (!parsed.ok()) return json::Value();
    return *parsed.value;
}

int error_code(const json::Value& response) {
    const json::Value* error = response.find("error");
    if (error == nullptr) return 0;
    const json::Value* code = error->find("code");
    return code == nullptr ? 0 : static_cast<int>(code->as_int());
}

}  // namespace

RS_TEST(initialize_returns_the_expected_handshake) {
    const auto response =
        call(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    const json::Value* result = response.find("result");
    RS_CHECK(result != nullptr);
    if (result == nullptr) return;

    RS_CHECK_EQ(result->find("protocolVersion")->as_string(),
                std::string("2024-11-05"));
    RS_CHECK(result->find("capabilities")->contains("tools"));
    RS_CHECK_EQ(result->find("serverInfo")->find("name")->as_string(),
                std::string("runtimeskeptic"));
    RS_CHECK_EQ(response.find("jsonrpc")->as_string(), std::string("2.0"));
    RS_CHECK_EQ(response.find("id")->as_int(), std::int64_t{1});
}

RS_TEST(ping_is_answered) {
    const auto response = call(R"({"jsonrpc":"2.0","id":"x","method":"ping"})");
    RS_CHECK(response.contains("result"));
    RS_CHECK_EQ(response.find("id")->as_string(), std::string("x"));
}

RS_TEST(notifications_get_no_response_at_all) {
    // Not even an error. A message without an id is a notification.
    RS_CHECK(server::handle_mcp_message(
                 R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
                 .empty());
    RS_CHECK(server::handle_mcp_message(R"({"jsonrpc":"2.0"})").empty());
}

RS_TEST(malformed_input_produces_a_parse_error_with_a_null_id) {
    const auto response = call("{not json");
    RS_CHECK_EQ(error_code(response), -32700);
    RS_CHECK(response.find("id")->is_null());
}

RS_TEST(unknown_method_and_unknown_tool_use_the_documented_codes) {
    RS_CHECK_EQ(error_code(call(R"({"jsonrpc":"2.0","id":1,"method":"nope"})")),
                -32601);
    RS_CHECK_EQ(
        error_code(call(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nope"}})")),
        -32602);
    RS_CHECK_EQ(
        error_code(call(R"({"jsonrpc":"2.0","id":1,"method":"tools/call"})")),
        -32602);
}

RS_TEST(tools_list_advertises_every_tool_with_a_schema) {
    const auto response =
        call(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    const json::Value* tools = response.find("result")->find("tools");
    RS_CHECK(tools != nullptr);
    if (tools == nullptr) return;

    const char* expected[] = {"probe_host", "check_requirement", "verify_profile",
                              "diff_profiles", "describe_findings"};
    for (const char* name : expected) {
        bool found = false;
        for (const auto& t : tools->as_array()) {
            if (t.find("name")->as_string() != name) continue;
            found = true;
            RS_CHECK_MESSAGE(!t.find("description")->as_string().empty(),
                             std::string(name) + " has no description");
            const json::Value* schema = t.find("inputSchema");
            RS_CHECK(schema != nullptr);
            if (schema != nullptr) {
                RS_CHECK_EQ(schema->find("type")->as_string(),
                            std::string("object"));
                RS_CHECK(schema->contains("properties"));
            }
        }
        RS_CHECK_MESSAGE(found, std::string("tool not advertised: ") + name);
    }
}

RS_TEST(every_advertised_property_is_a_string_type) {
    // The CodeSkeptic convention: booleans and lists cross as strings, so
    // clients that flatten arguments still work.
    const auto response =
        call(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    for (const auto& t : response.find("result")->find("tools")->as_array()) {
        const json::Value* props = t.find("inputSchema")->find("properties");
        if (props == nullptr) continue;
        for (const auto& [key, prop] : props->as_object()) {
            RS_CHECK_MESSAGE(prop.find("type")->as_string() == "string",
                             "property " + key + " is not type string");
        }
    }
}

RS_TEST(describe_findings_lists_the_registry_and_the_vocabulary) {
    const auto payload = tool_payload(call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_findings"}})"));

    RS_CHECK(payload.contains("findings"));
    RS_CHECK(payload.find("findings")->as_array().size() >= 18);
    // An agent reading a verdict needs the confidence vocabulary with it.
    RS_CHECK(payload.find("confidence_levels")->contains("PROVEN"));
    RS_CHECK(payload.find("confidence_levels")->contains("PREDICTIVE"));
    RS_CHECK(payload.find("verdicts")->contains("UNKNOWN"));
}

RS_TEST(describe_findings_can_explain_one_id) {
    const auto payload = tool_payload(call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_findings","arguments":{"id":"RS-VM-0001"}}})"));
    RS_CHECK_EQ(payload.find("finding")->find("id")->as_string(),
                std::string("RS-VM-0001"));

    RS_CHECK_EQ(
        error_code(call(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_findings","arguments":{"id":"RS-VM-9999"}}})")),
        -32602);
}

RS_TEST(check_requirement_works_entirely_inline) {
    // No filesystem: an agent that has just composed a requirement should be
    // able to evaluate it without writing anything to disk.
    const std::string request = R"({
      "jsonrpc":"2.0","id":7,"method":"tools/call",
      "params":{"name":"check_requirement","arguments":{
        "requirement_json":"{\"schema\":\"runtime-skeptic.application-requirements.v1\",\"name\":\"inline\",\"operation\":\"virtual_memory_map\",\"assumption_evidence\":\"specified_guarantee\",\"request\":{\"address\":\"0x1000\",\"size\":4096,\"exact_address_required\":true},\"assumptions\":{\"guest_host_identity_required\":true},\"failure_sink\":{\"kind\":\"fatal_assert\"}}",
        "profile_json":"{\"schema\":\"runtime-skeptic.environment-profile.v1\",\"origin\":\"measured\",\"platform\":{\"os\":\"linux\",\"process_arch\":\"x86_64\"},\"virtual_memory\":{\"anonymous_mapping_supported\":{\"value\":true,\"evidence\":\"measured_capability\"},\"min_map_address\":{\"value\":\"0x10000\",\"evidence\":\"measured_capability\"}}}"
      }}})";

    const auto payload = tool_payload(call(request));
    // The requested address is below the measured minimum: proven impossible.
    RS_CHECK_EQ(payload.find("verdict")->as_string(), std::string("UNSUPPORTED"));
    RS_CHECK_EQ(payload.find("exit_code")->as_int(), std::int64_t{1});
    RS_CHECK(payload.find("finding_count")->as_uint() >= 1);
    RS_CHECK(payload.contains("result"));
}

RS_TEST(check_requirement_surfaces_a_non_measured_profile) {
    const std::string request = R"({
      "jsonrpc":"2.0","id":8,"method":"tools/call",
      "params":{"name":"check_requirement","arguments":{
        "requirement_json":"{\"schema\":\"runtime-skeptic.application-requirements.v1\",\"operation\":\"virtual_memory_map\",\"assumption_evidence\":\"specified_guarantee\",\"request\":{\"size\":4096},\"failure_sink\":{\"kind\":\"error_return\"}}",
        "profile_json":"{\"schema\":\"runtime-skeptic.environment-profile.v1\",\"origin\":\"hand_authored_fixture\",\"platform\":{\"os\":\"macos\",\"process_arch\":\"x86_64\"},\"virtual_memory\":{}}"
      }}})";

    const auto payload = tool_payload(call(request));
    RS_CHECK(payload.contains("warning"));
    RS_CHECK(payload.find("warning")->as_string().find("not measured") !=
             std::string::npos);
    // And a profile that establishes nothing must never come out SUPPORTED.
    RS_CHECK_EQ(payload.find("verdict")->as_string(), std::string("UNKNOWN"));
}

RS_TEST(check_requirement_rejects_a_bad_document_with_a_useful_message) {
    const auto response = call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"check_requirement","arguments":{"requirement_json":"{}","profile_json":"{}"}}})");
    RS_CHECK_EQ(error_code(response), -32602);
    RS_CHECK(response.find("error")->find("message")->as_string().find(
                 "schema") != std::string::npos);
}

RS_TEST(check_requirement_reports_missing_arguments) {
    const auto response = call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"check_requirement","arguments":{}}})");
    RS_CHECK_EQ(error_code(response), -32602);
}

RS_TEST(verify_profile_counts_what_the_profile_knows) {
    const std::string request = R"({
      "jsonrpc":"2.0","id":1,"method":"tools/call",
      "params":{"name":"verify_profile","arguments":{
        "profile_json":"{\"schema\":\"runtime-skeptic.environment-profile.v1\",\"origin\":\"measured\",\"platform\":{\"os\":\"linux\",\"process_arch\":\"x86_64\"},\"virtual_memory\":{\"page_size\":{\"value\":4096,\"evidence\":\"measured_capability\"}}}"
      }}})";

    const auto payload = tool_payload(call(request));
    RS_CHECK(payload.find("valid")->as_bool());
    RS_CHECK(payload.find("canonical_form_stable")->as_bool());
    RS_CHECK_EQ(payload.find("facts_known")->as_uint(), std::uint64_t{1});
    RS_CHECK(payload.find("facts_unknown")->as_uint() > 0);
    RS_CHECK(payload.find("profile_id")->as_string().rfind("sha256:", 0) == 0);
}

RS_TEST(verify_profile_reports_invalidity_as_data_not_as_an_rpc_error) {
    // A malformed profile is a finding about the profile, not a protocol
    // failure - the agent should get a structured answer it can act on.
    const auto payload = tool_payload(call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"verify_profile","arguments":{"profile_json":"{\"schema\":\"wrong\"}"}}})"));
    RS_CHECK(!payload.find("valid")->as_bool());
    RS_CHECK(!payload.find("error")->as_string().empty());
}

RS_TEST(diff_profiles_detects_a_changed_fact) {
    const std::string same =
        R"({\"schema\":\"runtime-skeptic.environment-profile.v1\",\"origin\":\"measured\",\"platform\":{\"os\":\"linux\",\"process_arch\":\"x86_64\"},\"virtual_memory\":{\"page_size\":{\"value\":4096,\"evidence\":\"measured_capability\"}}})";
    const std::string changed =
        R"({\"schema\":\"runtime-skeptic.environment-profile.v1\",\"origin\":\"measured\",\"platform\":{\"os\":\"linux\",\"process_arch\":\"x86_64\"},\"virtual_memory\":{\"page_size\":{\"value\":16384,\"evidence\":\"measured_capability\"}}})";

    const std::string identical_request =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"diff_profiles","arguments":{"a_json":")" +
        same + R"(","b_json":")" + same + R"("}}})";
    const auto identical = tool_payload(call(identical_request));
    RS_CHECK(identical.find("identical")->as_bool());

    const std::string differing_request =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"diff_profiles","arguments":{"a_json":")" +
        same + R"(","b_json":")" + changed + R"("}}})";
    const auto differing = tool_payload(call(differing_request));
    RS_CHECK(!differing.find("identical")->as_bool());
    RS_CHECK(differing.find("difference_count")->as_uint() >= 1);
    RS_CHECK(differing.find("a_profile_id")->as_string() !=
             differing.find("b_profile_id")->as_string());
}

RS_TEST(probe_host_returns_a_profile_and_never_lies_about_the_platform) {
    const auto payload = tool_payload(call(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"probe_host","arguments":{"scan_address_space":"false","faulting_tests":"false"}}})"));

    RS_CHECK(payload.contains("profile"));
    RS_CHECK(payload.find("profile_id")->as_string().rfind("sha256:", 0) == 0);
    // If this build has no probe for the platform, it must say so loudly.
    if (!payload.find("implemented")->as_bool()) {
        RS_CHECK(payload.contains("warning"));
    }
}

RS_TEST(responses_are_deterministic) {
    // Agents cache and diff these. Two identical requests must produce
    // byte-identical responses.
    const std::string request =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    RS_CHECK_EQ(server::handle_mcp_message(request),
                server::handle_mcp_message(request));
}

RS_TEST(responses_are_single_line) {
    // Newline-delimited framing: a response containing a raw newline would
    // desynchronize the stream.
    for (const char* request :
         {R"({"jsonrpc":"2.0","id":1,"method":"initialize"})",
          R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})",
          R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_findings"}})"}) {
        const std::string response = server::handle_mcp_message(request);
        RS_CHECK_MESSAGE(response.find('\n') == std::string::npos,
                         std::string("response contains a raw newline: ") +
                             request);
    }
}

RS_TEST_MAIN("mcp")
