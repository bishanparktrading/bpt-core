// Sensitive outputs — pipe directly to `gh secret set`. Never commit.
//
// Usage:
//   terraform output -raw ci_releases_access_key_id
//   terraform output -raw ci_releases_secret_access_key

output "ci_releases_access_key_id" {
  description = "AWS access key ID for the CI release-uploader (GitHub Actions)."
  value       = aws_iam_access_key.ci_releases.id
}

output "ci_releases_secret_access_key" {
  description = "AWS secret access key for the CI release-uploader. Treat as a credential."
  value       = aws_iam_access_key.ci_releases.secret
  sensitive   = true
}

output "ci_releases_username" {
  description = "IAM username — useful for CloudTrail filtering."
  value       = aws_iam_user.ci_releases.name
}
