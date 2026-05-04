variable "aws_region" {
  description = "AWS region for the state bucket + lock table."
  type        = string
  default     = "ap-northeast-1"
}

variable "state_bucket_name" {
  description = "Globally unique name for the Terraform state bucket. Must be unique across all S3 — append a suffix to avoid collisions."
  type        = string
  # No default — operator MUST set this. S3 bucket names are global.
  # Suggested shape: "bpt-tfstate-<random-6-chars>"
}

variable "state_lock_table_name" {
  description = "DynamoDB table name for state locking."
  type        = string
  default     = "bpt-tfstate-lock"
}
