-- bpt-secmaster · migration 002
--
-- Move `multiplier` from instrument → listing.
--
-- Reason: multiplier (contract face value) is intrinsically venue-specific.
-- OKX BTC-USDT-SWAP contract = 0.01 BTC; Binance BTCUSDT perp contract = 1
-- BTC; Deribit BTC-PERPETUAL = 10 USD. They share the same canonical_symbol
-- (BTC/USDT:PERPETUAL or BTC/USD:PERPETUAL.INVERSE) but settle differently
-- because the contract definition differs per venue.
--
-- Having multiplier on instrument was the original schema design bug:
-- every refresh-run from a different venue triggered instrument SCD-2
-- (multiplier "changed" because it came from a different venue), which
-- created a new instrument row, which orphaned the prior listings.
--
-- Fix: each listing carries its own multiplier. Instrument keeps only
-- truly venue-invariant fields (canonical_symbol, class, base/quote/
-- settle ccy, expiry/strike/putcall for derivatives).

BEGIN;

ALTER TABLE listing
    ADD COLUMN multiplier NUMERIC(20, 10) NOT NULL DEFAULT 1.0;

-- Backfill: copy from the (currently-orphaned) parent instrument row.
UPDATE listing l SET multiplier = i.multiplier
FROM instrument i WHERE l.instrument_id = i.id;

ALTER TABLE instrument DROP COLUMN multiplier;

-- ───────────────────── Clean up orphan listings ──────────────────────
-- Listings still pointing at instrument rows whose valid_to is now set
-- (the orphans from the pre-migration bug). Re-point them at the
-- current instrument row for the same canonical_symbol, if one exists.
--
-- We do this once at migration time. After this, the refactored
-- upsert_listing logic (which finds existing listings by
-- venue_native_symbol+exchange, not just instrument_id) will keep
-- things consistent across SCD-2 events going forward.

UPDATE listing l
SET instrument_id = new_inst.id
FROM instrument old_inst, instrument new_inst
WHERE l.instrument_id = old_inst.id
  AND l.valid_to IS NULL
  AND old_inst.valid_to IS NOT NULL
  AND new_inst.canonical_symbol = old_inst.canonical_symbol
  AND new_inst.valid_to IS NULL;

-- Drop any duplicate (instrument_id, exchange_id) current rows that
-- the re-pointing may have created — keep the lowest id (oldest).
WITH dups AS (
    SELECT id,
           ROW_NUMBER() OVER (
             PARTITION BY instrument_id, exchange_id
             ORDER BY id
           ) AS rn
    FROM listing
    WHERE valid_to IS NULL
)
UPDATE listing SET valid_to = now()
WHERE id IN (SELECT id FROM dups WHERE rn > 1);

-- ───────────────────── Bump schema version ──────────────────────────
UPDATE meta SET value = '2' WHERE key = 'schema_version';

COMMIT;
