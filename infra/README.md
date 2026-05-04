# Infrastructure (Terraform)

AWS provisioning for `bpt-core` — currently scoped to **bpt-tape recording host + archive bucket**. Future work (trading host, monitoring host, etc.) lives alongside under `terraform/`.

## Layout

```
infra/terraform/
├── _bootstrap/    # S3 state bucket + DynamoDB lock table. Run ONCE before everything else.
├── tape-storage/  # S3 bucket for the tape archive + lifecycle policy.
├── tape-iam/      # IAM users: bpt-tape-archiver (writer), bpt-tape-reader (reader).
└── tape-host/     # EC2 instance + EBS + SG + EIP for the Tokyo recorder.
```

## Apply order (first time)

1. **Configure AWS CLI** with an admin profile (`aws configure --profile bpt-admin`). Use a dedicated IAM user, not root.
2. **Bootstrap state backend.**
   ```bash
   cd infra/terraform/_bootstrap
   terraform init
   terraform apply
   ```
   Outputs: `state_bucket_name`, `state_lock_table_name`. Note these — every other module's `backend.tf` references them.
3. **Storage** (creates the archive bucket every IAM user grants will reference):
   ```bash
   cd ../tape-storage
   terraform init
   terraform apply
   ```
4. **IAM** (depends on the bucket existing for ARN-based grants):
   ```bash
   cd ../tape-iam
   terraform init
   terraform apply
   ```
   Outputs include the access key + secret for both users — pipe to your password manager. **Do not commit.**
5. **Host** (the EC2 box):
   ```bash
   cd ../tape-host
   terraform init
   terraform apply -var "operator_ssh_cidr=<your.public.ip>/32" -var "ssh_public_key=$(cat ~/.ssh/id_ed25519.pub)"
   ```
   Outputs: instance public IP. SSH in with the matching private key.

## Region

All modules default to `ap-northeast-1` (Tokyo) — colocates the recording host with HL's matching engine for authentic `recv_ts_ns` timestamps. Override per-module via the `aws_region` variable.

## State backend

S3 + DynamoDB. State files are NEVER committed (see root `.gitignore`).

## What's deliberately NOT here yet

- **Cloud-init / configuration management** — the EC2 instance comes up bare. App deployment runs separately via `deploy/deploy.sh` over SSH. Add Ansible later if/when one VPS becomes >1.
- **Cross-region replication** — single-region (Tokyo) for cost. Add CRR if regional outage tolerance becomes a requirement.
- **VPC peering / private connectivity** — single public subnet with SSH allowlist. Switch to SSM Session Manager for prod (drops the need to expose port 22 at all).
- **Cost alerts** — set in the AWS console for now (Budgets → $50/mo threshold). Move to Terraform once the alert pattern is stable.
- **Trading-host stack** — only the recording host is in scope today. Trading hosts have a different shape (lower latency, more security), separate module when needed.

## Common operations

```bash
# Plan changes without applying:
terraform plan

# See what's deployed:
terraform show
terraform state list

# Destroy a module (careful — destroys real resources):
terraform destroy

# Format all .tf files:
terraform fmt -recursive

# Validate syntax:
terraform validate
```

## Cost ballpark (steady state)

| Resource | Monthly |
|---|---|
| EC2 t3.medium (24/7) | ~$30 |
| 500 GB gp3 EBS | ~$40 |
| Elastic IP (attached) | $0 |
| S3 Standard, 2 TB | ~$46 |
| S3 IA, 5 TB (older years) | ~$62 |
| S3 Glacier DA, 10+ TB (forever) | ~$10/TB/yr |
| **Total Year-1 baseline** | **~$120/mo** |

Numbers are illustrative — read the actual breakdown in each module's README.
