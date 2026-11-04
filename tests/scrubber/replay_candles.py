#!/usr/bin/env python3
"""Path-sensitivity experiment using Scrubber.mq4, not an expected-profit oracle.

CSV: MT4 date,time,open,high,low,close,tick_volume (Bid prices).
Dates are broker calendar dates, end is exclusive. No timezone conversion.
Compile actual EA with the deterministic test broker; replay four invented paths.
No third-party Python dependencies. Requires g++.
"""
import argparse
import csv
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def read_bars(path, start, end):
    bars, all_dates = [], []
    previous = None
    with path.open(newline="") as stream:
        for number, row in enumerate(csv.reader(stream), 1):
            if len(row) != 7:
                raise ValueError(f"row {number}: expected seven MT4 CSV fields")
            time = datetime.strptime(row[0] + " " + row[1], "%Y.%m.%d %H:%M")
            if previous is not None and time <= previous:
                raise ValueError(f"row {number}: timestamps must strictly increase")
            previous = time
            all_dates.append(time)
            prices = [Decimal(p) * 100000 for p in row[2:6]]
            if any(p != p.to_integral_value() or p <= 0 for p in prices):
                raise ValueError(f"row {number}: invalid five-decimal AUDUSD price")
            o, h, l, c = map(int, prices)
            volume = int(row[6])
            if not l <= min(o, c) <= max(o, c) <= h or volume < 0:
                raise ValueError(f"row {number}: invalid OHLC/volume")
            if start <= time < end:
                bars.append((time, o, h, l, c))
    if not bars:
        raise ValueError("no bars in selected date range")
    if all_dates[0] > start or all_dates[-1] < end - timedelta(minutes=1):
        raise ValueError("CSV does not cover requested date range; select a covered range")
    return bars, all_dates


def path_points(bar, high_first, dense):
    _, o, h, l, c = bar
    vertices = [o, h, l, c] if high_first else [o, l, h, c]
    points = [o]
    for target in vertices[1:]:
        if dense:
            current = points[-1]
            direction = 1 if target > current else -1
            points.extend(range(current + direction, target + direction, direction))
        else:
            points.append(target)
    return points


def write_quotes(path, bars, high_first, dense):
    with path.open("w") as stream:
        for bar in bars:
            points = path_points(bar, high_first, dense)
            # UTC is only a deterministic encoding of server wall-clock labels.
            stamp = int(bar[0].replace(tzinfo=timezone.utc).timestamp())
            for i, point in enumerate(points):
                second = 59 * i // max(1, len(points) - 1)
                stream.write(f"{stamp + second} {point}\n")


def compile_replay(folder):
    source = re.sub(r"^#property.*$", "", (ROOT / "experts/Scrubber.mq4").read_text(), flags=re.M)
    source = ('#include "mt4_shim.hpp"\n' + source +
              '\n#define main regression_main\n#include "test_scenarios.hpp"\n#undef main\n#include "replay_driver.hpp"\n')
    cpp, exe = folder / "replay.cpp", folder / "replay"
    cpp.write_text(source)
    subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-I", str(ROOT / "experts"), "-I", str(HERE), str(cpp), "-o", str(exe)], check=True)
    return exe


def run(args):
    start, end = map(datetime.fromisoformat, (args.start, args.end_exclusive))
    if end <= start:
        raise ValueError("end must be after start")
    for name in ("lots", "booked_commission_per_lot", "closing_commission_per_lot",
                 "reserve_per_lot", "spread_points", "stop_points", "position_minimum_profit_floor"):
        value = getattr(args, name)
        if not 0 <= value < float("inf") or (name == "lots" and value == 0):
            raise ValueError(f"invalid {name}")
    bars, all_dates = read_bars(args.csv, start, end)
    gaps = [(b[0] - a[0]).total_seconds() / 60 - 1 for a, b in zip(bars, bars[1:])]
    sources = [ROOT / "experts/Scrubber.mq4", ROOT / "experts/scrubber/profit_math.mqh",
               HERE / "mt4_shim.hpp", HERE / "test_scenarios.hpp", HERE / "replay_driver.hpp",
               Path(__file__)]
    report = {
        "purpose": "synthetic path sensitivity; not a predicted profit or a 5% acceptance target",
        "csv": str(args.csv.resolve()), "csv_sha256": hashlib.sha256(args.csv.read_bytes()).hexdigest(),
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        "csv_rows": len(all_dates), "csv_first": str(all_dates[0]), "csv_last": str(all_dates[-1]),
        "start_inclusive": str(start), "end_exclusive": str(end), "selected_bars": len(bars),
        "selected_first": str(bars[0][0]), "selected_last": str(bars[-1][0]),
        "gaps_over_one_minute": sum(g > 0 for g in gaps), "missing_calendar_minutes_between_bars": sum(gaps),
        "assumptions": {
            "exit_model": "independent per-position peak drawdown with permanent activation",
            "direction": "SHORT", "grid_pips": 4, "levels_per_side": 3, "retention_extra": 3,
            "giveback_percent": 15, "max_orders": 200, "lots": args.lots,
            "position_minimum_profit_floor": args.position_minimum_profit_floor,
            "spread_points": args.spread_points, "stop_points": args.stop_points,
            "booked_commission_usd_per_lot": args.booked_commission_per_lot,
            "actual_closing_commission_usd_per_lot": args.closing_commission_per_lot,
            "estimated_closing_reserve_usd_per_lot": args.reserve_per_lot,
            "friday_cutoff_server": "23:35", "swap": 0, "account_currency": "USD",
            "contract_units_per_lot": 100000,
            "execution": "stops at first available quote, limits at requested price, immediate market closes",
            "omitted": "latency, rejects, money rounding, margin constraints, changing spread/fees/swap/session hours",
            "paths": "synthetic O-H-L-C and O-L-H-C; sparse vertices or dense one-point steps; minute opens jump",
            "volume": "not used to invent an unobservable tick sequence",
        }, "scenarios": [],
    }
    with tempfile.TemporaryDirectory(prefix="scrubber-replay-") as tmp:
        tmp = Path(tmp)
        exe, quotes = compile_replay(tmp), tmp / "quotes.txt"
        for dense in (False, True):
            for high_first in (True, False):
                name = ("OHLC" if high_first else "OLHC") + ("_dense" if dense else "_sparse")
                write_quotes(quotes, bars, high_first, dense)
                with quotes.open() as stream:
                    result = subprocess.run([str(exe), str(args.lots), str(args.booked_commission_per_lot),
                        str(args.closing_commission_per_lot), str(args.reserve_per_lot),
                        str(args.spread_points), str(args.stop_points), str(args.position_minimum_profit_floor)], stdin=stream,
                        capture_output=True, text=True, check=True)
                row = {"path": name, **json.loads(result.stdout)}
                report["scenarios"].append(row)
                print(f"{name}: realized=${row['realized_usd']:.4f}, remaining=${row['liquidation_net_usd']:.4f}, "
                      f"total=${row['total_net_usd']:.4f}, closed={row['closed']}", flush=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Saved {args.output}. Scenarios are neither statistical estimates nor upper/lower bounds.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--start", default="2026-10-22")
    parser.add_argument("--end-exclusive", default="2026-11-03", help="default includes all of November 2")
    parser.add_argument("--lots", type=float, default=.01)
    parser.add_argument("--booked-commission-per-lot", type=float, default=1)
    parser.add_argument("--closing-commission-per-lot", type=float, default=0)
    parser.add_argument("--reserve-per-lot", type=float, default=1)
    parser.add_argument("--spread-points", type=float, default=2)
    parser.add_argument("--stop-points", type=float, default=0)
    parser.add_argument("--position-minimum-profit-floor", type=float, default=0)
    parser.add_argument("--output", type=Path, default=Path("/tmp/scrubber-replay.json"))
    run(parser.parse_args())
