"""Compile and execute the production control/parser C against a mocked port.

This checks software behavior, not GTM register timing or a connected motor.
Set FOC_HOST_CC to gcc/clang/zig, or use an installed compiler.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(args, cwd):
    result = subprocess.run(
        [str(arg) for arg in args], cwd=cwd,
        capture_output=True, text=True, errors="replace",
    )
    if result.stdout.strip():
        print(result.stdout.strip())
    if result.stderr.strip():
        print(result.stderr.strip())
    if result.returncode:
        raise SystemExit(result.returncode)


def compiler():
    specified = os.environ.get("FOC_HOST_CC")
    if specified:
        return Path(shutil.which(specified) or specified)
    for name in ("gcc", "clang", "cc", "zig"):
        found = shutil.which(name)
        if found:
            return Path(found)
    portable_zig = ROOT.parents[1] / "tmp/host_toolchain/ziglang/zig.exe"
    if portable_zig.is_file():
        return portable_zig
    raise SystemExit("No C99 host compiler found. Set FOC_HOST_CC to gcc, clang, or zig.")


def main():
    cc = compiler()
    sources = [ROOT / "code/foc.c", ROOT / "user/foc_console.c", ROOT / "tests/foc_host_test.c"]
    includes = [ROOT / "code", ROOT / "user"]
    with tempfile.TemporaryDirectory(prefix="tc264_foc_tests_") as directory:
        build = Path(directory)
        executable = build / ("foc_host_test.exe" if os.name == "nt" else "foc_host_test")
        print(f"Host C compiler: {cc}")
        invocation = [cc, "cc"] if cc.name.lower() in ("zig", "zig.exe") else [cc]
        run([*invocation, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-DFOC_HOST_TEST=1",
             *("-I" + str(path) for path in includes), *sources, "-lm", "-o", executable], build)
        if not executable.exists():
            raise SystemExit("Linker produced no host test executable")
        run([executable], build)


if __name__ == "__main__":
    main()
