// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/server/mcp_server.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/probe/vm_probe.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/version.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"

namespace rs::server {
namespace {

using json::Value;

Value make_response(const Value& id, Value result) {
    Value v = Value::object();
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["result"] = std::move(result);
    return v;
}

Value make_error(const Value& id, int code, const std::string& message) {
    Value error = Value::object();
    error["code"] = static_cast<long long>(code);
    error["message"] = message;
    Value v = Value::object();
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["error"] = std::move(error);
    return v;
}

std::string serialize(const Value& value) {
    // Canonical form: the same bytes on every platform, which matters because
    // agents cache and diff these responses.
    auto out = json::serialize_canonical(value);
    return out.value_or("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":"
                        "{\"code\":-32603,\"message\":\"unserializable "
                        "result\"}}");
}

// A tool result: the payload serialized as a JSON string inside
// content[0].text. `isError` is deliberately never set - findings mean the
// analysis succeeded and has something to say.
Value make_tool_result(const Value& id, Value payload) {
    Value text = Value::object();
    text["type"] = "text";
    text["text"] = serialize(payload);

    Value content = Value::array();
    content.push_back(std::move(text));

    Value result = Value::object();
    result["content"] = std::move(content);
    return make_response(id, std::move(result));
}

const std::string* string_arg(const Value* args, const char* key) {
    if (args == nullptr) return nullptr;
    const Value* node = args->find(key);
    if (node == nullptr || !node->is_string()) return nullptr;
    if (node->as_string().empty()) return nullptr;
    return &node->as_string();
}

bool bool_arg(const Value* args, const char* key, bool fallback) {
    const std::string* raw = string_arg(args, key);
    if (raw == nullptr) return fallback;
    return *raw == "true" || *raw == "1" || *raw == "yes";
}

// Documents may arrive either as a path or inline. Inline matters for agents
// that have just written a requirement and do not want to touch the disk.
struct DocumentSource {
    Value value;
    std::string origin;
    std::string error;

    bool ok() const { return error.empty(); }
};

DocumentSource load_document(const Value* args, const char* path_key,
                             const char* inline_key, const char* what) {
    DocumentSource out;
    const std::string* inline_text = string_arg(args, inline_key);
    const std::string* path = string_arg(args, path_key);

    std::string text;
    if (inline_text != nullptr) {
        text = *inline_text;
        out.origin = std::string("inline ") + inline_key;
    } else if (path != nullptr) {
        std::string io_error;
        auto contents = io::read_file(*path, io_error);
        if (!contents) {
            out.error = io_error;
            return out;
        }
        text = std::move(*contents);
        out.origin = *path;
    } else {
        out.error = std::string("missing ") + path_key + " or " + inline_key +
                    " (" + what + ")";
        return out;
    }

    auto parsed = json::parse(text);
    if (!parsed.ok()) {
        out.error = out.origin + " is not valid JSON: " +
                    parsed.error->to_string();
        return out;
    }
    out.value = std::move(*parsed.value);
    return out;
}

Value string_array(const std::vector<std::string>& items) {
    Value arr = Value::array();
    for (const auto& s : items) arr.push_back(Value(s));
    return arr;
}

// ---------------------------------------------------------------------------
// Tool schemas
// ---------------------------------------------------------------------------
//
// Following CodeSkeptic: every property is type "string", including booleans
// and lists. Agents pass "true"/"false" and comma-separated values. It costs a
// little elegance and buys compatibility with clients that flatten arguments.

Value property(const char* description) {
    Value v = Value::object();
    v["type"] = "string";
    v["description"] = description;
    return v;
}

Value tool(const char* name, const char* description, Value properties,
           std::vector<std::string> required) {
    Value schema = Value::object();
    schema["type"] = "object";
    schema["properties"] = std::move(properties);
    schema["required"] = string_array(required);

    Value v = Value::object();
    v["name"] = name;
    v["description"] = description;
    v["inputSchema"] = std::move(schema);
    return v;
}

Value handle_tools_list(const Value& id) {
    Value tools = Value::array();

    {
        Value props = Value::object();
        props["output"] = property(
            "Write the profile to this path. Omit to receive it in the "
            "response only.");
        props["name"] = property(
            "Human label stored in the profile. Not part of profile_id, so "
            "renaming never changes identity.");
        props["scan_address_space"] = property(
            "\"false\" to skip the address-space scan. Default \"true\".");
        props["faulting_tests"] = property(
            "\"false\" to skip tests that fault in a forked child. Default "
            "\"true\".");
        tools.push_back(tool(
            "probe_host",
            "Measure this machine's virtual-memory behaviour and return an "
            "environment profile. Every capability carries an evidence class; "
            "anything not measured is reported as unknown rather than guessed. "
            "Only Linux is implemented in v0.1 - other platforms return a "
            "schema-valid profile in which every fact is unknown.",
            std::move(props), {}));
    }

    {
        Value props = Value::object();
        props["requirement_path"] = property(
            "Path to a runtime-skeptic.application-requirements.v1 document.");
        props["requirement_json"] = property(
            "The requirement document inline, as a JSON string. Use instead of "
            "requirement_path.");
        props["profile_path"] = property(
            "Path to an environment profile from probe_host.");
        props["profile_json"] = property(
            "The profile inline, as a JSON string. Use instead of "
            "profile_path.");
        props["format"] = property(
            "\"json\" (default), \"markdown\" or \"text\". json returns the "
            "structured result; the others return a rendered report.");
        props["report_unknowns"] = property(
            "\"false\" to suppress informational findings about unestablished "
            "facts. The verdict is unaffected. Default \"true\".");
        tools.push_back(tool(
            "check_requirement",
            "Answer whether a host can satisfy an application's virtual-memory "
            "requirement. Returns a verdict (SUPPORTED / "
            "CONDITIONALLY_SUPPORTED / UNKNOWN / UNSUPPORTED), findings with "
            "cross-layer evidence chains, remediation classes, and the "
            "superficial fixes that provably will not work. A finding is never "
            "labelled more confidently than its weakest supporting fact.",
            std::move(props), {}));
    }

    {
        Value props = Value::object();
        props["profile_path"] = property("Path to the profile to validate.");
        props["profile_json"] = property("The profile inline, as a JSON string.");
        tools.push_back(tool(
            "verify_profile",
            "Validate a profile against its schema and report how much it "
            "actually knows: the profile_id, whether its canonical form is "
            "stable, and how many facts are established versus unknown. Use "
            "this before trusting any verdict derived from a profile.",
            std::move(props), {}));
    }

    {
        Value props = Value::object();
        props["a_path"] = property("Path to the first profile.");
        props["b_path"] = property("Path to the second profile.");
        props["a_json"] = property("The first profile inline.");
        props["b_json"] = property("The second profile inline.");
        tools.push_back(tool(
            "diff_profiles",
            "Compare the facts of two environment profiles. Run metadata "
            "(timestamps, run ids) is excluded, so a non-empty diff means the "
            "platform's measured behaviour actually changed - the check behind "
            "'did this kernel upgrade break us?'.",
            std::move(props), {}));
    }

    {
        Value props = Value::object();
        props["id"] = property(
            "A finding id such as \"RS-VM-0001\". Omit to list the whole "
            "registry.");
        tools.push_back(tool(
            "describe_findings",
            "List the finding registry, or explain one finding id. Use this to "
            "learn the vocabulary before writing a requirement document, or to "
            "interpret a verdict.",
            std::move(props), {}));
    }

    Value result = Value::object();
    result["tools"] = std::move(tools);
    return make_response(id, std::move(result));
}

// ---------------------------------------------------------------------------
// Tool implementations
// ---------------------------------------------------------------------------

Value run_probe_host(const Value& id, const Value* args) {
    probe::Options options;
    options.scan_address_space = bool_arg(args, "scan_address_space", true);
    options.run_faulting_tests = bool_arg(args, "faulting_tests", true);

    probe::Result result = probe::probe_virtual_memory(options);
    result.profile.run.tool_version = kToolVersion;
    if (const std::string* name = string_arg(args, "name"); name != nullptr) {
        result.profile.profile_name = *name;
    } else {
        result.profile.profile_name =
            probe::probe_platform_name() + "-" +
            std::string(vm::to_string(result.profile.platform.process_arch));
    }

    Value payload = Value::object();
    payload["implemented"] = result.implemented;
    payload["platform"] = probe::probe_platform_name();
    payload["profile_id"] = result.profile.profile_id();
    payload["profile"] = result.profile.to_json();

    if (const std::string* output = string_arg(args, "output");
        output != nullptr) {
        std::string error;
        const std::string text = json::serialize_pretty(result.profile.to_json());
        if (io::write_file(*output, text, error)) {
            payload["written_to"] = *output;
        } else {
            payload["write_error"] = error;
        }
    }

    if (!result.implemented) {
        payload["warning"] =
            std::string("No probe is implemented for ") +
            probe::probe_platform_name() +
            " in v0.1. Every fact in this profile is unknown. It is "
            "schema-valid but it is NOT evidence about this machine.";
    }
    return make_tool_result(id, std::move(payload));
}

Value run_check_requirement(const Value& id, const Value* args) {
    DocumentSource requirement_doc = load_document(
        args, "requirement_path", "requirement_json", "the application requirement");
    if (!requirement_doc.ok()) {
        return make_error(id, -32602, requirement_doc.error);
    }
    DocumentSource profile_doc =
        load_document(args, "profile_path", "profile_json", "the host profile");
    if (!profile_doc.ok()) {
        return make_error(id, -32602, profile_doc.error);
    }

    std::string error;
    auto requirement = vm::Requirement::from_json(requirement_doc.value, error);
    if (!requirement) {
        return make_error(id, -32602,
                          "requirement (" + requirement_doc.origin +
                              ") is invalid: " + error);
    }
    auto profile = vm::EnvironmentProfile::from_json(profile_doc.value, error);
    if (!profile) {
        return make_error(id, -32602,
                          "profile (" + profile_doc.origin +
                              ") is invalid: " + error);
    }

    vm::AnalysisOptions options;
    options.report_unknowns = bool_arg(args, "report_unknowns", true);
    const vm::AnalysisResult analysis =
        vm::analyze(*requirement, *profile, options);

    const std::string* format = string_arg(args, "format");
    const std::string chosen = format == nullptr ? "json" : *format;

    Value payload = Value::object();
    payload["verdict"] = std::string(rs::to_string(analysis.overall));
    payload["exit_code"] =
        static_cast<long long>(reports::exit_code_for(analysis.overall));
    payload["finding_count"] =
        static_cast<unsigned long long>(analysis.findings.size());
    payload["profile_origin"] = std::string(vm::to_string(profile->origin));

    if (chosen == "markdown") {
        payload["report"] =
            reports::render_markdown(analysis, *requirement, *profile);
    } else if (chosen == "text") {
        payload["report"] = reports::render_text(analysis, *requirement, *profile);
    } else {
        payload["result"] = analysis.to_json();
    }

    if (profile->origin != vm::ProfileOrigin::Measured) {
        payload["warning"] =
            "This verdict came from a profile whose origin is '" +
            std::string(vm::to_string(profile->origin)) +
            "'. Its facts were not measured on a real host. Run probe_host on "
            "the target machine before acting on this.";
    }
    return make_tool_result(id, std::move(payload));
}

Value run_verify_profile(const Value& id, const Value* args) {
    DocumentSource doc =
        load_document(args, "profile_path", "profile_json", "the host profile");
    if (!doc.ok()) return make_error(id, -32602, doc.error);

    std::string error;
    auto profile = vm::EnvironmentProfile::from_json(doc.value, error);
    if (!profile) {
        Value payload = Value::object();
        payload["valid"] = false;
        payload["error"] = error;
        payload["source"] = doc.origin;
        return make_tool_result(id, std::move(payload));
    }

    // Count established versus unknown facts. A profile is only as useful as
    // the number of questions it can answer.
    std::size_t total = 0;
    std::size_t unknown = 0;
    std::function<void(const Value&)> walk = [&](const Value& v) {
        if (v.is_object()) {
            const Value* evidence = v.find("evidence");
            if (evidence != nullptr && evidence->is_string() &&
                v.contains("value")) {
                ++total;
                if (evidence->as_string() == "unknown") ++unknown;
            }
            for (const auto& [key, child] : v.as_object()) {
                (void)key;
                walk(child);
            }
            return;
        }
        if (v.is_array()) {
            for (const auto& child : v.as_array()) walk(child);
        }
    };
    walk(profile->facts_json());

    auto first = json::serialize_canonical(profile->to_json());
    bool stable = false;
    if (first) {
        auto reparsed = json::parse(*first);
        if (reparsed.ok()) {
            auto second = json::serialize_canonical(*reparsed.value);
            stable = second.has_value() && *second == *first;
        }
    }

    Value payload = Value::object();
    payload["valid"] = true;
    payload["source"] = doc.origin;
    payload["profile_id"] = profile->profile_id();
    payload["profile_name"] = profile->profile_name;
    payload["origin"] = std::string(vm::to_string(profile->origin));
    payload["os"] = std::string(vm::to_string(profile->platform.os));
    payload["process_arch"] =
        std::string(vm::to_string(profile->platform.process_arch));
    payload["translation_mode"] =
        std::string(vm::to_string(profile->platform.translation_mode));
    payload["canonical_form_stable"] = stable;
    payload["facts_total"] = static_cast<unsigned long long>(total);
    payload["facts_known"] = static_cast<unsigned long long>(total - unknown);
    payload["facts_unknown"] = static_cast<unsigned long long>(unknown);
    payload["unavailable_range_count"] =
        static_cast<unsigned long long>(profile->vm.unavailable_ranges.size());
    payload["available_range_count"] =
        static_cast<unsigned long long>(profile->vm.available_ranges.size());
    payload["warnings"] = string_array(profile->run.warnings);
    if (profile->origin != vm::ProfileOrigin::Measured) {
        payload["warning"] =
            "Origin is '" + std::string(vm::to_string(profile->origin)) +
            "'. Findings derived from this profile are not evidence about a "
            "real host.";
    }
    return make_tool_result(id, std::move(payload));
}

void diff_values(const Value& a, const Value& b, const std::string& path,
                 std::vector<std::string>& out) {
    if (a.is_object() && b.is_object()) {
        const auto& oa = a.as_object();
        const auto& ob = b.as_object();
        auto ia = oa.begin();
        auto ib = ob.begin();
        while (ia != oa.end() || ib != ob.end()) {
            if (ib == ob.end() || (ia != oa.end() && ia->first < ib->first)) {
                out.push_back(path + "/" + ia->first + ": only in A");
                ++ia;
            } else if (ia == oa.end() || ib->first < ia->first) {
                out.push_back(path + "/" + ib->first + ": only in B");
                ++ib;
            } else {
                diff_values(ia->second, ib->second, path + "/" + ia->first, out);
                ++ia;
                ++ib;
            }
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        const auto& aa = a.as_array();
        const auto& ab = b.as_array();
        const std::size_t n = aa.size() < ab.size() ? aa.size() : ab.size();
        for (std::size_t i = 0; i < n; ++i) {
            diff_values(aa[i], ab[i], path + "/" + std::to_string(i), out);
        }
        for (std::size_t i = n; i < aa.size(); ++i) {
            out.push_back(path + "/" + std::to_string(i) + ": only in A");
        }
        for (std::size_t i = n; i < ab.size(); ++i) {
            out.push_back(path + "/" + std::to_string(i) + ": only in B");
        }
        return;
    }
    if (!(a == b)) {
        auto render = [](const Value& v) {
            auto canonical = json::serialize_canonical(v);
            return canonical ? *canonical : std::string("(uncanonicalizable)");
        };
        out.push_back(path + ": " + render(a) + " -> " + render(b));
    }
}

Value run_diff_profiles(const Value& id, const Value* args) {
    DocumentSource a_doc = load_document(args, "a_path", "a_json", "profile A");
    if (!a_doc.ok()) return make_error(id, -32602, a_doc.error);
    DocumentSource b_doc = load_document(args, "b_path", "b_json", "profile B");
    if (!b_doc.ok()) return make_error(id, -32602, b_doc.error);

    std::string error;
    auto a = vm::EnvironmentProfile::from_json(a_doc.value, error);
    if (!a) {
        return make_error(id, -32602, "profile A is invalid: " + error);
    }
    auto b = vm::EnvironmentProfile::from_json(b_doc.value, error);
    if (!b) {
        return make_error(id, -32602, "profile B is invalid: " + error);
    }

    std::vector<std::string> differences;
    diff_values(a->facts_json(), b->facts_json(), "", differences);

    Value payload = Value::object();
    payload["a_profile_id"] = a->profile_id();
    payload["b_profile_id"] = b->profile_id();
    payload["identical"] = differences.empty();
    payload["difference_count"] =
        static_cast<unsigned long long>(differences.size());
    payload["differences"] = string_array(differences);
    if (differences.empty()) {
        payload["conclusion"] =
            "The platform behaviour these two profiles describe is the same.";
    }
    return make_tool_result(id, std::move(payload));
}

Value run_describe_findings(const Value& id, const Value* args) {
    const std::string* wanted = string_arg(args, "id");

    Value payload = Value::object();
    if (wanted != nullptr) {
        const vm::FindingDefinition* def = vm::find_definition(*wanted);
        if (def == nullptr) {
            return make_error(id, -32602, "unknown finding id: " + *wanted);
        }
        Value entry = Value::object();
        entry["id"] = std::string(def->id);
        entry["title"] = std::string(def->title);
        entry["default_severity"] =
            std::string(rs::to_string(def->default_severity));
        entry["summary"] = std::string(def->summary);
        payload["finding"] = std::move(entry);
        return make_tool_result(id, std::move(payload));
    }

    Value entries = Value::array();
    for (const auto& def : vm::finding_registry()) {
        Value entry = Value::object();
        entry["id"] = std::string(def.id);
        entry["title"] = std::string(def.title);
        entry["default_severity"] =
            std::string(rs::to_string(def.default_severity));
        entry["summary"] = std::string(def.summary);
        entries.push_back(std::move(entry));
    }
    payload["findings"] = std::move(entries);

    // The evidence vocabulary travels with the registry: an agent that has
    // just read a finding needs to know what PROVEN and PREDICTIVE mean here,
    // and that they are not interchangeable.
    Value confidence = Value::object();
    confidence["PROVEN"] =
        "The constraints are unsatisfiable. Requires a specified guarantee or "
        "a measured capability.";
    confidence["COUNTEREXAMPLE"] =
        "A platform-legal outcome reaches a failure sink. It may not have "
        "happened yet.";
    confidence["OBSERVED_INVARIANT"] =
        "An invariant held across recorded traces and has now changed. "
        "Evidence, not proof.";
    confidence["PREDICTIVE"] = "A trend or heuristic suggests future failure.";
    confidence["HYPOTHESIS"] =
        "Plausible, incomplete evidence. Never fails CI by default.";
    payload["confidence_levels"] = std::move(confidence);

    Value verdicts = Value::object();
    verdicts["SUPPORTED"] = "exit code 0";
    verdicts["UNSUPPORTED"] = "exit code 1";
    verdicts["CONDITIONALLY_SUPPORTED"] = "exit code 2";
    verdicts["UNKNOWN"] =
        "exit code 3. Outranks CONDITIONALLY_SUPPORTED in aggregation: if a "
        "relevant fact was never established, calling the request conditional "
        "would falsely imply the conditions are known.";
    payload["verdicts"] = std::move(verdicts);
    return make_tool_result(id, std::move(payload));
}

Value handle_tools_call(const Value& id, const Value* params) {
    if (params == nullptr) return make_error(id, -32602, "missing params");
    const Value* name_node = params->find("name");
    if (name_node == nullptr || !name_node->is_string()) {
        return make_error(id, -32602, "missing tool name");
    }
    const std::string& name = name_node->as_string();
    const Value* args = params->find("arguments");

    if (name == "probe_host") return run_probe_host(id, args);
    if (name == "check_requirement") return run_check_requirement(id, args);
    if (name == "verify_profile") return run_verify_profile(id, args);
    if (name == "diff_profiles") return run_diff_profiles(id, args);
    if (name == "describe_findings") return run_describe_findings(id, args);
    return make_error(id, -32602, "unknown tool: " + name);
}

Value handle_initialize(const Value& id) {
    Value tools = Value::object();
    Value capabilities = Value::object();
    capabilities["tools"] = std::move(tools);

    Value server_info = Value::object();
    server_info["name"] = kServerName;
    server_info["version"] = std::string(kToolVersion);

    Value result = Value::object();
    result["protocolVersion"] = kProtocolVersion;
    result["capabilities"] = std::move(capabilities);
    result["serverInfo"] = std::move(server_info);
    return make_response(id, std::move(result));
}

}  // namespace

std::string handle_mcp_message(const std::string& line) {
    auto parsed = json::parse(line);
    if (!parsed.ok()) {
        return serialize(make_error(Value(), -32700,
                                    "parse error: " + parsed.error->to_string()));
    }
    const Value& message = *parsed.value;
    if (!message.is_object()) {
        return serialize(make_error(Value(), -32600, "message must be an object"));
    }

    const Value* id_node = message.find("id");
    // A message with no id is a notification. The protocol says notifications
    // get no response at all - not even an error.
    const bool is_notification = id_node == nullptr;
    const Value id = id_node != nullptr ? *id_node : Value();

    const Value* method_node = message.find("method");
    if (method_node == nullptr || !method_node->is_string()) {
        if (is_notification) return {};
        return serialize(make_error(id, -32600, "missing method"));
    }
    if (is_notification) return {};

    const std::string& method = method_node->as_string();
    if (method == "initialize") return serialize(handle_initialize(id));
    if (method == "ping") return serialize(make_response(id, Value::object()));
    if (method == "tools/list") return serialize(handle_tools_list(id));
    if (method == "tools/call") {
        return serialize(handle_tools_call(id, message.find("params")));
    }
    return serialize(make_error(id, -32601, "method not found: " + method));
}

int run_mcp_server() {
#if defined(_WIN32)
    // Windows text-mode stdio would expand "\n" to "\r\n" on write and leave
    // stray '\r's in reads. Binary mode keeps the frames byte-exact.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        // Tolerate CRLF-framing clients on every platform: getline splits at
        // '\n', so a client's "\r\n" leaves a trailing '\r'.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::string response = handle_mcp_message(line);
        if (!response.empty()) {
            std::cout << response << "\n" << std::flush;
        }
    }
    return 0;
}

}  // namespace rs::server
