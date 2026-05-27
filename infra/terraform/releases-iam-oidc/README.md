# Releases IAM (OIDC)

Replaces `releases-iam/` (access-key user) with GitHub Actions OIDC role
assumption. No long-lived secrets in the repo; short-lived (1h) credentials
fetched per workflow run via the `id-token: write` permission.

## What this module creates

1. **`aws_iam_openid_connect_provider.github`** — registers
   `https://token.actions.githubusercontent.com` with AWS. Shared across
   the account; other modules wanting GitHub→AWS OIDC should reference
   this provider's ARN (output `github_oidc_provider_arn`), not create a
   duplicate.

2. **`aws_iam_role.ci_releases`** — assumable by GitHub Actions runs that:
   - originate from `bishanparktrading/bpt-core` (sub claim check)
   - run for a `refs/tags/v*` tag OR `refs/heads/main` branch
   - present the standard `sts.amazonaws.com` audience
   - Fork PRs cannot satisfy these checks.

3. **`aws_iam_role_policy.ci_releases`** — same PutObject surface the
   `bpt-ci-releases` user had: list + put + abort multipart on
   `bpt-releases/*`. Nothing else.

## First-time apply

```bash
cd infra/terraform/releases-iam-oidc
terraform init
terraform apply
```

After apply, plug the role ARN into the repo:

```bash
gh variable set BPT_CI_OIDC_ROLE_ARN \
    --body "$(terraform output -raw ci_releases_role_arn)" \
    --repo bishanparktrading/bpt-core
```

The existing `release.yml` already has an OIDC branch
(`if: env.BPT_CI_OIDC_ROLE_ARN != ''`), so once the variable is set the
next release run automatically routes through OIDC instead of access keys.

## Validation

After setting the variable, push a test tag and confirm the workflow
takes the OIDC path:

```bash
git tag v0.0.2-test-oidc
git push origin v0.0.2-test-oidc
gh run watch  # observe the auth step
```

The "Configure AWS credentials (OIDC)" step should run; the "(access keys
fallback)" step should be skipped.

## Cutover: deleting the access-key user

Once an OIDC release succeeds end-to-end:

```bash
# Remove the legacy GitHub secrets
gh secret delete AWS_ACCESS_KEY_ID     --repo bishanparktrading/bpt-core
gh secret delete AWS_SECRET_ACCESS_KEY --repo bishanparktrading/bpt-core

# Tear down the access-key user
cd ../releases-iam
terraform destroy
```

## Why this is better than access keys

| Concern | Access keys | OIDC |
|---|---|---|
| Credential lifetime | Forever until rotated | 1 hour |
| Leak via GitHub log | Bearer credential usable forever | JWT expired before exfil completes |
| Audit granularity | "this IAM user did X" | "run #N on repo Y, tag v0.x did X" |
| Rotation | Manual, periodic | None |
| Fork PR attack surface | Same key, same access | Trust policy rejects different repos |
