// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <string>
#include <vector>

#include "runtimeskeptic/runtime/runtime.h"
#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

namespace {

int trigger_assertion() {
    rs_runtime_config_v1 config{};
    config.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    config.struct_size = static_cast<uint32_t>(sizeof(config));
    config.mode = RS_MONITOR_MODE_ASSERT_V1;
    config.event_capacity = 4;
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1) return 8;
    rs_vm_expectation_v1 invalid{};
    invalid.abi_version = 99;
    invalid.struct_size = static_cast<uint32_t>(sizeof(invalid));
#if defined(_WIN32)
    (void)rs_virtual_alloc_checked_v1(
        nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &invalid);
#else
#if defined(MAP_ANONYMOUS)
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    const int flags = MAP_PRIVATE | MAP_ANON;
#endif
    (void)rs_mmap_checked_v1(nullptr, 4096, PROT_READ | PROT_WRITE,
                             flags, -1, 0, &invalid);
#endif
    return 9;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    if (argc == 2 && std::string(argv[1]) == "--child") {
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
        return trigger_assertion();
    }
    std::vector<char> executable(32768, '\0');
    const DWORD length = GetModuleFileNameA(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return 1;
    std::string command = "\"" + std::string(executable.data()) +
                          "\" --child";
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) return 2;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    const BOOL got_code = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return got_code != FALSE && exit_code != 0 ? 0 : 3;
#else
    (void)argc;
    (void)argv;
    const pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) _exit(trigger_assertion());
    int status = 0;
    if (waitpid(child, &status, 0) != child) return 2;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT ? 0 : 3;
#endif
}
