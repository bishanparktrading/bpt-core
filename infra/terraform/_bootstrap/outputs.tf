output "state_bucket_name" {
  description = "S3 bucket holding Terraform state for all other modules. Wire this into each module's backend.tf."
  value       = aws_s3_bucket.tfstate.id
}

output "state_lock_table_name" {
  description = "DynamoDB table for state locking. Wire this into each module's backend.tf."
  value       = aws_dynamodb_table.tfstate_lock.name
}

output "aws_region" {
  description = "Region the state backend lives in. Other modules should match."
  value       = var.aws_region
}
