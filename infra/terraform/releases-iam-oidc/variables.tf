variable "aws_region" {
  description = "Region. IAM is global but we still need a provider region."
  type        = string
  default     = "ap-southeast-1"
}

variable "github_org" {
  description = "GitHub org/user the repo lives under."
  type        = string
  default     = "bishanparktrading"
}

variable "github_repo" {
  description = "GitHub repo name."
  type        = string
  default     = "bpt-core"
}

variable "allowed_refs" {
  description = "Git ref patterns (StringLike) allowed to assume the role. Default: tag pushes matching v* + workflow_dispatch from main. Anything else is rejected by the trust policy."
  type        = list(string)
  default = [
    "refs/tags/v*",
    "refs/heads/main",
  ]
}

variable "releases_bucket_name" {
  description = "Name of the releases bucket (created by releases-storage module)."
  type        = string
  default     = "bpt-releases"
}
