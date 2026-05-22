"""Smoke tests: prove the pybind11 bindings load + each class round-trips.

Intentionally minimal — catches "binding broken at compile time", "Python
can't find the .so", or "constructor/method signature drifted." Full
numerical correctness is covered by the C++ tests (features_unit_tests).

Filename kept as test_ofi_smoke.py for git-blame continuity; covers
all 4 bound feature classes today.
"""

import unittest

import bpt_features as bf


class TestExportsPresent(unittest.TestCase):
    def test_module_exports(self):
        # Raw C++ classes
        for cls in ["OFICalculator", "FairValueEstimator",
                    "RealizedVolEstimator", "VolatilityGate",
                    "OrderBookState", "OrderSide", "QueueTracker"]:
            self.assertTrue(hasattr(bf, cls), f"missing class: {cls}")
        # Function wrappers
        for fn in ["ofi", "microprice", "mid_price", "fair_value_ewma",
                   "microprice_size_capped", "realized_vol", "vol_gate"]:
            self.assertTrue(hasattr(bf, fn), f"missing wrapper: {fn}")


class TestOFISmoke(unittest.TestCase):
    def test_construct_and_update(self):
        cfg = bf.OFICalculator.Config()
        cfg.max_levels = 5
        cfg.window_ns = 1_000_000_000

        calc = bf.OFICalculator(cfg)
        self.assertFalse(calc.is_warm())

        v = calc.update(bids=[(100.0, 10.0)], asks=[(100.1, 8.0)],
                        timestamp_ns=1_000_000_000)
        self.assertEqual(v, 0.0)  # first call, no diff

        v2 = calc.update(bids=[(100.0, 15.0)], asks=[(100.1, 5.0)],
                         timestamp_ns=1_100_000_000)
        self.assertTrue(calc.is_warm())
        self.assertGreater(v2, 0.0)  # buy-pressure → positive OFI


class TestFairValueSmoke(unittest.TestCase):
    def test_microprice_basic(self):
        cfg = bf.FairValueEstimator.Config()
        cfg.mode = bf.FairValueEstimator.Mode.Micro
        fve = bf.FairValueEstimator(cfg)
        # Symmetric book → microprice = mid
        v = fve.estimate(bid_px=100.0, ask_px=100.2, bid_qty=10.0, ask_qty=10.0)
        self.assertAlmostEqual(v, 100.1, places=6)

    def test_microprice_tilts_to_thin_side(self):
        cfg = bf.FairValueEstimator.Config()
        cfg.mode = bf.FairValueEstimator.Mode.Micro
        fve = bf.FairValueEstimator(cfg)
        # Big bid, small ask → micro tilts toward ask (ask is thinner)
        v = fve.estimate(bid_px=100.0, ask_px=100.2, bid_qty=100.0, ask_qty=1.0)
        self.assertGreater(v, 100.1)

    def test_mid_ignores_qty(self):
        cfg = bf.FairValueEstimator.Config()
        cfg.mode = bf.FairValueEstimator.Mode.Mid
        fve = bf.FairValueEstimator(cfg)
        v = fve.estimate(bid_px=100.0, ask_px=100.2, bid_qty=100.0, ask_qty=1.0)
        self.assertAlmostEqual(v, 100.1, places=6)


class TestRealizedVolSmoke(unittest.TestCase):
    def test_construct_update_compute(self):
        # 10-sample window, 100ms sample interval
        est = bf.RealizedVolEstimator(window_size=10, sample_interval_ns=100_000_000)
        self.assertEqual(est.count(), 0)
        self.assertFalse(est.ready())

        # Feed 15 ticks 100ms apart with varying mid
        for i in range(15):
            est.update(100.0 + 0.01 * i, 100_000_000 * (i + 1))
        self.assertTrue(est.ready())
        self.assertGreater(est.realized_vol(), 0.0)

    def test_reset_clears(self):
        est = bf.RealizedVolEstimator(window_size=5, sample_interval_ns=100_000_000)
        for i in range(10):
            est.update(100.0, 100_000_000 * (i + 1))
        est.reset()
        self.assertEqual(est.count(), 0)


class TestVolGateSmoke(unittest.TestCase):
    def test_disabled_when_zero(self):
        cfg = bf.VolatilityGate.Config()
        cfg.max_bps_per_window = 0.0
        gate = bf.VolatilityGate(cfg)
        self.assertFalse(gate.enabled())
        # Even a large move shouldn't halt when disabled
        self.assertFalse(gate.update_and_check(100.0, 1_000_000_000))
        self.assertFalse(gate.update_and_check(150.0, 1_010_000_000))

    def test_trips_on_large_move(self):
        cfg = bf.VolatilityGate.Config()
        cfg.max_bps_per_window = 50.0
        gate = bf.VolatilityGate(cfg)
        self.assertTrue(gate.enabled())

        # Quiet tick — should not trip
        self.assertFalse(gate.update_and_check(100.0, 1_000_000_000))
        # 100 bps jump (over 50 threshold) — should trip
        halted = gate.update_and_check(101.0, 1_010_000_000)
        self.assertTrue(halted)
        self.assertGreater(gate.last_trip_bps(), 50.0)


class TestOrderBookStateSmoke(unittest.TestCase):
    def _level(self, price, qty):
        lvl = bf.OrderBookState.Level()
        lvl.price = price
        lvl.qty = qty
        return lvl

    def test_apply_and_accessors(self):
        obs = bf.OrderBookState()
        self.assertFalse(obs.ready())

        bids = [self._level(100.0, 5.0), self._level(99.9, 10.0)]
        asks = [self._level(100.1, 3.0), self._level(100.2, 8.0)]
        obs.apply(bid_levels=bids, ask_levels=asks,
                  seq_num=1, timestamp_ns=1_000_000_000, is_snapshot=True)

        self.assertTrue(obs.ready())
        self.assertAlmostEqual(obs.best_bid(), 100.0)
        self.assertAlmostEqual(obs.best_ask(), 100.1)
        self.assertAlmostEqual(obs.mid(), 100.05, places=6)
        self.assertAlmostEqual(obs.size_at_bid(100.0), 5.0)
        self.assertAlmostEqual(obs.size_at_ask(100.1), 3.0)

        # bid_vol_above(100.0): nothing strictly above best bid → 0
        self.assertEqual(obs.bid_vol_above(100.0), 0.0)
        # bid_vol_above(99.9): only price strictly above 99.9 is 100.0 (qty 5) → 5
        self.assertAlmostEqual(obs.bid_vol_above(99.9), 5.0)

    def test_delta_apply_removes_level(self):
        obs = bf.OrderBookState()
        obs.apply([self._level(100.0, 5.0)], [self._level(100.1, 3.0)],
                  seq_num=1, timestamp_ns=1_000_000_000, is_snapshot=True)
        self.assertAlmostEqual(obs.size_at_bid(100.0), 5.0)

        # Delta: qty=0 removes the bid level
        obs.apply([self._level(100.0, 0.0)], [],
                  seq_num=2, timestamp_ns=1_001_000_000, is_snapshot=False)
        self.assertEqual(obs.size_at_bid(100.0), 0.0)


class TestQueueTrackerSmoke(unittest.TestCase):
    def _level(self, price, qty):
        lvl = bf.OrderBookState.Level()
        lvl.price = price
        lvl.qty = qty
        return lvl

    def test_track_then_lookup(self):
        # Build a book with 8.0 worth of bids at our price.
        obs = bf.OrderBookState()
        obs.apply([self._level(100.0, 8.0)], [self._level(100.1, 3.0)],
                  seq_num=1, timestamp_ns=1_000_000_000, is_snapshot=True)

        qt = bf.QueueTracker()
        qt.track(order_id=42, side=bf.OrderSide.Buy, price=100.0,
                 our_qty=2.0, ts_ns=1_000_001_000, book=obs)

        self.assertEqual(qt.size(), 1)
        entry = qt.lookup(42)
        self.assertIsNotNone(entry)
        self.assertAlmostEqual(entry.our_qty, 2.0)
        self.assertAlmostEqual(entry.queue_ahead, 8.0)  # all 8 of size_at_bid is ahead
        self.assertEqual(entry.side, bf.OrderSide.Buy)

        # fill_probability = our_qty / (our_qty + queue_ahead) = 2/10
        self.assertAlmostEqual(qt.fill_probability(42), 0.2, places=6)

    def test_on_trade_decrements_queue_ahead(self):
        obs = bf.OrderBookState()
        obs.apply([self._level(100.0, 10.0)], [self._level(100.1, 5.0)],
                  seq_num=1, timestamp_ns=1_000_000_000, is_snapshot=True)

        qt = bf.QueueTracker()
        qt.track(1, bf.OrderSide.Buy, 100.0, our_qty=1.0,
                 ts_ns=1_000_001_000, book=obs)
        before = qt.lookup(1).queue_ahead

        # Aggressive SELL at our price → passive bid-side fill → ahead drops
        qt.on_trade(aggressor=bf.OrderSide.Sell, trade_price=100.0,
                    trade_qty=3.0, ts_ns=1_002_000_000)
        after = qt.lookup(1).queue_ahead
        self.assertLess(after, before)

    def test_on_cancel_removes_entry(self):
        obs = bf.OrderBookState()
        obs.apply([self._level(100.0, 5.0)], [self._level(100.1, 3.0)],
                  seq_num=1, timestamp_ns=1_000_000_000, is_snapshot=True)

        qt = bf.QueueTracker()
        qt.track(99, bf.OrderSide.Buy, 100.0, our_qty=1.0,
                 ts_ns=1_000_001_000, book=obs)
        self.assertIsNotNone(qt.lookup(99))

        qt.on_cancel(99)
        self.assertIsNone(qt.lookup(99))
        self.assertEqual(qt.size(), 0)


class TestEwmaSmoke(unittest.TestCase):
    def test_ewma_variance_zero_on_flat_price(self):
        v = bf.EwmaVariance(halflife_s=1.0)
        for i in range(1, 50):
            v.update(100.0, i * 1_000_000_000)
        self.assertAlmostEqual(v.value(), 0.0)
        # First update sets baseline only; 48 updates produce EWMA entries.
        self.assertEqual(v.count(), 48)

    def test_ewma_drift_positive_on_uptrend(self):
        import math
        d = bf.EwmaDrift(halflife_s=1.0)
        for i in range(1, 50):
            d.update(100.0 * math.exp(0.0001 * i), i * 1_000_000_000)
        self.assertGreater(d.value(), 0.0)

    def test_kappa_converges(self):
        k = bf.KappaEstimator(halflife_s=1.0)
        # 1 trade per 100ms → kappa per side = 5.0
        for i in range(1, 200):
            k.update(i * 100_000_000)
        self.assertAlmostEqual(k.value(), 5.0, places=1)


if __name__ == "__main__":
    unittest.main()
