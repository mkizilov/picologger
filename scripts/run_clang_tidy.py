#!/usr/bin/env python3
"""Analyze every project C source using its real host or firmware compile command."""

from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src"
TIDY = ROOT / ".venv" / "bin" / "clang-tidy"
ARM_INCLUDE = ROOT / ".toolchain" / "arm-gnu" / "arm-none-eabi" / "include"


def entries(directory):
    with (directory / "compile_commands.json").open(encoding="utf-8") as source:
        return {Path(entry["file"]).resolve() for entry in json.load(source)}


def main():
    pico = ROOT / "build" / "pico"
    wireless = ROOT / "build" / "pico_w"
    host = ROOT / "build" / "host-sanitized"
    pico_files, wireless_files, host_files = map(entries, (pico, wireless, host))
    sources = sorted(SOURCE.rglob("*.c"))
    missing = [path for path in sources if path not in pico_files and path not in host_files]
    if missing:
        print("Missing compile commands: " + ", ".join(str(p) for p in missing), file=sys.stderr)
        return 1
    jobs = [(path, pico if path in pico_files else host) for path in sources]
    for path in (SOURCE / "hal" / "board.c", SOURCE / "hal" / "power_clock.c"):
        if path not in wireless_files:
            print(f"Missing Pico W compile command: {path}", file=sys.stderr)
            return 1
        jobs.append((path, wireless))

    def check(job):
        path, build = job
        command = [str(TIDY), "-p", str(build), str(path), "--quiet",
                   # Newlib lacks Annex K; this checker rejects even bounded snprintf.
                   "--checks=-*,clang-analyzer-core.*,clang-analyzer-security.*,clang-analyzer-unix.*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling",
                   "--warnings-as-errors=*", "--header-filter=^" + re.escape(str(SOURCE)) + "/"]
        if build != host:
            command.extend(("--extra-arg=-isystem", "--extra-arg=" + str(ARM_INCLUDE)))
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return path.relative_to(ROOT), build.name, result.returncode, result.stdout

    with ThreadPoolExecutor(max_workers=2) as workers:
        results = list(workers.map(check, jobs))
    failed = False
    for path, build, code, output in results:
        if code:
            failed = True
            print(f"clang-tidy failed: {path} ({build})\n{output}", file=sys.stderr)
    if failed:
        return 1
    print(f"clang-tidy passed for {len(sources)} project C files, including Pico W variants")
    return 0


if __name__ == "__main__":
    sys.exit(main())
