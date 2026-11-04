import tempfile
import unittest
import json
import subprocess
from datetime import datetime
from pathlib import Path

from replay_candles import compile_replay, path_points, read_bars, write_quotes


class CandleReplayTests(unittest.TestCase):
    def test_paths_preserve_ohlc(self):
        bar = (datetime(2026, 10, 22), 100, 110, 95, 102)
        for dense in (False, True):
            for high_first in (False, True):
                points = path_points(bar, high_first, dense)
                self.assertEqual((points[0], max(points), min(points), points[-1]), (100, 110, 95, 102))
                if dense:
                    self.assertTrue(all(abs(a-b) == 1 for a, b in zip(points, points[1:])))
                self.assertEqual(points.index(110) < points.index(95), high_first)

    def test_minute_gaps_are_not_interpolated(self):
        bars = [(datetime(2026, 10, 22), 100, 102, 99, 101),
                (datetime(2026, 10, 22, 0, 5), 120, 122, 119, 121)]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "quotes"
            write_quotes(path, bars, True, True)
            ticks = [tuple(map(int, s.split())) for s in path.read_text().splitlines()]
        jumps = [(b[0]-a[0], b[1]-a[1]) for a, b in zip(ticks, ticks[1:]) if b[0]-a[0] > 60]
        self.assertEqual(jumps, [(241, 19)])

    def test_parser_dates_validation_and_end_exclusion(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bars.csv"
            rows = ["2026.10.22,00:00,0.70000,0.70001,0.69999,0.70000,3",
                    "2026.10.22,00:01,0.70000,0.70001,0.69999,0.70000,3"]
            path.write_text("\n".join(rows))
            args = (path, datetime(2026, 10, 22), datetime(2026, 10, 22, 0, 1))
            bars, all_dates = read_bars(*args)
            self.assertEqual(len(bars), 1)
            self.assertEqual(len(all_dates), 2)
            for invalid in [rows[0] + "\n" + rows[0], rows[0].replace("0.70001", "0.69998")]:
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    read_bars(*args)


class BrokerReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix="scrubber-replay-test-")
        cls.exe = compile_replay(Path(cls.folder.name))

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def replay(self, points, lot=.01, closing=0, floor=0):
        # Thursday, same server-calendar encoding as the candle runner.
        quotes = "".join(f"1792627500 {p}\n" for p in points)
        result = subprocess.run([str(self.exe), str(lot), "1", str(closing), "1", "2", "0", str(floor)],
                                input=quotes, capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def test_unchanged_quotes_no_fake_fills(self):
        result = self.replay([70000] * 20)
        self.assertEqual((result["fills"], result["closed"], result["pending"]), (0, 0, 6))
        self.assertEqual(result["total_net_usd"], 0)

    def test_floor_excludes_small_profit_cycles(self):
        result = self.replay([70000, 70040, 70000, 70020], floor=5)
        self.assertEqual(result["closed"], 0)
        self.assertEqual(result["broker_sl_fills"], 0)
        self.assertEqual(result["drawdown_triggers"], 0)
        self.assertEqual(result["open"], 2)

    def test_gapped_stop_and_commission_accounting(self):
        for closing in (0, 1):
            for lot in (.01, 1):
                result = self.replay([70000, 70040, 70000, 70020], lot, closing)
                scale = lot / .01
                # Sell 0.70040 stopped at Ask 0.70022: gross 0.18 per .01 lot,
                # less 0.01 booked commission and optional actual closing fee.
                # Second sell 0.70000 remains: gross -0.22, same booked/exit costs.
                self.assertEqual(result["broker_sl_fills"], 1)
                self.assertEqual((result["fills"], result["closed"], result["open"]), (2, 1, 1))
                self.assertAlmostEqual(result["realized_usd"], (.17 - closing * .01) * scale, places=7)
                self.assertAlmostEqual(result["liquidation_net_usd"], (-.23 - closing * .01) * scale, places=7)
                self.assertAlmostEqual(result["total_net_usd"], (-.06 - closing * .02) * scale, places=7)


if __name__ == "__main__":
    unittest.main()
