# `_bootstrap` — Terraform state backend

Creates the S3 bucket + DynamoDB lock table that every other module's `backend.tf` references. Run this **once**, ahead of everything else.

## The chicken-and-egg

This module uses **local state** (no `backend "s3"` block) because the bucket it would point at doesn't exist yet. After apply, the local `terraform.tfstate` file IS the source of truth for the bootstrap module itself — keep it somewhere safe (1Password, password manager attachment, encrypted backup), but **never commit it.**

The actual long-lived archive bucket (`bpt-tape-archive`) is in the `tape-storage/` module, not here.

## Apply

```bash
terraform init
terraform apply -var "state_bucket_name=bpt-tfstate-$(openssl rand -hex 3)"
# Or set TF_VAR_state_bucket_name in your shell env.
```

Note the outputs — `state_bucket_name` and `state_lock_table_name` go into every other module's `backend.tf`.

## Destroy

```bash
# Empty the bucket first (versioned bucket — must purge versions too):
aws s3api list-object-versions --bucket <name> --output json |
  jq -r '.Versions[]?, .DeleteMarkers[]? | "\(.Key) \(.VersionId)"' |
  while read key vid; do aws s3api delete-object --bucket <name> --key "$key" --version-id "$vid"; done

terraform destroy
```
