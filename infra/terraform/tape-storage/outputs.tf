output "bucket_name" {
  description = "Name of the tape archive bucket. Wire into rclone config + IAM policies."
  value       = aws_s3_bucket.tape_archive.id
}

output "bucket_arn" {
  description = "ARN of the bucket. tape-iam module uses this to scope per-user grants."
  value       = aws_s3_bucket.tape_archive.arn
}
