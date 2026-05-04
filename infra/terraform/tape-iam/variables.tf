variable "aws_region" {
  description = "Region. IAM is global but we still need a provider region."
  type        = string
  default     = "ap-northeast-1"
}

variable "archive_bucket_name" {
  description = "Name of the tape-storage bucket (output from tape-storage module)."
  type        = string
  default     = "bpt-tape-archive"
}

variable "archive_bucket_arn" {
  description = "ARN of the tape-storage bucket. Used to scope policies."
  type        = string
  // Operator sets explicitly OR we look it up via data source — see main.tf.
  default = ""
}
