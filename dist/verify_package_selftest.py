#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Adversarial tests for the prebuilt-package verifier."""
import os
import stat
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY = ROOT / "dist" / "verify-package.sh"
VALID_BENCHMARK = '{"schema":"runtime-skeptic.runtime-overhead.v1","iterations":8,"unit":"nanoseconds","modes":[{"name":"native","median_ns":1,"p95_ns":1,"delta_ns":0,"ratio_milli":1000},{"name":"compile_disabled","median_ns":1,"p95_ns":1,"delta_ns":0,"ratio_milli":1000},{"name":"runtime_disabled","median_ns":1,"p95_ns":1,"delta_ns":0,"ratio_milli":1000},{"name":"enabled_buffered","median_ns":1,"p95_ns":1,"delta_ns":0,"ratio_milli":1000},{"name":"enabled_plus_flush","median_ns":1,"p95_ns":1,"delta_ns":0,"ratio_milli":1000}]}'


def write_tool(directory: Path, name: str, body: str) -> None:
    path = directory / name
    path.write_text("#!/usr/bin/env bash\n" + body, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def run(package: Path, **extra_env: str) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.update(extra_env)
    return subprocess.run(["bash", str(VERIFY), str(package)], env=env,
                          capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rs-package-selftest-") as tmp:
        package = Path(tmp) / "package"
        for rel in ("bin", "lib/cmake/RuntimeSkeptic",
                    "include/runtimeskeptic/runtime", "contracts/campaign",
                    "contracts", "profiles/measured"):
            (package / rel).mkdir(parents=True, exist_ok=True)
        for rel in ("QUICKSTART.md", "RELEASE.md", "LICENSE",
                    "lib/libruntimeskeptic.so.1",
                    "lib/cmake/RuntimeSkeptic/RuntimeSkepticConfig.cmake",
                    "contracts/campaign/redis-jemalloc-page-size-lg12.json",
                    "contracts/emulator-highmem-guest-mapping.json",
                    "profiles/measured/macos-14-arm64-native.measured.json"):
            (package / rel).touch()
        for name in ("runtime.h", "runtime_posix.h", "runtime_windows.h",
                     "trace.hpp"):
            (package / "include/runtimeskeptic/runtime" / name).touch()

        tools = package / "bin"
        write_tool(tools, "rs-check", 'echo "UNSUPPORTED RS-VM-0006"\nexit 1\n')
        write_tool(tools, "rs-runtime-sample", 'printf "{}\\n" > "$1"\n')
        write_tool(tools, "rs-replay",
                   'echo "reproduced without issuing OS calls"\n'
                   'if [ "${RS_SELFTEST_REPLAY_FAIL:-0}" = 1 ]; then exit 42; fi\n')
        write_tool(tools, "rs-runtime-benchmark",
                   'out="$4"\n'
                   'if [ "${RS_SELFTEST_BAD_BENCHMARK:-0}" = 1 ]; then\n'
                   '  printf "{}\\n" > "$out"\n'
                   'else\n'
                   f'  printf \'%s\\n\' \'{VALID_BENCHMARK}\' > "$out"\n'
                   'fi\n')
        for name in ("rs-env-probe", "rs-profile", "rs-mcp"):
            write_tool(tools, name, "exit 0\n")

        valid = run(package)
        if valid.returncode != 0:
            print("package verifier selftest: valid fixture failed")
            print(valid.stdout + valid.stderr)
            return 1
        replay = run(package, RS_SELFTEST_REPLAY_FAIL="1")
        if replay.returncode == 0 or "runtime replay exit 42" not in (
                replay.stdout + replay.stderr):
            print("package verifier selftest: non-zero replay did not fail closed")
            print(replay.stdout + replay.stderr)
            return 1
        benchmark = run(package, RS_SELFTEST_BAD_BENCHMARK="1")
        if benchmark.returncode == 0 or "benchmark root fields violate schema" not in (
                benchmark.stdout + benchmark.stderr):
            print("package verifier selftest: malformed benchmark did not fail closed")
            print(benchmark.stdout + benchmark.stderr)
            return 1
    print("package verifier selftest: replay status and benchmark schema fail closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
