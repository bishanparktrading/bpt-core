variable "aws_region" {
  description = "Region. IAM is global but we still need a provider region."
  type        = string
  default     = "ap-southeast-1"
}

variable "releases_bucket_name" {
  description = "Name of the releases bucket (created by releases-storage module)."
  type        = string
  default     = "bpt-releases"
}
