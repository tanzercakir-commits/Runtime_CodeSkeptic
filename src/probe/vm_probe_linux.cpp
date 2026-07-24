// SPDX-License-Identifier: Apache-2.0
//
// Linux x86-64 / aarch64 virtual-memory probe.
#include "runtimeskeptic/probe/vm_probe.hpp"

#if defined(RS_PLATFORM_LINUX)

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "runtimeskeptic/core/sha256.hpp"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace rs::probe {
namespace {

using rs::Address;
using vm::AddressRange;
using vm::ClassifiedRange;

constexpr const char* kSourceProbe = "rs-env-probe vm (linux)";

std::string errno_name(int e) {
    switch (e) {
        case EEXIST: return "EEXIST";
        case EINVAL: return "EINVAL";
        case ENOMEM: return "ENOMEM";
        case EACCES: return "EACCES";
        case EPERM: return "EPERM";
        case EAGAIN: return "EAGAIN";
        case EBADF: return "EBADF";
        case ENODEV: return "ENODEV";
        case EOVERFLOW: return "EOVERFLOW";
        default: return "errno " + std::to_string(e);
    }
}

std::string read_small_file(const char* path) {
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buffer[256];
    const ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (n <= 0) return {};
    buffer[n] = '\0';
    std::string s(buffer);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

// A single mmap attempt that always cleans up after itself.
struct MapAttempt {
    void* address = MAP_FAILED;
    int error = 0;

    bool ok() const { return address != MAP_FAILED; }
};

MapAttempt try_map(void* hint, std::size_t length, int prot, int flags,
                   int fd = -1, off_t offset = 0) {
    MapAttempt attempt;
    errno = 0;
    attempt.address = ::mmap(hint, length, prot, flags, fd, offset);
    attempt.error = attempt.ok() ? 0 : errno;
    return attempt;
}

void unmap(const MapAttempt& attempt, std::size_t length) {
    if (attempt.ok()) ::munmap(attempt.address, length);
}

// -------------------------------------------------------------------------
// MAP_FIXED_NOREPLACE detection.
//
// Kernels older than 4.17 do not know the flag. They ignore the unknown bit
// and treat the call as an ordinary hinted mapping, which means a request at
// an occupied address SUCCEEDS somewhere else instead of failing. Using the
// flag without this check would make every "exact placement worked" answer
// unreliable, so the probe establishes it first and refuses to use exact
// placement facts at all if the flag is not honoured.
// -------------------------------------------------------------------------
enum class NoReplaceSupport { Yes, No, Undetermined };

NoReplaceSupport detect_fixed_noreplace(std::size_t page_size,
                                        std::vector<std::string>& warnings) {
    // Occupy a page, then ask for exactly that page with MAP_FIXED_NOREPLACE.
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANONYMOUS);
    if (!occupied.ok()) {
        warnings.emplace_back(
            "could not create a reference mapping while detecting "
            "MAP_FIXED_NOREPLACE: " + errno_name(occupied.error));
        return NoReplaceSupport::Undetermined;
    }

    MapAttempt collide =
        try_map(occupied.address, page_size, PROT_READ,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);

    NoReplaceSupport support = NoReplaceSupport::Undetermined;
    if (!collide.ok()) {
        // EEXIST is the documented refusal. Any other refusal is ambiguous, so
        // we do not claim support.
        support = collide.error == EEXIST ? NoReplaceSupport::Yes
                                          : NoReplaceSupport::Undetermined;
        if (collide.error != EEXIST) {
            warnings.emplace_back(
                "MAP_FIXED_NOREPLACE collision test failed with " +
                errno_name(collide.error) + " instead of EEXIST; exact-placement "
                "facts were left unknown");
        }
    } else if (collide.address == occupied.address) {
        // It replaced the mapping. That is the destructive MAP_FIXED behavior
        // and must never happen with NOREPLACE.
        support = NoReplaceSupport::Undetermined;
        warnings.emplace_back(
            "MAP_FIXED_NOREPLACE replaced an existing mapping; the flag is not "
            "behaving as documented and exact-placement probing was disabled");
        // The original mapping is gone; the new one covers the same address.
        unmap(collide, page_size);
        return support;
    } else {
        // Silently relocated: the kernel ignored the flag.
        support = NoReplaceSupport::No;
        unmap(collide, page_size);
    }

    unmap(occupied, page_size);
    return support;
}

// -------------------------------------------------------------------------
// Highest usable user address, by binary search.
//
// Only ever attempts one page, and only with MAP_FIXED_NOREPLACE, so the
// search cannot disturb the process.
// -------------------------------------------------------------------------
bool can_map_exactly_at(std::uint64_t address, std::size_t length) {
    MapAttempt attempt =
        try_map(reinterpret_cast<void*>(address), length, PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE);
    if (!attempt.ok()) return false;
    const bool exact = reinterpret_cast<std::uint64_t>(attempt.address) == address;
    unmap(attempt, length);
    return exact;
}

std::uint64_t find_max_user_address(std::size_t page_size) {
    // Lower bound: an address we know works. Upper bound: one we know fails.
    std::uint64_t low = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {
        const std::uint64_t candidate = std::uint64_t{1} << bit;
        if (can_map_exactly_at(candidate, page_size)) {
            low = candidate;
        } else {
            break;
        }
    }
    if (low == 0) return 0;

    std::uint64_t high = low * 2;
    // Binary search for the first page that cannot be mapped.
    while (high - low > page_size) {
        const std::uint64_t mid = low + ((high - low) / 2 / page_size) * page_size;
        if (mid == low) break;
        if (can_map_exactly_at(mid, page_size)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    // `low` is the last page that mapped successfully; the exclusive end of the
    // usable space is one page above it.
    return low + page_size;
}

// -------------------------------------------------------------------------
// Does a hint relocate?
// -------------------------------------------------------------------------
enum class RelocationObservation { Relocated, Honoured, Undetermined };

RelocationObservation observe_hint_relocation(std::size_t page_size) {
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANONYMOUS);
    if (!occupied.ok()) return RelocationObservation::Undetermined;

    // Ask for the occupied address as a plain hint (no MAP_FIXED* flags).
    MapAttempt hinted = try_map(occupied.address, page_size, PROT_READ,
                                MAP_PRIVATE | MAP_ANONYMOUS);
    RelocationObservation observation = RelocationObservation::Undetermined;
    if (hinted.ok()) {
        observation = hinted.address == occupied.address
                          ? RelocationObservation::Honoured
                          : RelocationObservation::Relocated;
        unmap(hinted, page_size);
    }
    unmap(occupied, page_size);
    return observation;
}

// -------------------------------------------------------------------------
// Protection experiments.
// -------------------------------------------------------------------------
struct ProtectionObservations {
    bool tested_rwx = false;
    bool rwx_allowed = false;
    bool tested_rx = false;
    bool rx_allowed = false;
    bool tested_flip = false;
    bool flip_allowed = false;
};

ProtectionObservations probe_protection(std::size_t page_size) {
    ProtectionObservations obs;

    MapAttempt rwx = try_map(nullptr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS);
    obs.tested_rwx = true;
    obs.rwx_allowed = rwx.ok();
    unmap(rwx, page_size);

    MapAttempt rx = try_map(nullptr, page_size, PROT_READ | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS);
    obs.tested_rx = true;
    obs.rx_allowed = rx.ok();
    unmap(rx, page_size);

    MapAttempt rw = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS);
    if (rw.ok()) {
        obs.tested_flip = true;
        obs.flip_allowed =
            ::mprotect(rw.address, page_size, PROT_READ | PROT_EXEC) == 0;
        unmap(rw, page_size);
    }
    return obs;
}

// -------------------------------------------------------------------------
// Lazy reservation: can a large range be reserved without backing it?
// -------------------------------------------------------------------------
enum class ReservationObservation { Lazy, Eager, Undetermined };

ReservationObservation probe_lazy_reservation() {
    constexpr std::size_t kOneGiB = 1024ull * 1024 * 1024;
    MapAttempt reserved = try_map(nullptr, kOneGiB, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
    if (!reserved.ok()) return ReservationObservation::Undetermined;
    unmap(reserved, kOneGiB);
    return ReservationObservation::Lazy;
}

// -------------------------------------------------------------------------
// Faulting test: mapping past end of file. Runs in a forked child.
// -------------------------------------------------------------------------
enum class EofObservation { Sigbus, ZeroFill, MapRefused, Undetermined };

EofObservation probe_beyond_eof(std::size_t page_size, unsigned timeout_seconds,
                                std::vector<std::string>& warnings) {
    const char* tmpdir = ::getenv("TMPDIR");
    std::string path = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    path += "/rs-env-probe-XXXXXX";
    std::vector<char> templ(path.begin(), path.end());
    templ.push_back('\0');

    const int fd = ::mkstemp(templ.data());
    if (fd < 0) {
        warnings.emplace_back("could not create a temporary file for the "
                              "beyond-EOF test");
        return EofObservation::Undetermined;
    }
    ::unlink(templ.data());  // the file lives only as long as the descriptor

    // One page of file, two pages of mapping: the second page lies entirely
    // past end of file.
    if (::ftruncate(fd, static_cast<off_t>(page_size)) != 0) {
        ::close(fd);
        return EofObservation::Undetermined;
    }

    const std::size_t map_length = page_size * 2;
    MapAttempt probe_map = try_map(nullptr, map_length, PROT_READ, MAP_SHARED, fd, 0);
    if (!probe_map.ok()) {
        ::close(fd);
        return EofObservation::MapRefused;
    }
    // The parent never touches the second page; only the child does.
    unmap(probe_map, map_length);

    ::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fd);
        warnings.emplace_back("fork failed; the beyond-EOF test was skipped");
        return EofObservation::Undetermined;
    }

    if (pid == 0) {
        // Child. Any fault here is the observation we want; it must not be
        // caught or reported through the parent's machinery.
        ::alarm(timeout_seconds);
        void* p = ::mmap(nullptr, map_length, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) ::_exit(20);
        volatile const unsigned char* bytes =
            static_cast<volatile const unsigned char*>(p);
        // Touch a byte on the page that lies entirely past end of file.
        const unsigned char value = bytes[page_size];
        ::_exit(value == 0 ? 10 : 11);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::close(fd);
        return EofObservation::Undetermined;
    }
    ::close(fd);

    if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        if (sig == SIGBUS) return EofObservation::Sigbus;
        if (sig == SIGALRM) {
            warnings.emplace_back("the beyond-EOF test child timed out");
            return EofObservation::Undetermined;
        }
        warnings.emplace_back("the beyond-EOF test child died on signal " +
                              std::to_string(sig));
        return EofObservation::Undetermined;
    }
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code == 10 || code == 11) return EofObservation::ZeroFill;
        if (code == 20) return EofObservation::MapRefused;
    }
    return EofObservation::Undetermined;
}

// -------------------------------------------------------------------------
// Address-space scan.
// -------------------------------------------------------------------------
struct ScanOutcome {
    std::vector<ClassifiedRange> available;
    std::vector<ClassifiedRange> unavailable;
    std::vector<std::string> occupied_notes;
};

ScanOutcome scan_address_space(std::size_t page_size, std::uint64_t probe_length,
                               std::uint64_t max_user_address) {
    ScanOutcome outcome;

    std::vector<std::uint64_t> candidates;
    for (unsigned bit = 16; bit < 63; ++bit) {
        const std::uint64_t boundary = std::uint64_t{1} << bit;
        candidates.push_back(boundary);
        // Also sample JUST BELOW each boundary. The ladder used to climb only
        // powers of two, and it systematically missed the addresses that
        // emulators actually use: guard pages, commpages and rollover
        // barriers are placed just under a boundary, never on it. QEMU's ARM
        // commpage at 0xffff0f00 and Box64's 4 GiB rollover guard both sit in
        // that blind spot, so both came back UNKNOWN for no better reason
        // than where the probe happened to look.
        if (boundary > probe_length) {
            const std::uint64_t below = boundary - probe_length;
            if (below % page_size == 0) candidates.push_back(below);
        }
    }
    // The ROADMAP's motivating address, and the band around it.
    candidates.push_back(0x1000000000ull);
    candidates.push_back(0x4000000000ull);
    candidates.push_back(0x6fffff0000ull);
    candidates.push_back(0x7fff00000000ull);

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (std::uint64_t base : candidates) {
        if (base % page_size != 0) continue;
        if (max_user_address != 0 && base >= max_user_address) {
            // Already covered by the max_user_address fact; probing here would
            // only restate it.
            continue;
        }
        const auto range = AddressRange::from_base_size(base, probe_length);
        if (!range) continue;

        MapAttempt attempt = try_map(
            reinterpret_cast<void*>(base), static_cast<std::size_t>(probe_length),
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE);

        if (attempt.ok()) {
            if (reinterpret_cast<std::uint64_t>(attempt.address) == base) {
                ClassifiedRange cr;
                cr.range = *range;
                cr.evidence = EvidenceClass::MeasuredCapability;
                cr.note = "mapped successfully at this exact address in the "
                          "probe process";
                outcome.available.push_back(cr);
            }
            unmap(attempt, static_cast<std::size_t>(probe_length));
            continue;
        }

        if (attempt.error == EEXIST) {
            // Occupied by this process's own image, libraries or heap. That is
            // a fact about one process layout, NOT about the host, so it must
            // not become an "unavailable range" that other programs would be
            // judged against.
            outcome.occupied_notes.push_back(
                "range " + range->to_string() +
                " was occupied in the probe process (EEXIST); this is a "
                "property of the probe's own layout and was NOT recorded as a "
                "host limitation");
            continue;
        }

        // EINVAL / ENOMEM / EPERM at a specific address are structural: the
        // kernel refuses this part of the address space regardless of what is
        // already mapped.
        ClassifiedRange cr;
        cr.range = *range;
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = "exact mapping refused with " + errno_name(attempt.error);
        outcome.unavailable.push_back(cr);
    }
    return outcome;
}

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now);
#else
    ::gmtime_r(&now, &tm_buf);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buffer;
}

vm::Architecture architecture_from_machine(const char* machine) {
    if (machine == nullptr) return vm::Architecture::Unknown;
    const std::string m(machine);
    if (m == "x86_64" || m == "amd64") return vm::Architecture::X86_64;
    if (m == "aarch64" || m == "arm64") return vm::Architecture::Aarch64;
    if (m == "i386" || m == "i686") return vm::Architecture::X86;
    if (m.rfind("arm", 0) == 0) return vm::Architecture::Arm;
    return vm::Architecture::Other;
}

}  // namespace

std::string probe_platform_name() { return "linux"; }

Result probe_virtual_memory(const Options& options) {
    Result result;
    result.implemented = true;

    vm::EnvironmentProfile& profile = result.profile;
    std::vector<std::string>& warnings = profile.run.warnings;

    profile.origin = vm::ProfileOrigin::Measured;
    profile.run.probe_version = kProbeVersion;

    // -- platform ---------------------------------------------------------
    profile.platform.os = vm::OperatingSystem::Linux;
    utsname uts{};
    if (::uname(&uts) == 0) {
        profile.platform.kernel_version = uts.release;
        profile.platform.os_version = std::string(uts.sysname) + " " + uts.release;
        profile.platform.host_arch = architecture_from_machine(uts.machine);
    }
    // The probe is the process being measured, so process width is known
    // exactly and does not need uname.
    profile.platform.process_arch =
        sizeof(void*) == 8
            ? (profile.platform.host_arch == vm::Architecture::Aarch64
                   ? vm::Architecture::Aarch64
                   : vm::Architecture::X86_64)
            : vm::Architecture::X86;
    // Detecting emulated execution (qemu-user and friends) is not attempted in
    // v0.1, so translation mode stays unknown rather than being guessed as
    // "none".
    profile.platform.translation_mode = vm::TranslationMode::Unknown;
    warnings.emplace_back(
        "translation_mode was not probed in v0.1 and is reported as unknown "
        "rather than assumed to be 'none'");

    // -- page size and granularity ----------------------------------------
    const long sc_page = ::sysconf(_SC_PAGESIZE);
    if (sc_page <= 0) {
        warnings.emplace_back("sysconf(_SC_PAGESIZE) failed; the probe cannot "
                              "continue meaningfully");
        return result;
    }
    const std::size_t page_size = static_cast<std::size_t>(sc_page);

    profile.vm.page_size = Fact<std::uint64_t>::known(
        static_cast<std::uint64_t>(page_size), EvidenceClass::MeasuredCapability,
        std::string(kSourceProbe) + ": sysconf(_SC_PAGESIZE)");

    // On Linux, mmap placement granularity equals the page size. This is a
    // property of the mmap interface rather than a separate measurement, so
    // it is recorded as a specified guarantee with an explicit source.
    profile.vm.allocation_granularity = Fact<std::uint64_t>::known(
        static_cast<std::uint64_t>(page_size), EvidenceClass::SpecifiedGuarantee,
        "Linux mmap(2): addr is rounded to page granularity");

    // -- can we map anything at all? --------------------------------------
    // The most basic capability, and the one a profile must carry before any
    // request may be reported as SUPPORTED. Measured rather than assumed:
    // seccomp filters, restrictive LSM policy and exhausted map counts all
    // make this false on real machines.
    {
        MapAttempt basic = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS);
        profile.vm.anonymous_mapping_supported = Fact<bool>::known(
            basic.ok(), EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(NULL, page_size, RW, "
            "MAP_PRIVATE|MAP_ANONYMOUS)" +
                (basic.ok() ? "" : " failed with " + errno_name(basic.error)));
        unmap(basic, page_size);
        if (!basic.ok()) {
            warnings.emplace_back(
                "a plain anonymous mapping failed with " +
                errno_name(basic.error) +
                "; the remaining measurements are unlikely to be meaningful");
        }
    }

    // -- minimum mappable address -----------------------------------------
    const std::string mmap_min = read_small_file("/proc/sys/vm/mmap_min_addr");
    if (!mmap_min.empty()) {
        profile.vm.min_map_address = Fact<Address>::known(
            Address(std::strtoull(mmap_min.c_str(), nullptr, 10)),
            EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": /proc/sys/vm/mmap_min_addr");
    } else {
        warnings.emplace_back(
            "/proc/sys/vm/mmap_min_addr was unreadable; the minimum mappable "
            "address is unknown");
    }

    // -- MAP_FIXED_NOREPLACE ----------------------------------------------
    const NoReplaceSupport noreplace = detect_fixed_noreplace(page_size, warnings);
    switch (noreplace) {
        case NoReplaceSupport::Yes:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": MAP_FIXED_NOREPLACE refused a collision with EEXIST");
            break;
        case NoReplaceSupport::No:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                false, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": MAP_FIXED_NOREPLACE was ignored and the mapping relocated");
            break;
        case NoReplaceSupport::Undetermined:
            break;
    }

    // Exact placement facts depend on a trustworthy MAP_FIXED_NOREPLACE. If we
    // could not establish it, we deliberately learn nothing about exact
    // placement rather than probing destructively.
    const bool exact_probing_allowed = noreplace == NoReplaceSupport::Yes;
    if (!exact_probing_allowed) {
        warnings.emplace_back(
            "exact-placement probing was skipped because MAP_FIXED_NOREPLACE "
            "could not be confirmed; MAP_FIXED is never used by this probe "
            "because it destroys existing mappings");
    }

    if (exact_probing_allowed) {
        profile.vm.exact_mapping = Fact<SupportLevel>::known(
            SupportLevel::ConditionallySupported,
            EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": exact placement succeeds for free, in-bounds, page-aligned "
                "ranges");
        profile.vm.exact_mapping_failure_codes = {"EEXIST", "EINVAL", "ENOMEM",
                                                  "EPERM"};
    }

    // -- maximum user address ---------------------------------------------
    std::uint64_t max_user = 0;
    if (exact_probing_allowed) {
        max_user = find_max_user_address(page_size);
        if (max_user != 0) {
            profile.vm.max_user_address = Fact<Address>::known(
                Address(max_user), EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": binary search with MAP_FIXED_NOREPLACE");
        }
    }

    // -- hint relocation ---------------------------------------------------
    switch (observe_hint_relocation(page_size)) {
        case RelocationObservation::Relocated:
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": a hinted mapping at an occupied address was relocated");
            break;
        case RelocationObservation::Honoured:
            // Observing one honoured hint does NOT establish that hints are
            // never relocated. mmap(2) explicitly permits relocation, so the
            // specified guarantee outranks this single observation.
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                true, EvidenceClass::SpecifiedGuarantee,
                "Linux mmap(2): addr is a hint and the kernel may place the "
                "mapping elsewhere");
            warnings.emplace_back(
                "the hint was honoured in this run, but mmap(2) permits "
                "relocation; the specified guarantee was recorded rather than "
                "the observation");
            break;
        case RelocationObservation::Undetermined:
            break;
    }

    // -- protection --------------------------------------------------------
    const ProtectionObservations prot = probe_protection(page_size);
    if (prot.tested_rwx) {
        profile.vm.protection.write_execute_simultaneous = Fact<bool>::known(
            prot.rwx_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(PROT_READ|PROT_WRITE|PROT_EXEC)");
    }
    if (prot.tested_rx) {
        profile.vm.protection.anonymous_executable_mapping = Fact<bool>::known(
            prot.rx_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(PROT_READ|PROT_EXEC)");
    }
    if (prot.tested_flip) {
        profile.vm.protection.write_then_execute_transition = Fact<bool>::known(
            prot.flip_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mprotect(RW -> RX)");
    }
    if (prot.tested_rx && prot.rx_allowed) {
        // Executable memory was obtained by an ordinary unprivileged process
        // with no entitlement of any kind.
        profile.vm.protection.jit_entitlement_required = Fact<bool>::known(
            false, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": executable memory was obtained without any entitlement");
    }

    // -- reserve/commit ----------------------------------------------------
    if (probe_lazy_reservation() == ReservationObservation::Lazy) {
        profile.vm.reserve_commit_model = Fact<vm::ReserveCommitModel>::known(
            vm::ReserveCommitModel::PosixLazy, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": a 1 GiB PROT_NONE reservation succeeded without backing it");
    }

    // -- beyond end of file ------------------------------------------------
    if (options.run_faulting_tests) {
        switch (probe_beyond_eof(page_size, options.child_timeout_seconds,
                                 warnings)) {
            case EofObservation::Sigbus:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::Sigbus,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": an isolated child took SIGBUS reading a whole "
                            "page past end of file");
                break;
            case EofObservation::ZeroFill:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::ZeroFill,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": an isolated child read past end of file without "
                            "faulting");
                break;
            case EofObservation::MapRefused:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::Error,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": the mapping call itself refused to extend past "
                            "end of file");
                break;
            case EofObservation::Undetermined:
                break;
        }
    } else {
        warnings.emplace_back(
            "faulting tests were disabled; beyond-EOF behavior is unknown");
    }

    // -- address-space scan ------------------------------------------------
    if (options.scan_address_space && exact_probing_allowed) {
        const std::uint64_t probe_length =
            options.max_test_mapping_bytes < page_size
                ? page_size
                : options.max_test_mapping_bytes;
        ScanOutcome scan = scan_address_space(page_size, probe_length, max_user);
        profile.vm.available_ranges = std::move(scan.available);
        profile.vm.unavailable_ranges = std::move(scan.unavailable);
        for (auto& note : scan.occupied_notes) {
            warnings.push_back(std::move(note));
        }
    }

    // -- run metadata ------------------------------------------------------
    profile.run.timestamp_utc = utc_timestamp();
    profile.run.run_id = rs::hash::sha256_hex(profile.run.timestamp_utc + ":" +
                                              std::to_string(::getpid()))
                             .substr(0, 16);
    profile.notes.emplace_back(
        "Ranges the probe found occupied by its own process image are NOT "
        "recorded as host limitations; see probe_run.warnings.");

    return result;
}

}  // namespace rs::probe

#endif  // RS_PLATFORM_LINUX
