// Look up the bucket created by releases-storage so we don't hand-paste ARNs.
data "aws_s3_bucket" "releases" {
  bucket = var.releases_bucket_name
}

locals {
  bucket_arn = data.aws_s3_bucket.releases.arn
}

// ── bpt-ci-releases: write-only for GitHub Actions release.yml ──────────────
// Allowed: Put + ListBucket + AbortMultipartUpload on the releases bucket only.
// NOT allowed: Get/Delete — CI should never read existing releases (deploy
// host has its own reader path) and never delete (versioning + lifecycle
// own the eviction policy).
resource "aws_iam_user" "ci_releases" {
  name = "bpt-ci-releases"
  path = "/bpt-ci/"
  tags = {
    purpose = "GitHub Actions release.yml uploads tarballs to bpt-releases"
  }
}

resource "aws_iam_user_policy" "ci_releases" {
  name = "releases-write"
  user = aws_iam_user.ci_releases.name

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Sid      = "ListBucket"
        Effect   = "Allow"
        Action   = ["s3:ListBucket", "s3:GetBucketLocation"]
        Resource = local.bucket_arn
      },
      {
        Sid    = "PutObjectsOnly"
        Effect = "Allow"
        Action = [
          "s3:PutObject",
          "s3:PutObjectTagging",
          "s3:AbortMultipartUpload",
          "s3:ListMultipartUploadParts",
        ]
        Resource = "${local.bucket_arn}/*"
      },
    ]
  })
}

resource "aws_iam_access_key" "ci_releases" {
  user = aws_iam_user.ci_releases.name
}
