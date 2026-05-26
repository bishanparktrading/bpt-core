# Releases IAM

Dedicated IAM user for the GitHub Actions release workflow. Scoped to
PutObject on `bpt-releases/*` ONLY. Separate from `tape-iam/` so the
two credential trails don't entangle.

## First-time apply

```bash
cd infra/terraform/releases-iam
terraform init
terraform apply
```

After apply, pipe the credentials directly to `gh secret set` (don't
echo them to your terminal history):

```bash
gh secret set AWS_ACCESS_KEY_ID     --body "$(terraform output -raw ci_releases_access_key_id)"
gh secret set AWS_SECRET_ACCESS_KEY --body "$(terraform output -raw ci_releases_secret_access_key)"
```

## What this user can do

- `s3:PutObject` on `bpt-releases/*`
- `s3:ListBucket` on `bpt-releases`
- `s3:AbortMultipartUpload` on `bpt-releases/*`

## What this user CANNOT do

- Read existing releases (deploy host has its own reader path)
- Delete anything (versioning + lifecycle own eviction)
- Touch any other bucket

## Rotation

```bash
# Mark the old key inactive, generate new, swap into gh secrets, then delete old.
aws iam update-access-key --user-name bpt-ci-releases \
    --access-key-id <OLD-KEY-ID> --status Inactive
# Re-apply terraform with a refreshed aws_iam_access_key.ci_releases
# resource (taint + apply) to issue a new key.
# Update gh secrets with new values.
# Then delete the old key:
aws iam delete-access-key --user-name bpt-ci-releases --access-key-id <OLD-KEY-ID>
```

Eventually: migrate to GitHub OIDC role assumption — no long-lived keys
to rotate. See `release.yml`'s OIDC branch.
