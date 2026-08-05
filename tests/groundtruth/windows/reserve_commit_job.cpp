// SPDX-License-Identifier: Apache-2.0
//
// A bounded, native Windows control for reserve/commit semantics.
//
// The controller puts only a suspended child process in a Job Object with a
// 64 MiB per-process commit limit. The child reserves 256 MiB of address space
// and then tries to commit 128 MiB inside it. MEM_RESERVE consumes no commit
// charge, so the reservation must succeed; MEM_COMMIT crosses the Job Object
// limit and must fail synchronously with ERROR_COMMITMENT_LIMIT.
//
// This is deliberately self-contained and links no RuntimeSkeptic code. The
// observed Win32 error crosses a pipe back to the controller, so the test
// checks the native error itself rather than reducing every refusal to a
// generic exit status.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr SIZE_T kProcessCommitLimit = 64ull * 1024ull * 1024ull;
constexpr SIZE_T kReserveBytes = 256ull * 1024ull * 1024ull;
constexpr SIZE_T kCommitBytes = 128ull * 1024ull * 1024ull;
constexpr DWORD kWorkerTimeoutMs = 10'000;

class Handle {
public:
  Handle() = default;
  explicit Handle(HANDLE value) : value_(value) {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  ~Handle() {
    if (valid())
      ::CloseHandle(value_);
  }

  [[nodiscard]] bool valid() const {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  [[nodiscard]] HANDLE get() const { return value_; }
  void reset(HANDLE value = nullptr) {
    if (valid())
      ::CloseHandle(value_);
    value_ = value;
  }

private:
  HANDLE value_ = nullptr;
};

int fail(const char *operation, DWORD error) {
  std::fprintf(stderr, "%s failed: native_error=%lu\n", operation,
               static_cast<unsigned long>(error));
  return 1;
}

bool write_observation(bool reserve_ok, bool commit_ok,
                       const char *native_error_phase, DWORD native_error) {
  std::array<char, 256> text{};
  const int length = std::snprintf(
      text.data(), text.size(),
      "reserve_ok=%u commit_ok=%u native_error_phase=%s native_error=%lu "
      "native_error_name=%s\n",
      reserve_ok ? 1u : 0u, commit_ok ? 1u : 0u, native_error_phase,
      static_cast<unsigned long>(native_error),
      native_error == ERROR_COMMITMENT_LIMIT ? "ERROR_COMMITMENT_LIMIT"
                                             : "OTHER");
  if (length <= 0 || static_cast<std::size_t>(length) >= text.size()) {
    return false;
  }

  const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;
  return output != nullptr && output != INVALID_HANDLE_VALUE &&
         ::WriteFile(output, text.data(), static_cast<DWORD>(length), &written,
                     nullptr) != FALSE &&
         written == static_cast<DWORD>(length);
}

int worker() {
  void *const reservation =
      ::VirtualAlloc(nullptr, kReserveBytes, MEM_RESERVE, PAGE_NOACCESS);
  if (reservation == nullptr) {
    const DWORD reserve_error = ::GetLastError();
    write_observation(false, false, "reserve", reserve_error);
    return 10;
  }

  ::SetLastError(ERROR_SUCCESS);
  void *const committed =
      ::VirtualAlloc(reservation, kCommitBytes, MEM_COMMIT, PAGE_READWRITE);
  const DWORD commit_error = ::GetLastError();

  const bool reported =
      write_observation(true, committed != nullptr, "commit", commit_error);
  const bool released = ::VirtualFree(reservation, 0, MEM_RELEASE) != FALSE;

  if (!reported)
    return 11;
  if (!released)
    return 12;
  if (committed != nullptr)
    return 13;
  if (commit_error != ERROR_COMMITMENT_LIMIT)
    return 14;
  return 0;
}

std::wstring current_executable() {
  std::vector<wchar_t> path(512);
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(),
                                              static_cast<DWORD>(path.size()));
    if (length == 0)
      return {};
    if (static_cast<std::size_t>(length) < path.size() - 1) {
      return {path.data(), length};
    }
    if (path.size() >= 32768)
      return {};
    path.resize(path.size() * 2);
  }
}

std::string read_all(HANDLE pipe) {
  std::string text;
  std::array<char, 256> buffer{};
  for (;;) {
    DWORD read = 0;
    if (::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                   &read, nullptr) == FALSE) {
      if (::GetLastError() == ERROR_BROKEN_PIPE)
        break;
      return {};
    }
    if (read == 0)
      break;
    text.append(buffer.data(), read);
  }
  return text;
}

int controller() {
  Handle job(::CreateJobObjectW(nullptr, nullptr));
  if (!job.valid())
    return fail("CreateJobObjectW", ::GetLastError());

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  limits.ProcessMemoryLimit = kProcessCommitLimit;
  if (::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits)) == FALSE) {
    return fail("SetInformationJobObject", ::GetLastError());
  }

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE raw_read = nullptr;
  HANDLE raw_write = nullptr;
  if (::CreatePipe(&raw_read, &raw_write, &security, 0) == FALSE) {
    return fail("CreatePipe", ::GetLastError());
  }
  Handle read_pipe(raw_read);
  Handle write_pipe(raw_write);
  if (::SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0) ==
      FALSE) {
    return fail("SetHandleInformation", ::GetLastError());
  }

  const std::wstring executable = current_executable();
  if (executable.empty()) {
    return fail("GetModuleFileNameW", ::GetLastError());
  }
  std::wstring command = L"\"" + executable + L"\" --worker";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = write_pipe.get();
  startup.hStdError = write_pipe.get();

  PROCESS_INFORMATION process{};
  if (::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                       CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                       &startup, &process) == FALSE) {
    return fail("CreateProcessW", ::GetLastError());
  }
  Handle process_handle(process.hProcess);
  Handle thread_handle(process.hThread);

  if (::AssignProcessToJobObject(job.get(), process_handle.get()) == FALSE) {
    const DWORD error = ::GetLastError();
    ::TerminateProcess(process_handle.get(), 90);
    return fail("AssignProcessToJobObject", error);
  }

  // The controller must not keep the pipe writable: once the worker exits,
  // ReadFile then reaches EOF instead of waiting forever.
  write_pipe.reset();
  if (::ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
    const DWORD error = ::GetLastError();
    ::TerminateProcess(process_handle.get(), 91);
    return fail("ResumeThread", error);
  }
  thread_handle.reset();

  const DWORD wait =
      ::WaitForSingleObject(process_handle.get(), kWorkerTimeoutMs);
  if (wait == WAIT_TIMEOUT) {
    ::TerminateProcess(process_handle.get(), 92);
    std::fprintf(stderr, "worker timed out after %lu ms\n",
                 static_cast<unsigned long>(kWorkerTimeoutMs));
    return 1;
  }
  if (wait != WAIT_OBJECT_0) {
    return fail("WaitForSingleObject", ::GetLastError());
  }

  DWORD exit_code = 0;
  if (::GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE) {
    return fail("GetExitCodeProcess", ::GetLastError());
  }
  const std::string observation = read_all(read_pipe.get());
  std::fputs(observation.c_str(), stdout);

  const std::string expected =
      "reserve_ok=1 commit_ok=0 native_error_phase=commit native_error=" +
      std::to_string(static_cast<unsigned long>(ERROR_COMMITMENT_LIMIT)) +
      " native_error_name=ERROR_COMMITMENT_LIMIT\n";
  if (exit_code != 0) {
    std::fprintf(stderr, "worker exit code=%lu\n",
                 static_cast<unsigned long>(exit_code));
    return 1;
  }
  if (observation != expected) {
    std::fprintf(stderr, "unexpected worker observation; expected: %s",
                 expected.c_str());
    return 1;
  }

  std::printf(
      "bounded control held: process_commit_limit=%llu reserve_bytes=%llu "
      "commit_bytes=%llu native_error=%lu ERROR_COMMITMENT_LIMIT\n",
      static_cast<unsigned long long>(kProcessCommitLimit),
      static_cast<unsigned long long>(kReserveBytes),
      static_cast<unsigned long long>(kCommitBytes),
      static_cast<unsigned long>(ERROR_COMMITMENT_LIMIT));
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--worker")
    return worker();
  if (argc != 1) {
    std::fprintf(stderr, "usage: %s [--worker]\n", argv[0]);
    return 64;
  }
  return controller();
}
