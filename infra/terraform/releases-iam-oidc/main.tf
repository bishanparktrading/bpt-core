// ── GitHub OIDC provider ───────────────────────────────────────────────────
// Shared across ALL GitHub-Actions-to-AWS integrations in this account.
// If another module needs OIDC later (e.g., secmaster Lambda deploy), it
// should reference the provider's ARN from this module's remote state
// rather than creating a duplicate.

// Pull the OIDC issuer's CA thumbprint dynamically. AWS announced in
// 2023 that IAM OIDC providers no longer validate thumbprints for
// well-known issuers (GitHub included), but the resource still requires
// the field populated. Sourcing it from the actual TLS cert keeps the
// value honest if GitHub ever rotates.
data "tls_certificate" "github" {
  url = "https://token.actions.githubusercontent.com"
}

resource "aws_iam_openid_connect_provider" "github" {
  url             = "https://token.actions.githubusercontent.com"
  client_id_list  = ["sts.amazonaws.com"]
  thumbprint_list = [data.tls_certificate.github.certificates[0].sha1_fingerprint]

  tags = {
    Name      = "github-actions-oidc"
    ManagedBy = "terraform-releases-iam-oidc"
  }
}

// Look up the releases bucket so the role's policy stays in sync with
// whatever releases-storage produces.
data "aws_s3_bucket" "releases" {
  bucket = var.releases_bucket_name
}

locals {
  bucket_arn = data.aws_s3_bucket.releases.arn

  // Build the StringLike condition values for the trust policy.
  // Format per GitHub's OIDC docs: repo:<org>/<repo>:ref:<git-ref>
  // The role can be assumed for ANY of these ref patterns.
  trust_sub_patterns = [
    for ref_pattern in var.allowed_refs :
    "repo:${var.github_org}/${var.github_repo}:ref:${ref_pattern}"
  ]
}

// ── Trust policy: who can assume this role ─────────────────────────────────
data "aws_iam_policy_document" "ci_releases_trust" {
  statement {
    sid     = "GitHubActionsAssume"
    effect  = "Allow"
    actions = ["sts:AssumeRoleWithWebIdentity"]

    principals {
      type        = "Federated"
      identifiers = [aws_iam_openid_connect_provider.github.arn]
    }

    // Restrict by audience claim — must be "sts.amazonaws.com" (the value
    // aws-actions/configure-aws-credentials@v4 sends).
    condition {
      test     = "StringEquals"
      variable = "token.actions.githubusercontent.com:aud"
      values   = ["sts.amazonaws.com"]
    }

    // Restrict by subject claim — only this repo's tag pushes + main branch.
    // Fork PRs cannot satisfy this; their sub claim has a different repo.
    condition {
      test     = "StringLike"
      variable = "token.actions.githubusercontent.com:sub"
      values   = local.trust_sub_patterns
    }
  }
}

resource "aws_iam_role" "ci_releases" {
  name               = "bpt-ci-releases-oidc"
  description        = "GitHub Actions release.yml - uploads tarballs to bpt-releases via OIDC. No long-lived keys."
  assume_role_policy = data.aws_iam_policy_document.ci_releases_trust.json
  max_session_duration = 3600  // 1 hour — sufficient for release.yml's ~5 min runtime

  tags = {
    Name      = "bpt-ci-releases-oidc"
    purpose   = "OIDC role for GitHub Actions release workflow"
    ManagedBy = "terraform-releases-iam-oidc"
  }
}

// ── Permissions: same surface as the access-key user it replaces ───────────
data "aws_iam_policy_document" "ci_releases_perms" {
  statement {
    sid       = "ListBucket"
    effect    = "Allow"
    actions   = ["s3:ListBucket", "s3:GetBucketLocation"]
    resources = [local.bucket_arn]
  }

  statement {
    sid    = "PutObjectsOnly"
    effect = "Allow"
    actions = [
      "s3:PutObject",
      "s3:PutObjectTagging",
      "s3:AbortMultipartUpload",
      "s3:ListMultipartUploadParts",
    ]
    resources = ["${local.bucket_arn}/*"]
  }
}

resource "aws_iam_role_policy" "ci_releases" {
  name   = "releases-write"
  role   = aws_iam_role.ci_releases.id
  policy = data.aws_iam_policy_document.ci_releases_perms.json
}
