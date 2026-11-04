#!/usr/bin/env python3
"""Compile Scrubber's actual event handlers against a deterministic MT4 test shim."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "experts/Scrubber.mq4").read_text()
assert not re.search(r"\b(iOpen|iHigh|iLow|iClose|iTime|iBars|Period|Bars)\s*\(", source)
assert not re.search(r"\b(Open|High|Low|Close|Time)\s*\[", source)
source = re.sub(r"^#property.*$", "", source, flags=re.M)
source = '#include "mt4_shim.hpp"\n' + source + '\n#include "test_scenarios.hpp"\n'
with tempfile.TemporaryDirectory(prefix="scrubber-tests-") as tmp:
    cpp = Path(tmp) / "scrubber.cpp"
    exe = Path(tmp) / "scrubber-tests"
    cpp.write_text(source)
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(root / "experts"), "-I", str(Path(__file__).parent), str(cpp), "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
