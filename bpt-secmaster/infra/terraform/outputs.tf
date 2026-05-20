output "rds_endpoint" {
  description = "RDS connection host:port. Use this in libpq DSNs for bpt-refdata."
  value       = "${aws_db_instance.main.address}:${aws_db_instance.main.port}"
}

output "rds_dbname" {
  description = "Postgres database name inside the RDS instance."
  value       = aws_db_instance.main.db_name
}

output "db_secret_arn" {
  description = "Secrets Manager ARN holding the DB credentials. Lambda reads from here; bpt-refdata fetches once and stores via systemd-creds."
  value       = aws_secretsmanager_secret.db.arn
}

output "ecr_repository_url" {
  description = "ECR repo to push the Lambda image to. Used by deploy.sh."
  value       = aws_ecr_repository.refresh.repository_url
}

output "lambda_function_name" {
  description = "Lambda name. Used by deploy.sh for `aws lambda update-function-code`."
  value       = aws_lambda_function.refresh.function_name
}

output "lambda_log_group" {
  description = "CloudWatch log group for the Lambda. Tail with `aws logs tail` for live debugging."
  value       = aws_cloudwatch_log_group.lambda.name
}

output "fetch_dsn_command" {
  description = "One-liner to extract the libpq DSN for psql / bpt-refdata bootstrap."
  value       = "aws secretsmanager get-secret-value --secret-id ${aws_secretsmanager_secret.db.arn} --query SecretString --output text | jq -r .dsn"
}
