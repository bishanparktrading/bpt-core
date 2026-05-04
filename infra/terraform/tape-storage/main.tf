// Archive bucket. Versioning ON: tape data is write-once, but versioning
// is cheap insurance against accidental overwrites or deletes.
resource "aws_s3_bucket" "tape_archive" {
  bucket = var.archive_bucket_name
}

resource "aws_s3_bucket_versioning" "tape_archive" {
  bucket = aws_s3_bucket.tape_archive.id
  versioning_configuration {
    status = "Enabled"
  }
}

resource "aws_s3_bucket_server_side_encryption_configuration" "tape_archive" {
  bucket = aws_s3_bucket.tape_archive.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

resource "aws_s3_bucket_public_access_block" "tape_archive" {
  bucket                  = aws_s3_bucket.tape_archive.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

// Lifecycle: Standard → IA → Glacier Deep Archive. Tape data is never
// deleted by lifecycle (Expiration block intentionally absent) — recapture
// is impossible, storage is cheap.
resource "aws_s3_bucket_lifecycle_configuration" "tape_archive" {
  bucket = aws_s3_bucket.tape_archive.id

  rule {
    id     = "tape-tiering"
    status = "Enabled"

    filter {} // applies to all objects

    transition {
      days          = var.ia_transition_days
      storage_class = "STANDARD_IA"
    }

    transition {
      days          = var.glacier_transition_days
      storage_class = "DEEP_ARCHIVE"
    }

    // Old versions (from accidental overwrites) — quicker tier-down to save
    // money since they're just safety copies.
    noncurrent_version_transition {
      noncurrent_days = 30
      storage_class   = "DEEP_ARCHIVE"
    }

    // Eventually clean up old delete markers + expired multipart uploads.
    abort_incomplete_multipart_upload {
      days_after_initiation = 7
    }
  }
}
