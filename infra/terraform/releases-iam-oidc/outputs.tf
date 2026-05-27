output "ci_releases_role_arn" {
  description = "ARN of the OIDC-assumable role for GitHub Actions release.yml. Set as the BPT_CI_OIDC_ROLE_ARN repo variable to activate the OIDC path in release.yml."
  value       = aws_iam_role.ci_releases.arn
}

output "github_oidc_provider_arn" {
  description = "ARN of the shared GitHub OIDC provider. Reuse this in any other module that wants GitHub Actions to assume an AWS role — don't create a second provider for the same issuer."
  value       = aws_iam_openid_connect_provider.github.arn
}
