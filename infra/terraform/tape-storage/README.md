# `tape-storage` — S3 archive bucket + lifecycle

Creates the bucket the tape host uploads to, with a 3-tier lifecycle policy.

## Apply

```bash
# First time: edit backend.tf with values from _bootstrap outputs.
terraform init
terraform apply
```

## Lifecycle (defaults)

| Age (days) | Storage class | Cost (~$/TB/mo) |
|---|---|---|
| 0–30 | S3 Standard | $23 |
| 30–365 | S3 Standard-IA | $12.50 |
| 365+ | S3 Glacier Deep Archive | $1 |

Objects are **never deleted** by lifecycle. Tape data is write-once and recapture is impossible — storage cost is the smallest line item in the budget.

## What lives in this bucket

```
s3://bpt-tape-archive/
  raw/<venue>/<YYYY-MM-DD>/<venue>-HHMMSS.wslog.zst
  parquet/<dataset>/<venue>/<symbol>/<YYYY-MM-DD>.parquet
```

Path layout matches what `mdlog_to_parquet.py` writes locally + what the
backtester expects for partition-pruning.

## Manual emergency restore from Glacier DA

Glacier Deep Archive retrievals are **slow** (12-48h) and **paid** (per GB
retrieved). Don't restore casually.

```bash
aws s3api restore-object --bucket bpt-tape-archive \
  --key raw/HYPERLIQUID/2025-01-15/HYPERLIQUID-093000.wslog.zst \
  --restore-request '{"Days":7,"GlacierJobParameters":{"Tier":"Standard"}}'
```
