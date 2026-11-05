#!/usr/bin/env python3
"""Execute CandleTaker's actual handlers with candle and broker simulation."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "experts/CandleTaker.mq4").read_text()
assert "MaximumProfitDrawdownPercent" not in source
assert "CandleLookbackBars" not in source and "g_sequence_mean" not in source
assert "input int minimum_candles_in_sequence = 2;" in source
assert source.index("ProtectBeforeClose(i);") < source.index("if(OrderClose(")


def function_body(text, name):
    start = re.search(r"^\w+ " + name + r"\([^)]*\)\s*\{", text, re.M)
    assert start, name
    opening = text.index("{", start.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[opening:end]


# Regression guard: all execution/accounting functions must remain identical
# to Scrubber apart from the EA's order-comment prefix and its string length.
baseline = (root / "experts/Scrubber.mq4").read_text()
shared = [
    "Direction", "MarketType", "PendingType", "PendingMatchesDirection",
    "PendingTypeName", "OwnSelected", "MarketSelected", "PendingSelected",
    "ExitPrice", "EntryPrice", "SlippagePoints", "ExitReserve", "SelectedNet",
    "Instrument", "SelectFixedLotSize", "AutoPipSize", "TradeReady", "BrokerDistance",
    "ServerNow", "EntryDeadline", "SampleSpread", "SpreadAllowed", "DeletePending",
    "DeleteAllPending", "SelectedGrid", "GridOccupied", "OwnOrderCount",
    "EarlierPendingAtGrid", "WrongDirectionExists", "ManageGrid",
    "RunPendingSafetyChecks", "OnTimer", "OnTester", "OnDeinit",
]
for name in shared:
    actual = function_body(source, name).replace("CandleTaker|G=", "Scrubber|G=")
    actual = actual.replace("StringSubstr(comment, 14)", "StringSubstr(comment, 11)")
    actual = actual.replace("CandleTakerFrontier", "ScrubberFrontier")
    actual = actual.replace("CANDLETAKER_INSTRUMENT", "SCRUBBER_INSTRUMENT")
    assert actual == function_body(baseline, name), f"Execution drift: {name}"
print(f"PASS: {len(shared)} execution/accounting functions match Scrubber", flush=True)
math = (root / "experts/candletaker/profit_math.mqh").read_text()
baseline_math = (root / "experts/scrubber/profit_math.mqh").read_text()
for name in ["MeetsFloor", "Frontier", "RoundPrice"]:
    assert function_body(math, "CandleTaker" + name) == function_body(baseline_math, "Scrubber" + name)
source = re.sub(r"^#property.*$", "", source, flags=re.M)
source = '#include "mt4_candles.hpp"\n' + source + '\n#include "test_scenarios.hpp"\n'
with tempfile.TemporaryDirectory(prefix="candletaker-tests-") as tmp:
    cpp = Path(tmp) / "candletaker.cpp"
    exe = Path(tmp) / "candletaker-tests"
    cpp.write_text(source)
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(root / "experts"), "-I", str(Path(__file__).parent), str(cpp), "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
