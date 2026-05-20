# bpt-secmaster · Terraform infra

Provisions everything secmaster needs in AWS ap-northeast-1:

| Resource | Purpose |
|---|---|
| `aws_db_instance.main` | RDS PostgreSQL `db.t4g.micro` (Free Tier 12 mo). Source of truth. |
| `aws_db_subnet_group.main` | Subnets from the default VPC. |
| `aws_security_group.db` | 5432 ingress from trading host + 0.0.0.0/0 (for Lambda). |
| `aws_secretsmanager_secret.db` | Auto-generated DB password + pre-built DSN. |
| `aws_ecr_repository.refresh` | Holds the Lambda container image. |
| `aws_lambda_function.refresh` | The daily refresher. Reads DSN from Secrets Manager. |
| `aws_iam_role.lambda` + 2 policies | Basic exec + `secretsmanager:GetSecretValue` on the DB secret only. |
| `aws_cloudwatch_log_group.lambda` | 30-day retention (default avoids unbounded cost). |
| `aws_cloudwatch_event_rule.daily` | EventBridge cron — fires Lambda daily 03:00 UTC. |

## Prerequisites

- AWS account + credentials (`aws sts get-caller-identity` works)
- Terraform ≥ 1.6
- Docker (for `deploy.sh`)
- The trading host's public IP (`curl ifconfig.me` on the host)

## First-time bootstrap

```bash
cd bpt-secmaster/infra/terraform

# 1. Configure your inputs
cp terraform.tfvars.example terraform.tfvars
$EDITOR terraform.tfvars   # set trading_host_cidr; optionally discord_webhook_url

# 2. Init + plan + apply
terraform init
terraform plan
terraform apply
# (~5 min; RDS provisioning dominates)

# 3. Apply the schema to the empty RDS instance
DSN=$(terraform output -raw fetch_dsn_command | bash)
psql "$DSN" -f ../../schema/001_initial.sql

# 4. Build + push the Lambda container, point Lambda at it
cd ..
./deploy.sh

# 5. Smoke-test: invoke the Lambda manually
aws lambda invoke \
  --function-name $(cd terraform && terraform output -raw lambda_function_name) \
  --region ap-northeast-1 \
  /tmp/out.json
cat /tmp/out.json
```

## Day-2 ops

- **Inspect daily run**: `aws logs tail /aws/lambda/bpt-secmaster-refresh --follow`
- **Force a refresh now**: `aws lambda invoke --function-name bpt-secmaster-refresh /tmp/out.json`
- **Update the schema** (new migration): `psql "$DSN" -f ../../schema/00X_xxx.sql`
- **Rotate Lambda code**: `./deploy.sh` (rebuilds image, pushes, updates Lambda)
- **Tail RDS slow logs**: `aws rds describe-db-log-files --db-instance-identifier bpt-secmaster`

## Cost expectations

| Item | Free Tier (mo 1–12) | After |
|---|---|---|
| RDS db.t4g.micro 750 hrs | $0 | ~$13 |
| RDS 20 GB gp3 | $0 | ~$2.50 |
| RDS automated backups (≤ DB size) | $0 | $0 |
| Lambda (1 invocation/day, ~5 min) | $0 | ~$0 (still under free tier post-12mo) |
| ECR storage (~500 MB) | n/a | ~$0.05 |
| CloudWatch Logs (~50 MB/mo) | $0 (first 5 GB free) | $0 |
| Secrets Manager (1 secret) | n/a | ~$0.40 |
| **Total** | **$0** | **~$16/mo** |

## Tightening the security posture later

The cost-pragmatic v1 ships with `0.0.0.0/0` ingress on the DB SG to
accommodate Lambda's rotating outbound IPs. Tighter alternatives:

- **Put Lambda in VPC + add VPC endpoints for Secrets Manager / ECR API / ECR DKR**
  → SG can drop the public ingress (replace with Lambda SG). Cost: ~$21/mo for
  three interface endpoints. Worth it if you start handling material PII.
- **AWS IAM auth on RDS** (already enabled). bpt-refdata can swap password
  auth for IAM tokens. Removes the password from the trust path entirely.
- **PrivateLink to RDS** from the trading host. Requires a VPN
  (site-to-site, ~$36/mo + per-GB). Only useful if you outgrow IP allowlisting.

None of these matter at v1 scale. Revisit when you have a real reason.

## Destroying the stack

```bash
# Disables deletion_protection on the RDS instance, runs destroy, takes a
# final snapshot so data is recoverable.
terraform apply -var='db_deletion_protection=false'
terraform destroy
```

The DB secret has a 7-day recovery window — if you destroy by accident,
`aws secretsmanager restore-secret --secret-id …` within 7 days recovers it.
