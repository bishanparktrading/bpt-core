// Sensitive outputs — pipe to your password manager. Never commit.
//
// Usage:
//   terraform output -raw archiver_access_key_id
//   terraform output -raw archiver_secret_access_key

output "archiver_access_key_id" {
  description = "AWS access key ID for the writer (used by rclone on the recording host)."
  value       = aws_iam_access_key.archiver.id
}

output "archiver_secret_access_key" {
  description = "AWS secret access key for the writer. Treat as a credential."
  value       = aws_iam_access_key.archiver.secret
  sensitive   = true
}

output "reader_access_key_id" {
  description = "AWS access key ID for the reader (used by rclone on backtester hosts)."
  value       = aws_iam_access_key.reader.id
}

output "reader_secret_access_key" {
  description = "AWS secret access key for the reader. Treat as a credential."
  value       = aws_iam_access_key.reader.secret
  sensitive   = true
}
