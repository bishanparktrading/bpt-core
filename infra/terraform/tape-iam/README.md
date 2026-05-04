# `tape-iam` — Writer + reader IAM users for the archive

Two least-privilege IAM users:

| User | Permissions | Used by |
|---|---|---|
| `bpt-tape-archiver` | PutObject + ListBucket only (NO GetObject, NO DeleteObject) | rclone on the Tokyo recording host |
| `bpt-tape-reader` | GetObject + ListBucket | rclone on backtester hosts |

The writer deliberately can't read or delete — a compromised key on the recorder can disrupt new uploads but **cannot exfiltrate the archive or destroy history**. The reader can't write back — limits blast radius of a leaked laptop.

## Apply

```bash
# Edit backend.tf with values from _bootstrap.
terraform init
terraform apply
```

## Retrieve credentials

```bash
# Writer (paste into recording host's rclone config or /etc/bpt/creds/aws-tape-archiver.json):
terraform output -raw archiver_access_key_id
terraform output -raw archiver_secret_access_key

# Reader (paste into backtester host's rclone config):
terraform output -raw reader_access_key_id
terraform output -raw reader_secret_access_key
```

**Pipe to your password manager immediately. Don't paste into chat / git / Slack.**

## Rotation

```bash
# Force new keys: taint the existing access_key resource and re-apply.
terraform taint aws_iam_access_key.archiver
terraform apply
# Roll the new key onto the host BEFORE deleting the old one — IAM allows
# 2 active keys per user temporarily.
```

## Why no Delete on the writer

Tape is write-once. If a key leaks, the worst the attacker can do is upload junk under their own keys (which costs you a tiny amount until you notice). Without `s3:DeleteObject`, they can't ransom or destroy historical data.
