// Look up the bucket created by tape-storage so we don't hand-paste ARNs.
// If you split deployments by region/account later, swap this for a
// data source remote-state read or pass the ARN as a variable.
data "aws_s3_bucket" "tape_archive" {
  bucket = var.archive_bucket_name
}

locals {
  bucket_arn = data.aws_s3_bucket.tape_archive.arn
}

// ── bpt-tape-archiver: write + read-back for the recording host ─────────────
// Allowed: Put/List/Get (read-back is needed for rclone's idempotency
// check — without GetObject it tries every object every run and fails
// when AWS returns 403 on the existence probe).
// NOT allowed: Delete — a compromised writer key can re-upload junk
// under its own paths (cheap to detect + clean) but cannot ransom or
// destroy historical objects.
resource "aws_iam_user" "archiver" {
  name = "bpt-tape-archiver"
  path = "/bpt-tape/"
  tags = {
    purpose = "rclone uploads from bpt-tape recording host"
  }
}

resource "aws_iam_user_policy" "archiver" {
  name = "tape-write"
  user = aws_iam_user.archiver.name

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
        Sid    = "PutAndReadObjects"
        Effect = "Allow"
        Action = [
          "s3:PutObject",
          "s3:GetObject",
          "s3:AbortMultipartUpload",
          "s3:ListMultipartUploadParts",
        ]
        Resource = "${local.bucket_arn}/*"
      },
    ]
  })
}

resource "aws_iam_access_key" "archiver" {
  user = aws_iam_user.archiver.name
}

// ── bpt-tape-reader: READ-ONLY for backtester hosts ─────────────────────────
resource "aws_iam_user" "reader" {
  name = "bpt-tape-reader"
  path = "/bpt-tape/"
  tags = {
    purpose = "rclone downloads to backtester hosts"
  }
}

resource "aws_iam_user_policy" "reader" {
  name = "tape-read"
  user = aws_iam_user.reader.name

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
        Sid      = "GetObjects"
        Effect   = "Allow"
        Action   = ["s3:GetObject"]
        Resource = "${local.bucket_arn}/*"
      },
    ]
  })
}

resource "aws_iam_access_key" "reader" {
  user = aws_iam_user.reader.name
}
