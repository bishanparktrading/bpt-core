variable "aws_region" {
  description = "Region for the archive bucket. Same as the recording host for free same-region transfers."
  type        = string
  default     = "ap-northeast-1"
}

variable "archive_bucket_name" {
  description = "S3 bucket holding the tape archive. Globally unique — append a suffix if collision."
  type        = string
  default     = "bpt-tape-archive"
}

variable "ia_transition_days" {
  description = "Move objects to S3 Standard-IA after this many days."
  type        = number
  default     = 30
}

variable "glacier_transition_days" {
  description = "Move objects to Glacier Deep Archive after this many days. Must be >= ia_transition_days + 30."
  type        = number
  default     = 365
}
